import os
from pathlib import Path
import tempfile
import unittest

import glass_picker as picker


def sparse(path, size):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'wb') as f:
        f.truncate(size)


class GlassPickerLogicTests(unittest.TestCase):
    def test_size_and_colour_helpers(self):
        self.assertEqual(picker.format_size(4 * 1024 ** 3), '4.0 GB')
        self.assertEqual(picker.format_size(700 * 1024 ** 2), '700 MB')
        self.assertEqual(picker.mix('#000000', '#ffffff', 0.5), '#808080')
        self.assertEqual(picker.mix('#102030', '#102030', 0.9), '#102030')

    def test_check_iso_only_accepts_plausible_images(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            good, small, other = root / 'Disgaea D2.iso', root / 'tiny.iso', root / 'game.bin'
            sparse(good, 4 * 1024 ** 3); sparse(small, 1024); sparse(other, 4 * 1024 ** 3)
            self.assertEqual(picker.check_iso(str(good)), (True, 'Pronta para usar • 4.0 GB'))
            self.assertFalse(picker.check_iso(str(small))[0])
            self.assertFalse(picker.check_iso(str(other))[0])
            self.assertFalse(picker.check_iso(str(root / 'missing.iso'))[0])
            self.assertFalse(picker.check_iso('')[0])

    def test_find_isos_prefers_game_names_then_newest_and_skips_small_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            sparse(root / 'ubuntu.iso', 2 * 1024 ** 3)
            sparse(root / 'sub/Disgaea D2 USA.iso', 4 * 1024 ** 3)
            sparse(root / 'sub/deeper/too-deep/hidden.iso', 2 * 1024 ** 3)
            sparse(root / 'tiny.iso', 10)
            sparse(root / 'notes.txt', 3 * 1024 ** 3)
            names = [os.path.basename(p) for p, _, _ in picker.find_isos([root, root])]
            self.assertEqual(names, ['Disgaea D2 USA.iso', 'ubuntu.iso'])

    def test_recent_list_is_deduplicated_capped_and_corruption_safe(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'perfil'
            self.assertEqual(picker.load_recent(home), [])
            for index in range(12):
                picker.save_recent(home, 'C:/isos/%d.iso' % index)
            picker.save_recent(home, 'C:/isos/5.iso')
            recent = picker.load_recent(home)
            self.assertEqual(recent[0], 'C:/isos/5.iso')
            self.assertEqual(len(recent), 8)
            self.assertEqual(len(set(recent)), 8)
            (home / picker.RECENT_FILE).write_text('{broken')
            self.assertEqual(picker.load_recent(home), [])
            (home / picker.RECENT_FILE).write_text('{"recent": [1, "a.iso", null]}')
            self.assertEqual(picker.load_recent(home), ['a.iso'])
            self.assertFalse(list(home.glob('*.tmp')))


if __name__ == '__main__':
    unittest.main()
