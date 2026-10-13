import json
from pathlib import Path, PureWindowsPath
import os
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import launcher
import portable_tools


class PortableToolsTests(unittest.TestCase):
    def test_windows_compiler_paths_do_not_leak_cmake_escapes_or_msvc_headers(self):
        with patch('portable_tools.ensure_tool', return_value=PureWindowsPath('C:/Users/test name/tools')), \
                patch('portable_tools.shutil.which', return_value='portable-tool'), \
                patch.dict(os.environ, {'INCLUDE': 'msvc headers', 'LIB': 'msvc libs', 'CFLAGS': 'foreign flags'}, clear=True):
            env = portable_tools.portable_environment('home', 'bundled', None, None)
        self.assertEqual(env['D2_PORTABLE_RC'], 'C:/Users/test name/tools/bin/x86_64-w64-mingw32-windres.exe')
        self.assertNotIn('INCLUDE', env)
        self.assertNotIn('LIB', env)
        self.assertNotIn('CFLAGS', env)

    def test_rejects_unsafe_zip_before_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ('../escape', '/absolute', 'C:/escape', 'safe/../escape', 'safe/file:stream', 'safe/trailing.'):
                archive = root / 'bad.zip'
                with zipfile.ZipFile(archive, 'w') as z:
                    z.writestr(name, b'invalid')
                with self.assertRaisesRegex(RuntimeError, 'inseguro'):
                    portable_tools.extract_checked(archive, root / 'out')
                self.assertFalse((root / 'out').exists())

    def test_incomplete_download_preserves_previous_tools(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with patch.dict(portable_tools.TOOLS, {'test': ('https://example.test/archive', 'a' * 64, '', ('bin/tool.exe',))}):
                target = root / 'ferramentas-portateis/test-aaaaaaaaaaaa'
                target.mkdir(parents=True)
                (target / 'old.txt').write_text('preserve')
                def download(url, archive, sha):
                    with zipfile.ZipFile(archive, 'w') as z:
                        z.writestr('wrong.exe', b'invalid')
                with self.assertRaisesRegex(RuntimeError, 'incompleto'):
                    portable_tools.ensure_tool(root, 'test', download, lambda text: None)
                self.assertEqual((target / 'old.txt').read_text(), 'preserve')

    def test_complete_tools_reused_without_download(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with patch.dict(portable_tools.TOOLS, {'test': ('https://example.test/archive', 'a' * 64, '', ('bin/tool.exe',))}):
                def download(url, archive, sha):
                    with zipfile.ZipFile(archive, 'w') as z:
                        z.writestr('bin/tool.exe', b'tool')
                target = portable_tools.ensure_tool(root, 'test', download, lambda text: None)
                with patch('portable_tools.ensure_tool', wraps=portable_tools.ensure_tool):
                    result = portable_tools.ensure_tool(root, 'test', lambda *args: self.fail('Unnecessary download'), lambda text: None)
                self.assertEqual(result, target)

    def test_failed_preparation_never_starts_game(self):
        backend = launcher.Backend(lambda text: None)
        with patch.object(backend, 'prepare', side_effect=RuntimeError('preparation failed')), patch.object(backend, 'play') as play:
            with self.assertRaisesRegex(RuntimeError, 'preparation failed'):
                backend.prepare_and_play('game')
            play.assert_not_called()

    def test_preparation_callback_before_game_and_forward_inputs(self):
        backend = launcher.Backend(lambda text: None)
        order = []
        with patch.object(backend, 'prepare', side_effect=lambda *args: order.append(args)), patch.object(backend, 'play', side_effect=lambda: order.append('play')):
            backend.prepare_and_play('game', ['update.pkg'], 'firmware.pup', lambda: order.append('prepared'))
        self.assertEqual(order, [('game', ['update.pkg'], 'firmware.pup'), 'prepared', 'play'])


if __name__ == '__main__':
    unittest.main()
