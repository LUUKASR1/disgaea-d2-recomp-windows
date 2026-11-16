import json
from pathlib import Path
import tempfile
import unittest
from save_access import save_directory, open_save_directory, SAVE_HELP
from launcher_i18n import Localizer


class SaveAccessTests(unittest.TestCase):
    def test_configured_hdd_matches_game_not_assumed_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'DisgaeaD2Recomp-Graphics-Preview'; home.mkdir()
            hdd = Path(tmp) / 'Área d usuário' / 'old-hdd'
            (home / 'build-atual.json').write_text(json.dumps({'hdd0_root': str(hdd)}))
            self.assertEqual(save_directory(home), hdd / 'home/00000001/savedata')

    def test_first_run_and_invalid_config_fallbacks(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name, hdd in [('DisgaeaD2Recomp-Graphics-Preview','hdd0'),
                              ('DisgaeaD2Recomp-Standalone','hdd0'),
                              ('DisgaeaD2Recomp','auxiliar-rpcs3/dev_hdd0')]:
                home = Path(tmp) / name; home.mkdir()
                expected = home / hdd / 'home/00000001/savedata'
                self.assertEqual(save_directory(home), expected)
                for text in ('broken', '[]', '{"hdd0_root":12}', '{"hdd0_root":"relative"}'):
                    (home / 'build-atual.json').write_text(text)
                    self.assertEqual(save_directory(home), expected)

    def test_open_creates_only_directory_and_preserves_existing_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'DisgaeaD2Recomp-Graphics-Preview'; home.mkdir()
            target = save_directory(home)
            calls = []
            self.assertEqual(open_save_directory(home, calls.append), target)
            save = target / 'OWN-SAVE'; save.mkdir()
            data = save / 'DATA.BIN'; data.write_bytes(b'own-save-sentinel')
            settings = home / 'launcher-settings.json'; settings.write_bytes(b'{"mode":"windowed"}')
            open_save_directory(home, calls.append)
            self.assertEqual(calls, [str(target),str(target)])
            self.assertEqual(data.read_bytes(), b'own-save-sentinel')
            self.assertEqual(settings.read_bytes(), b'{"mode":"windowed"}')

    def test_help_translates_but_preserves_user_path(self):
        path = "C:/Área d'usuário/hdd0/home/00000001/savedata"
        self.assertIn(path, Localizer('en')(SAVE_HELP).format(path=path))
        self.assertIn('decrypted', Localizer('en')(SAVE_HELP))
        self.assertIn('descriptografado', Localizer('pt-BR')(SAVE_HELP))
