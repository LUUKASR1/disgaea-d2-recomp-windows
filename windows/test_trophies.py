import tempfile
import unittest
from pathlib import Path

from trophies import reset_trophies, trophy_files


class TrophyResetTest(unittest.TestCase):
    def test_moves_every_build_to_backup(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            for build in ('eu-1', 'abc'):
                f = home / 'builds' / build / 'gamedata/trophies/NPWR05031.json'
                f.parent.mkdir(parents=True)
                f.write_text('{"unlocked":[1]}')
            self.assertEqual(len(trophy_files(home)), 2)
            count, backup = reset_trophies(home, '20261127-1')
            self.assertEqual(count, 2)
            self.assertEqual(trophy_files(home), [])
            self.assertEqual((backup / 'eu-1/NPWR05031.json').read_text(), '{"unlocked":[1]}')

    def test_nothing_to_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(reset_trophies(tmp, 'x')[0], 0)


if __name__ == '__main__':
    unittest.main()
