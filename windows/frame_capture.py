"""Explicit frame requests over a per-game Win32 event; no continuous trace."""
import ctypes
from datetime import datetime
import json
import os
from pathlib import Path
import threading
import uuid
import zipfile

LIMIT = 8
CAPTURE_ENV = ('D2_CAPTURE_EVENT', 'D2_CAPTURE_MASK_STATE', 'D2_CAPTURE_F8',
               'RSX_CAP', 'RSX_CAP_DIR', 'D2_SCENERY_FEEDBACK_FILE', 'D2_CAPTURE_GOAL')


class WinEvent:
    def __init__(self, name):
        if os.name != 'nt':
            raise RuntimeError('A captura gráfica requer Windows.')
        from ctypes import wintypes
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.CreateEventW.argtypes = (ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR)
        self.kernel.CreateEventW.restype = wintypes.HANDLE
        self.kernel.SetEvent.argtypes = (wintypes.HANDLE,)
        self.kernel.SetEvent.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.handle = self.kernel.CreateEventW(None, False, False, name)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def signal(self):
        if not self.handle or not self.kernel.SetEvent(self.handle):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def clean_capture_environment(original):
    return {key: value for key, value in original.items() if key.upper() not in CAPTURE_ENV}


class CaptureSession:
    def __init__(self, home, version, settings, event_factory=WinEvent, capture_goal="scenery"):
        if capture_goal not in ("scenery", "sprites"):
            raise ValueError("Objetivo de captura inválido.")
        self.capture_goal = capture_goal
        self.lock = threading.Lock()
        self.running = False
        self.pending = False
        self.closed = False
        self.completed_before_request = 0
        token = uuid.uuid4().hex
        self.name = 'Local\\DisgaeaD2Capture-' + token
        self.directory = Path(home) / 'captures' / (datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + token[:8])
        # The audited SDK uses a 300-byte frame-directory buffer.
        if len(str(self.directory).encode('utf-8')) > 240:
            raise RuntimeError('O caminho da captura é longo demais. Use uma pasta local mais curta.')
        self.directory.mkdir(parents=True)
        (self.directory / 'session.json').write_text(json.dumps({
            'schema': 1, 'capture_goal': capture_goal, 'launcher': version, 'settings': settings, 'limit': LIMIT,
            'image': 'internal-scene-with-HUD', 'created': datetime.now().isoformat(),
            'hud_replay': 'experimental-capture-only',
            'description': 'Capturas pontuais e reprodução isolada da HUD quando reconhecida. A renderização normal permanece como referência.'
        }, ensure_ascii=False, indent=2), encoding='utf-8')
        self.event = event_factory(self.name)

    def environment(self, original):
        env = clean_capture_environment(original)
        env.update(D2_CAPTURE_EVENT=self.name, D2_CAPTURE_GOAL=self.capture_goal, D2_CAPTURE_MASK_STATE='1', RSX_CAP_DIR=str(self.directory),
                   D2_SCENERY_FEEDBACK_FILE=str(self.directory.parent / 'scenery-feedback-v1.bin'))
        return env

    def started(self):
        with self.lock:
            if not self.closed:
                self.running = True

    def results(self):
        results = []
        for marker in sorted(self.directory.glob('frame_*/complete.json')):
            try:
                data = json.loads(marker.read_text(encoding='utf-8'))
                if data.get('schema') == 1 and data.get('status') in ('complete', 'failed'):
                    results.append((marker.parent, data))
            except (OSError, ValueError):
                pass  # Renderer may still be writing this single marker.
        return results

    def request(self):
        with self.lock:
            if not self.running or self.closed:
                raise RuntimeError('Abra o jogo com a captura habilitada antes de capturar.')
            results = self.results()
            if len(results) >= LIMIT:
                raise RuntimeError('Limite de oito capturas atingido. Reinicie o jogo para uma nova sessão.')
            if self.pending and len(results) <= self.completed_before_request:
                raise RuntimeError('A captura anterior ainda está sendo gravada.')
            self.completed_before_request = len(results)
            self.event.signal()
            self.pending = True

    def status(self):
        with self.lock:
            results = self.results()
            if len(results) > self.completed_before_request:
                self.pending = False
            complete = sum(item['status'] == 'complete' for _, item in results)
            failed = len(results) - complete
            verified = 0
            output_hud = 0
            output_verified = 0
            for folder, _ in results:
                try:
                    split = json.loads((folder / 'hud-split.json').read_text(encoding='utf-8'))
                    if split.get('status') == 'exact-match': verified += 1
                except (OSError, ValueError):
                    pass
                try:
                    presentation = json.loads((folder / 'presentation.json').read_text(encoding='utf-8'))
                    if presentation.get('hud_after_upscale') is True: output_hud += 1
                    if presentation.get('native_comparison_available') is True and presentation.get('status') == 'exact-match': output_verified += 1
                except (OSError, ValueError):
                    pass
            return f'{complete}/{LIMIT} capturas prontas' + (f' • Replay interno idêntico em {verified}' if verified else '') + (f' • HUD na saída em {output_hud}' if output_hud else '') + (f' • Saída nativa idêntica em {output_verified}' if output_verified else '') + (f' • {failed} falharam' if failed else '') + (' • Gravando…' if self.pending else '')

    def finish(self, game_log=None, destination=None):
        with self.lock:
            self.running = False
            self.closed = True
            self.event.close()
        # Partial attempts are retained for diagnosis rather than silently discarded.
        if not any(self.directory.glob('frame_*')):
            return None
        destination = Path(destination) if destination else Path.home() / 'Downloads'
        if not destination.is_dir():
            destination = self.directory.parent / 'reports'
        destination.mkdir(parents=True, exist_ok=True)
        report = destination / ('DisgaeaD2-Capturas-' + self.directory.name + '.zip')
        temporary = report.with_suffix('.zip.tmp')
        try:
            with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as archive:
                for path in sorted(self.directory.rglob('*')):
                    if path.is_file() and not path.is_symlink():
                        archive.write(path, self.directory.name + '/' + path.relative_to(self.directory).as_posix())
                if game_log and Path(game_log).is_file():
                    archive.write(game_log, self.directory.name + '/game.log')
            temporary.replace(report)
        finally:
            temporary.unlink(missing_ok=True)
        return report
