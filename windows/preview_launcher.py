"""CI-only preview of the actual Tk UI, with an empty temporary profile."""
from pathlib import Path
import subprocess
import tempfile
import sys

from launcher import Backend, VERSION
from launcher_gui import run_gui
from launcher_i18n import load_language


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
        official = '--official' in sys.argv
        app, show = run_gui(Backend, Path(root) / 'DisgaeaD2Recomp-Graphics-Preview', '0.2.4' if official else VERSION, lambda directory: False, preview=True, official=official)
        try:
            translate, choose_language = app._launcher_i18n
            original_settings = app._launcher_get_settings()
            def all_widgets(widget):
                yield widget
                for child in widget.winfo_children(): yield from all_widgets(child)
            app.geometry('1040x640+0+0'); app.update(); app.update_idletasks()
            language_picker = next(w for w in all_widgets(app) if 'values' in w.keys()
                and tuple(w.cget('values')) == ('Português (Brasil)', 'English'))
            picker_bottom = language_picker.winfo_rooty() - app.winfo_rooty() + language_picker.winfo_height()
            assert picker_bottom <= app.winfo_height() - 20, 'Language picker clipped at minimum height'
            # Keep the whole client area above the taskbar on the CI desktop.
            app.geometry('1040x640+0+0')
            for language in ('pt-BR', 'en'):
                choose_language(language)
                assert load_language(Path(root) / 'DisgaeaD2Recomp-Graphics-Preview') == language
                assert app._launcher_get_settings() == original_settings, 'Language changed graphics values'
                def all_text(widget):
                    values = [str(widget.cget('text'))] if 'text' in widget.keys() else []
                    for child in widget.winfo_children(): values.extend(all_text(child))
                    return values
                assert translate('▶  JOGAR DISGAEA D2') in all_text(app), 'Play button was not localized'
                assert translate('Arquivos e suporte') in all_text(app), 'Navigation was not localized'
                assert translate('Controles') in all_text(app), 'Controls navigation was not localized'
                target = out if language == 'pt-BR' else out / 'en'
                target.mkdir(exist_ok=True)
                for page, name in [('Início', 'inicio'), ('Vídeo', 'video'), ('Controles', 'controles'), ('Preparação', 'preparacao'), ('Arquivos e suporte', 'capturas')]:
                    show(page); app.update(); app.update_idletasks()
                    if page == 'Controles':
                        assert translate('Direcional / analógico esquerdo') in all_text(app)
                        assert translate('Cruz (×) — confirmar') in all_text(app)
                        titles = [w.itemcget(i, 'text') for w in all_widgets(app)
                                  if w.winfo_class() == 'Canvas' for i in w.find_all()
                                  if w.type(i) == 'text']
                        assert translate('Guia de comandos') in titles, 'Outlined title was not localized'
                    capture(app, target / (name + '.png'))
            choose_language('pt-BR')
            assert app._launcher_get_settings() == original_settings
            assert '▶  JOGAR DISGAEA D2' in all_text(app), 'Portuguese roundtrip failed'
        finally:
            app.destroy()


if __name__ == '__main__':
    main()
