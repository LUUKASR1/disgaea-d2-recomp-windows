"""Direct game startup reuses the existing prepared profile and launch checks."""
import base64
import ctypes
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import traceback


def prepared_profile(home):
    try:
        data = json.loads((Path(home) / 'build-atual.json').read_text(encoding='utf-8'))
        if not isinstance(data, dict):
            return False
        workspace = data.get('workspace')
        source = data.get('iso_path') or data.get('game_root')
        if not isinstance(workspace, str) or not workspace or not isinstance(source, str) or not source:
            return False
        return ((Path(workspace) / 'work/v140/EBOOT.elf').is_file()
                and (Path(source).is_file() if data.get('iso_path') else Path(source).is_dir()))
    except (OSError, ValueError, TypeError):
        return False


def powershell(script):
    binary = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    encoded = base64.b64encode(script.encode('utf-16-le')).decode('ascii')
    return subprocess.run([str(binary), '-NoLogo', '-NoProfile', '-NonInteractive',
                           '-EncodedCommand', encoded], check=True, timeout=20,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))


def create_shortcuts(executable, destination=None):
    if os.name != 'nt':
        raise RuntimeError('Os atalhos estão disponíveis no Windows.')
    executable = Path(executable).resolve()
    if not executable.is_file() or executable.suffix.lower() != '.exe':
        raise ValueError('Executável ausente. Extraia o aplicativo para uma pasta permanente.')
    if destination is None:
        desktop = ctypes.create_unicode_buffer(32768)
        result = ctypes.windll.shell32.SHGetFolderPathW(None, 0x10, None, 0, desktop)
        if result != 0:
            raise OSError('Não foi possível localizar a área de trabalho.')
        destination = Path(desktop.value)
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    def quote(value):
        return "'" + str(value).replace("'", "''") + "'"
    lines = ["$ErrorActionPreference='Stop'", "$shell=New-Object -ComObject WScript.Shell"]
    for name, argument in (('Disgaea D2.lnk', '--play'), ('Disgaea D2 - Configurações.lnk', '--settings')):
        lines += ["$link=$shell.CreateShortcut(" + quote(destination / name) + ")",
                  '$link.TargetPath=' + quote(executable), '$link.Arguments=' + quote(argument),
                  '$link.WorkingDirectory=' + quote(executable.parent),
                  '$link.IconLocation=' + quote(str(executable) + ',0'), '$link.Save()']
    powershell('\n'.join(lines))
    return destination


def run_direct(Backend, home, version):
    """Return False to open settings/setup, True after a completed direct session."""
    import tkinter as tk
    from tkinter import messagebox, ttk
    from launcher_i18n import Localizer, load_language
    if not prepared_profile(home):
        return False
    translate = Localizer(load_language(home))
    events = queue.Queue()
    root = tk.Tk()
    root.title('Disgaea D2 • ' + version)
    root.geometry('500x125'); root.resizable(False, False)
    status = tk.StringVar(value=translate('Iniciando o jogo…'))
    ttk.Label(root, textvariable=status, wraplength=460).pack(padx=20, pady=16)
    progress = ttk.Progressbar(root, mode='indeterminate'); progress.pack(fill='x', padx=20); progress.start()
    assets = (Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1])) / 'windows/assets/launcher.ico')
    if os.name == 'nt' and assets.is_file():
        root.iconbitmap(default=str(assets))
    result = [True]
    def close():
        messagebox.showinfo('Disgaea D2', translate('Aguarde a preparação ou feche a janela do jogo.'), parent=root)
    root.protocol('WM_DELETE_WINDOW', close)
    def execute():
        try:
            backend = Backend(lambda message: events.put(('status', message)), home=home)
            backend.play(on_game_started=lambda: events.put(('started', '')))
        except Exception as error:
            Path(home).mkdir(parents=True, exist_ok=True)
            try:
                with (Path(home) / 'preparacao.log').open('a', encoding='utf-8') as f:
                    traceback.print_exc(file=f)
            except OSError:
                pass
            events.put(('error', str(error)))
        else:
            events.put(('done', ''))
    def poll():
        while not events.empty():
            kind, message = events.get()
            if kind == 'status':
                status.set(translate(message))
            elif kind == 'started':
                progress.stop(); root.withdraw()
            elif kind == 'error':
                root.deiconify()
                result[0] = not messagebox.askyesno('Disgaea D2', translate('Não foi possível iniciar o jogo. Abrir configurações?') + '\n\n' + message, parent=root)
                root.destroy(); return
            elif kind == 'done':
                root.destroy(); return
        root.after(100, poll)
    threading.Thread(target=execute, daemon=False).start()
    root.after(100, poll); root.mainloop()
    return result[0]
