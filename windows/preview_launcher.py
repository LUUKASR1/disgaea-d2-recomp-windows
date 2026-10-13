"""CI-only preview of the actual Tk UI, with an empty temporary profile."""
from pathlib import Path
import subprocess
import tempfile

from launcher import Backend, VERSION
from launcher_gui import run_gui


def capture(app, target):
    # Screen copy avoids synchronous cross-process paint messages to Tk.
    script = r"""
Add-Type -AssemblyName System.Drawing
$bitmap = New-Object System.Drawing.Bitmap(WIDTH, HEIGHT)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen(X, Y, 0, 0, $bitmap.Size)
$bitmap.Save('TARGET', [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
""".replace('WIDTH', str(app.winfo_width())).replace('HEIGHT', str(app.winfo_height())).replace('X', str(app.winfo_rootx())).replace('Y', str(app.winfo_rooty())).replace('TARGET', str(target.resolve()).replace("'", "''"))
    subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script], check=True, timeout=30)


def main():
    out = Path('launcher-previews'); out.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as root:
        app, show = run_gui(Backend, Path(root), VERSION, lambda directory: False, preview=True)
        try:
            for page, name in [('Início', 'inicio'), ('Vídeo', 'video'), ('Preparação', 'preparacao')]:
                show(page); app.update(); app.update_idletasks()
                capture(app, out / (name + '.png'))
        finally:
            app.destroy()


if __name__ == '__main__':
    main()
