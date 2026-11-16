import unittest
from pathlib import Path
import tempfile

from player_config import build_player_config, write_player_config, config_path


class PlayerConfigTests(unittest.TestCase):
    def test_only_differences_are_stored(self):
        base = {'PATH': 'x', 'D2_OLD': '1'}
        env = {'PATH': 'x', 'PS3_VFS_ROOT': 'C:/Jogos/D2', 'D2_GRAPHICS_API': 'vulkan', 'D2_OUTPUT_WIDTH': '1920',
               'D2_VSYNC': '0', 'D2_CAPTURE_F8': '1', 'D2_GAME_ISO': 'a.iso'}
        text = build_player_config(Path('C:/w/EBOOT.elf'), Path('C:/w'), 'C:/Jogos/D2.iso', env, base, ['D2_OLD', 'NOPE'])
        lines = text.splitlines()
        self.assertIn('env.PS3_VFS_ROOT=C:/Jogos/D2', lines)
        self.assertIn('env.D2_GRAPHICS_API=vulkan', lines)
        self.assertIn('iso=C:/Jogos/D2.iso', lines)
        self.assertIn('unset.D2_OLD', lines)
        self.assertNotIn('unset.NOPE', lines)
        for blocked in ('D2_OUTPUT_WIDTH', 'D2_VSYNC', 'D2_CAPTURE_F8', 'D2_GAME_ISO', 'PATH'):
            self.assertFalse(any(line.startswith('env.' + blocked + '=') for line in lines), blocked)

    def test_written_next_to_executable_with_utf8(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'DisgaeaD2Recomp.exe'
            path = write_player_config(exe, 'C:/Usuário/EBOOT.elf', folder, None, {'PS3_HDD0_ROOT': 'D:/Dados é'}, {})
            self.assertEqual(path, config_path(exe))
            text = path.read_text(encoding='utf-8')
            self.assertIn('eboot=C:/Usuário/EBOOT.elf', text)
            self.assertIn('env.PS3_HDD0_ROOT=D:/Dados é', text)


if __name__ == '__main__':
    unittest.main()
