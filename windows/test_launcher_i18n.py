import json
from pathlib import Path
import string
import tempfile
import tkinter as tk
import unittest

from launcher_i18n import Dialogs, LANGUAGES, Localizer, load_language, save_language, text_variable_class


class LanguageTests(unittest.TestCase):
    def test_persistence_and_invalid_preferences_preserve_graphics_and_saves(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'área do usuário'
            self.assertEqual(load_language(home), 'pt-BR')
            home.mkdir(); settings = home / 'launcher-settings.json'
            settings.write_bytes(b'{"mode":"borderless","upscaler":"fsr1"}')
            original = settings.read_bytes()
            save = home / 'savedata.bin'; save.write_bytes(b'user-save')
            save_language(home, 'en'); self.assertEqual(load_language(home), 'en')
            save_language(home, 'pt-BR'); self.assertEqual(load_language(home), 'pt-BR')
            self.assertEqual(settings.read_bytes(), original)
            self.assertEqual(save.read_bytes(), b'user-save')
            self.assertFalse(list(home.glob('*.tmp')))
            for invalid in ('[]', '{"language":[]}', '{"language":"de"}', 'broken'):
                (home / 'launcher-language.json').write_text(invalid)
                self.assertEqual(load_language(home), 'pt-BR')
            with self.assertRaises(ValueError): save_language(home, 'de')

    def test_catalog_placeholder_parity_and_choice_roundtrip(self):
        t = Localizer('en'); fmt = string.Formatter()
        for source, translated in t.catalog.items():
            fields = lambda value: sorted(field for _, field, _, _ in fmt.parse(value) if field)
            self.assertEqual(fields(source), fields(translated), source)
        for choice in ('Janela', 'Tela cheia sem bordas', 'Nativo', 'Desligado', 'FSR 1 — espacial',
                       'Cenário — girar câmera', 'Sprites — movimentar personagem',
                       'Shaders — preparar Vulkan', 'Desenho real — comparar Vulkan'):
            self.assertEqual(t.source(t(choice)), choice)
        self.assertEqual(set(LANGUAGES), {'pt-BR', 'en'})

    def test_nested_status_and_errors_preserve_paths_and_technical_details(self):
        t = Localizer('en')
        path = 'C:\\Users\\João\\Área de Trabalho\\Disgaea D2\\START_7.dat'
        self.assertEqual(t('Relatório de desempenho: ' + path), 'Performance report: ' + path)
        self.assertEqual(t('Executando: Preparar SDK Windows'), 'Running: Prepare Windows SDK')
        self.assertEqual(t('Preparar SDK Windows falhou (codigo 5). Consulte ' + path),
                         'Prepare Windows SDK failed (code 5). See ' + path)
        self.assertEqual(t('1920 × 1080  •  Janela  •  VSync desligado'),
                         '1920 × 1080  •  Windowed  •  VSync off')
        self.assertEqual(t('2/8 capturas prontas • HUD na saída em 1 • Gravando…'),
                         '2/8 captures ready • Output-resolution HUD in 1 • Saving…')
        self.assertEqual(t('HRESULT 0x80004005: ' + path), 'HRESULT 0x80004005: ' + path)
        t.language = 'pt-BR'
        self.assertEqual(t('Executando: Preparar SDK Windows'), 'Executando: Preparar SDK Windows')

    def test_tcl_choices_keep_canonical_values_when_language_changes(self):
        interpreter = tk.Tcl(); t = Localizer('en'); TextVariable = text_variable_class(tk, t)
        mode = TextVariable(master=interpreter, value='Janela', choice=True)
        status = TextVariable(master=interpreter, value='Preparação concluída.')
        self.assertEqual(interpreter.globalgetvar(str(mode)), 'Windowed')
        self.assertEqual(mode.get(), 'Janela')
        # Simulate a selection made by the ttk combobox, bypassing Python.set.
        interpreter.globalsetvar(str(mode), 'Borderless fullscreen')
        self.assertEqual(mode.get(), 'Tela cheia sem bordas')
        t.language = 'pt-BR'; mode.refresh(); status.refresh()
        self.assertEqual(interpreter.globalgetvar(str(mode)), 'Tela cheia sem bordas')
        self.assertEqual(interpreter.globalgetvar(str(status)), 'Preparação concluída.')
        t.language = 'en'; mode.refresh(); status.set('Jogo em execução.')
        self.assertEqual(interpreter.globalgetvar(str(mode)), 'Borderless fullscreen')
        self.assertEqual(interpreter.globalgetvar(str(status)), 'Game running.')

    def test_dialogs_translate_titles_and_filters_not_selected_paths(self):
        calls = []
        class NativeDialog:
            def askopenfilename(self, *args, **kwargs):
                calls.append((args, kwargs)); return 'C:/Área do usuário/jogo.iso'
        dialog = Dialogs(NativeDialog(), Localizer('en'))
        result = dialog.askopenfilename(title='Selecionar ISO de Disgaea D2 USA',
                    filetypes=[('Imagem ISO', '*.iso')], initialdir='C:/Área do usuário')
        self.assertEqual(result, 'C:/Área do usuário/jogo.iso')
        self.assertEqual(calls[0][1]['title'], 'Select a Disgaea D2 USA ISO')
        self.assertEqual(calls[0][1]['filetypes'], [('ISO image', '*.iso')])
        self.assertEqual(calls[0][1]['initialdir'], 'C:/Área do usuário')


if __name__ == '__main__': unittest.main()
