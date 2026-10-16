"""Bounded host-frame measurements; no image captures or GPU-time inference."""
import csv
from datetime import datetime
import json
import math
from pathlib import Path
import uuid
import zipfile

LIMIT = 18000
MAX_CSV_BYTES = 8 * 1024 * 1024
COLUMNS = ('frame', 'stage', 'interval_ms', 'render_host_ms', 'pipeline_compile_ms',
           'pipeline_misses', 'pipeline_cache_entries', 'draws', 'fifo_host_ms',
           'wait_fence_host_ms', 'prepare_host_ms', 'present_host_ms', 'wait_fence_calls')
STAGES = {'castle', 'batalha', 'mothman'}
TIMES = ('render_host_ms', 'pipeline_compile_ms', 'fifo_host_ms',
         'wait_fence_host_ms', 'prepare_host_ms', 'present_host_ms')
COUNTS = ('pipeline_misses', 'pipeline_cache_entries', 'draws', 'wait_fence_calls')
ENVIRONMENT = {'D2_FRAME_LIGHT', 'D2_PERF_CSV', 'D2_FIFO_FRAME_TIME'}


def clean_environment(original):
    return {k: v for k, v in original.items() if k.upper() not in ENVIRONMENT}


def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)]


def analyze(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_CSV_BYTES:
        raise ValueError('CSV de desempenho ausente, inválido ou grande demais.')
    groups = {}
    with path.open(encoding='utf-8', newline='') as source:
        reader = csv.DictReader(source)
        if tuple(reader.fieldnames or ()) != COLUMNS:
            raise ValueError('Formato de medição incompatível com esta versão.')
        for index, raw in enumerate(reader):
            if index >= LIMIT or None in raw or any(v is None for v in raw.values()):
                raise ValueError('CSV fora do limite ou com colunas incompletas.')
            if int(raw['frame']) != index or raw['stage'] not in STAGES:
                raise ValueError('Sequência ou cenário de medição inválido.')
            row = {k: float(raw[k]) for k in ('interval_ms', *TIMES)}
            if any(not math.isfinite(v) or v < 0 or v > 1e12 for v in row.values()) or row['interval_ms'] <= 0:
                raise ValueError('Tempo de quadro inválido.')
            row.update({k: int(raw[k]) for k in COUNTS})
            if any(not 0 <= row[k] <= 0xffffffff for k in COUNTS):
                raise ValueError('Contador de medição inválido.')
            groups.setdefault(raw['stage'], []).append(row)
    stages = {}
    for stage, rows in groups.items():
        intervals = [r['interval_ms'] for r in rows]
        total = sum(intervals)
        stages[stage] = {
            'samples': len(rows), 'sampled_seconds': total / 1000,
            'average_present_callback_fps': len(rows) * 1000 / total,
            'frame_ms': {'mean': total / len(rows), 'p50': percentile(intervals, .5),
                         'p95': percentile(intervals, .95), 'p99': percentile(intervals, .99), 'maximum': max(intervals)},
            'frames_over_20ms': sum(v > 20 for v in intervals),
            'frames_over_33_34ms': sum(v > 33.34 for v in intervals),
            'frames_over_50ms': sum(v > 50 for v in intervals),
            'host_timings_ms': {k: {'mean': sum(r[k] for r in rows) / len(rows),
                                    'p95': percentile([r[k] for r in rows], .95)} for k in TIMES},
            'pipeline_misses_total': sum(r['pipeline_misses'] for r in rows),
            'frames_with_pipeline_misses': sum(r['pipeline_misses'] > 0 for r in rows),
            'pipeline_compile_ms_total': sum(r['pipeline_compile_ms'] for r in rows),
            'wait_fence_calls_total': sum(r['wait_fence_calls'] for r in rows),
            'draws_mean': sum(r['draws'] for r in rows) / len(rows)}
    return {'schema': 1, 'purpose': 'dx12-host-frame-baseline',
            'status': 'measured' if stages else 'no-samples',
            'samples': sum(len(rows) for rows in groups.values()), 'stages': stages,
            'gpu_timestamps_available': False, 'gpu_utilization_available': False,
            'whole_game_cpu_time_available': False, 'normal_rendering_changed': False,
            'sample_limit': LIMIT, 'percentile_method': 'nearest-rank',
            'limitations': ['Intervals are between visible render callbacks, not hardware display scanout.',
                           'Host timings are wall times, not CPU utilization or GPU execution time.',
                           'Preparation includes compilation/uploads and may overlap fence waits; do not sum categories.',
                           'FIFO time is completed-drain host time; it may overlap other threads and categories.',
                           'Offscreen work is not represented by render_host_ms alone.',
                           'Compilation time tracks the SDK fragment-pipeline cache; it does not cover all shader compilation.',
                           'Instrumentation overhead has not been measured on the target PC.']}


class PerformanceSession:
    def __init__(self, home, version, settings):
        self.home = Path(home)
        token = datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8]
        self.directory = self.home / 'performance' / token
        self.directory.mkdir(parents=True)
        self.csv = self.directory / 'frames.csv'
        self.result = None
        (self.directory / 'session.json').write_text(json.dumps({
            'schema': 1, 'launcher': version, 'settings': settings,
            'created': datetime.now().isoformat(), 'sample_limit': LIMIT,
            'keys': {'F6': 'castle', 'F7': 'batalha', 'F9': 'mothman', 'F10': 'finish'},
            'images_captured': False, 'per_draw_profiling': False,
            'graphics_api': 'd3d12'}, ensure_ascii=False, indent=2), encoding='utf-8')

    def environment(self, original):
        env = clean_environment(original)
        # Keep heavy diagnostics out even if inherited from an external shell.
        for key in list(env):
            if key.upper() in {'PERF', 'D2_FIFO_DETAIL', 'D2_FIFO_SAMPLE', 'D2_RUNTIME_TIMING'}:
                env.pop(key)
        env.update(D2_FRAME_LIGHT='1', D2_PERF_CSV=str(self.csv), D2_FIFO_FRAME_TIME='1')
        return env

    def finish(self, destination=None):
        if self.result:
            return self.result
        report = analyze(self.csv) if self.csv.exists() else {
            'schema': 1, 'purpose': 'dx12-host-frame-baseline', 'status': 'no-samples',
            'samples': 0, 'stages': {}, 'gpu_timestamps_available': False,
            'message': 'Nenhum cenário marcado. Com a janela do jogo em foco, use F6/F7/F9 e depois F10.'}
        (self.directory / 'summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        destination = Path(destination) if destination else Path.home() / 'Downloads'
        if not destination.is_dir():
            destination = self.home / 'performance/reports'
        destination.mkdir(parents=True, exist_ok=True)
        target = destination / ('DisgaeaD2-Desempenho-' + self.directory.name + '.zip')
        # Exclusive creation: never replace a user's existing report.
        with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED) as archive:
            for name in ('session.json', 'summary.json', 'frames.csv'):
                path = self.directory / name
                if path.is_file() and not path.is_symlink():
                    archive.write(path, name)
        self.result = target
        return target
