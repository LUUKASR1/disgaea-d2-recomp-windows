"""CI-only preview of the actual Tk UI, with an empty temporary profile."""
from pathlib import Path
import subprocess
import tempfile
import concurrent.futures
import time

from launcher import Backend
from launcher_gui import run_gui


def capture(app, target):
    script = r'''
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class CaptureD2 {
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
}
'@
$bitmap = New-Object System.Drawing.Bitmap(WIDTH, HEIGHT)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$dc = $graphics.GetHdc()
try { if (![CaptureD2]::PrintWindow([IntPtr]HWND, $dc, 2)) { throw 'PrintWindow failed' } }
finally { $graphics.ReleaseHdc($dc) }
$bitmap.Save('TARGET', [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
'''.replace('WIDTH', str(app.winfo_width())).replace('HEIGHT', str(app.winfo_height())).replace('HWND', str(app.winfo_id())).replace('TARGET', str(target.resolve()).replace("'", "''"))
    # PrintWindow sends paint messages to Tk. Keep its owning thread pumping
    # while the separate capture process waits for those messages.
    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        future = pool.submit(subprocess.run, ['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script], check=True, timeout=30)
        while not future.done():
            app.update()
            time.sleep(0.01)
        future.result()


def main():
    out = Path('launcher-previews'); out.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as root:
        app, show = run_gui(Backend, Path(root), '0.1.8-preview', lambda directory: False, preview=True)
        try:
            for page, name in [('Início', 'inicio'), ('Vídeo', 'video'), ('Preparação', 'preparacao')]:
                show(page); app.update(); app.update_idletasks()
                capture(app, out / (name + '.png'))
        finally:
            app.destroy()


if __name__ == '__main__':
    main()
