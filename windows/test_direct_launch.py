import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from direct_launch import prepared_profile, run_direct, create_shortcuts, powershell


def profile(directory):
    home = Path(directory)
    workspace = home / 'workspace'
    elf = workspace / 'work/v140/EBOOT.elf'; elf.parent.mkdir(parents=True)
    elf.write_bytes(b'synthetic ELF fixture, not a game')
    iso = home / 'owned.iso'; iso.write_bytes(b'synthetic ISO fixture')
    data = {'workspace': str(workspace), 'iso_path': str(iso)}
    (home / 'build-atual.json').write_text(json.dumps(data), encoding='utf-8')
    return home, data


class DirectLaunchTests(unittest.TestCase):
    def test_existing_iso_and_folder_profiles_are_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            home, data = profile(tmp)
            save = home / 'savedata.bin'; save.write_bytes(b'preserve')
            before = (home / 'build-atual.json').read_bytes()
            self.assertTrue(prepared_profile(home))
            self.assertEqual((home / 'build-atual.json').read_bytes(), before)
            self.assertEqual(save.read_bytes(), b'preserve')
            data.pop('iso_path'); data['game_root'] = str(home)
            (home / 'build-atual.json').write_text(json.dumps(data))
            self.assertTrue(prepared_profile(home))
            (Path(data['workspace']) / 'work/v140/EBOOT.elf').unlink()
            self.assertFalse(prepared_profile(home))

    def test_missing_invalid_or_moved_inputs_return_to_setup(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            self.assertFalse(prepared_profile(home))
            for text in ('broken', '[]', '{}', '{"workspace":12}', '{"workspace":"","iso_path":""}'):
                (home / 'build-atual.json').write_text(text)
                self.assertFalse(prepared_profile(home))
            home, data = profile(tmp)
            Path(data['iso_path']).unlink()
            self.assertFalse(prepared_profile(home))

    def test_first_run_opens_setup_without_constructing_a_game_window(self):
        with tempfile.TemporaryDirectory() as tmp, patch('tkinter.Tk') as window:
            self.assertFalse(run_direct(None, Path(tmp), 'test'))
            window.assert_not_called()

    @unittest.skipUnless(os.name == 'nt', 'Win32 Tk lifecycle')
    def test_direct_session_closes_and_does_not_enable_diagnostics(self):
        with tempfile.TemporaryDirectory() as tmp:
            home, _ = profile(tmp)
            calls = []
            class Backend:
                def __init__(self, notify, home): self.notify = notify
                def play(self, on_game_started):
                    calls.append('play'); self.notify('Jogo em execução.'); on_game_started()
            self.assertTrue(run_direct(Backend, home, 'test'))
            self.assertEqual(calls, ['play'])

    @unittest.skipUnless(os.name == 'nt', 'Win32 Tk error handling')
    def test_failed_launch_is_logged_and_can_return_to_settings(self):
        with tempfile.TemporaryDirectory() as tmp:
            home, _ = profile(tmp)
            class Backend:
                def __init__(self, notify, home): pass
                def play(self, on_game_started): raise RuntimeError('Synthetic startup failure')
            with patch('tkinter.messagebox.askyesno', return_value=True):
                self.assertFalse(run_direct(Backend, home, 'test'))
            self.assertIn('Synthetic startup failure', (home / 'preparacao.log').read_text())

    @unittest.skipUnless(os.name == 'nt', 'Real Windows shortcuts')
    def test_shortcuts_with_unicode_spaces_and_quotes(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / "Área d'usuário"; home.mkdir()
            executable = home / "Disgaea D2 d'teste.exe"; executable.write_bytes(b'fixture')
            destination = home / 'atalhos'
            self.assertEqual(create_shortcuts(executable, destination), destination.resolve())
            self.assertEqual(len(list(destination.glob('*.lnk'))), 2)
            quoted = lambda p: "'" + str(p).replace("'", "''") + "'"
            result = powershell("[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
                "$s=New-Object -ComObject WScript.Shell; @(" + ','.join(
                    "$s.CreateShortcut(" + quoted(destination / n) + ")"
                    for n in ('Disgaea D2.lnk', 'Disgaea D2 - Configurações.lnk')) +
                ") | Select-Object TargetPath,Arguments,WorkingDirectory | ConvertTo-Json")
            records = json.loads(result.stdout.decode('utf-8-sig'))
            self.assertEqual([r['Arguments'] for r in records], ['--play', '--settings'])
            for r in records:
                self.assertEqual(Path(r['TargetPath']), executable.resolve())
                self.assertEqual(Path(r['WorkingDirectory']), home.resolve())
