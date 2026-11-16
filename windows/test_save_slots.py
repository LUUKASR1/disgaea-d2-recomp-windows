import tempfile
import unittest
from pathlib import Path

import save_slots
from save_access import save_directory


def make_home(tmp):
    home = Path(tmp) / 'DisgaeaD2Recomp-Standalone'
    home.mkdir()
    return home


def write_save(home, slot, text):
    d = save_directory(home) / slot
    d.mkdir(parents=True, exist_ok=True)
    (d / 'DATA.BIN').write_text(text)


class SaveSlotsTests(unittest.TestCase):
    def test_nothing_to_back_up(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = make_home(tmp)
            self.assertIsNone(save_slots.create_backup(home))
            self.assertEqual(save_slots.list_backups(home), [])

    def test_backup_and_restore_keeps_current_saves(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = make_home(tmp)
            write_save(home, 'SLOT1', 'antigo')
            backup = save_slots.create_backup(home, 'meu save')
            self.assertTrue((backup / 'SLOT1/DATA.BIN').is_file())
            write_save(home, 'SLOT1', 'novo')
            write_save(home, 'SLOT2', 'extra')
            safety = save_slots.restore_backup(home, backup)
            self.assertEqual((save_directory(home) / 'SLOT1/DATA.BIN').read_text(), 'antigo')
            self.assertFalse((save_directory(home) / 'SLOT2').exists())
            self.assertEqual((safety / 'SLOT1/DATA.BIN').read_text(), 'novo')
            self.assertTrue((safety / 'SLOT2/DATA.BIN').is_file())
            self.assertEqual(len(save_slots.list_backups(home)), 2)

    def test_auto_backups_are_pruned_and_manual_ones_kept(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = make_home(tmp)
            write_save(home, 'SLOT1', 'x')
            save_slots.create_backup(home, 'manual')
            root = save_slots.backups_directory(home)
            for i in range(8):
                (root / f'2020-01-0{i + 1}_00-00-00-auto').mkdir()
            save_slots.prune_auto_backups(home)
            names = [n for _, n, _ in save_slots.list_backups(home)]
            self.assertEqual(sum('-auto' in n for n in names), save_slots.KEEP_AUTO)
            self.assertTrue(any('manual' in n for n in names))

    def test_paths_outside_backup_folder_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = make_home(tmp)
            write_save(home, 'SLOT1', 'x')
            with self.assertRaises(ValueError):
                save_slots.restore_backup(home, Path(tmp))
            with self.assertRaises(ValueError):
                save_slots.delete_backup(home, save_directory(home))


if __name__ == '__main__':
    unittest.main()
