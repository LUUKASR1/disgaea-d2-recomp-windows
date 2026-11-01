"""Asset-free owned-DLC import regressions; synthetic flag headers, no keys or game data."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import import_owned_dlc as dlc


def flag(content_id='JP0404-NPJB00300_00-SYNTHETIC0000001', license_type=2, body=b'synthetic'):
    header = bytearray(0x100)
    header[:4] = b'NPD\0'
    struct.pack_into('>II', header, 4, 4, license_type)
    header[0x10:0x10 + len(content_id)] = content_id.encode()
    return bytes(header) + body


def owned(root, files):
    folder = Path(root) / 'NPJB00300/USRDIR/Data/flag'
    folder.mkdir(parents=True)
    for name, data in files.items(): (folder / name).write_bytes(data)
    return folder.parents[2]


class OwnedDlcTests(unittest.TestCase):
    def test_region_free_install_with_owner_keys(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = owned(tmp / 'src', {'flag00050002.edat': flag(), 'flag00050001.edat': flag('JP0404-NPJB00300_00-SYNTHETIC0000002', 3),
                                         'notes.txt': b'ignored'})
            keys = tmp / 'keys'; keys.mkdir()
            (keys / 'JP0404-NPJB00300_00-SYNTHETIC0000001.rifkey').write_bytes(bytes(range(16)))
            ready, missing = dlc.install('us', tmp / 'hdd0', [source], keys)
            installed = tmp / 'hdd0/game/NPUB31321/USRDIR/Data/flag'
            self.assertEqual((len(ready), missing), (2, {}))
            self.assertEqual((installed / 'flag00050002.edat.rifkey').read_bytes(), bytes(range(16)))
            self.assertFalse((installed / 'flag00050001.edat.rifkey').exists())   # free license: no key
            self.assertIn(b'NPUB31321', (tmp / 'hdd0/game/NPUB31321/PARAM.SFO').read_bytes())
            self.assertIn(b'GD', (tmp / 'hdd0/game/NPUB31321/PARAM.SFO').read_bytes())
            dlc.install('us', tmp / 'hdd0', [source], keys)                       # identical re-run is fine

    def test_paid_flag_without_owner_key_is_not_installed(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            ready, missing = dlc.install('eu', tmp / 'hdd0', [owned(tmp / 'src', {'flag00020000.edat': flag()})])
            self.assertEqual(ready, [])
            self.assertEqual(missing, {'JP0404-NPJB00300_00-SYNTHETIC0000001': ['flag00020000.edat']})
            self.assertFalse((tmp / 'hdd0/game/NPEB01818').exists())                # nothing created

    def test_other_games_bad_files_and_bad_keys_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            for data in (flag('UP0001-BLUS00000_00-OTHERGAME0000001'), b'not an edat' * 40, flag(license_type=0)):
                src = tmp / ('src%d' % len(list(tmp.iterdir())))
                with self.assertRaises(ValueError):
                    dlc.install('jp', tmp / 'hdd0', [owned(src, {'flag00010001.edat': data})])
            src = owned(tmp / 'srck', {'flag00010001.edat': flag()})
            keys = tmp / 'keys'; keys.mkdir()
            (keys / 'JP0404-NPJB00300_00-SYNTHETIC0000001.rifkey').write_bytes(bytes(17))
            with self.assertRaisesRegex(ValueError, '16 bytes'):
                dlc.install('jp', tmp / 'hdd0', [src], keys)
            self.assertFalse((tmp / 'hdd0/game').exists())

    def test_different_installed_flag_needs_replace(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            dlc.install('jp', tmp / 'hdd0', [owned(tmp / 'a', {'flag00010001.edat': flag(license_type=3)})])
            changed = owned(tmp / 'b', {'flag00010001.edat': flag(license_type=3, body=b'other')})
            with self.assertRaisesRegex(ValueError, '--replace'):
                dlc.install('jp', tmp / 'hdd0', [changed])
            dlc.install('jp', tmp / 'hdd0', [changed], replace=True)
            self.assertTrue((tmp / 'hdd0/game/NPJB00300/USRDIR/Data/flag/flag00010001.edat').read_bytes().endswith(b'other'))


if __name__ == '__main__':
    unittest.main()
