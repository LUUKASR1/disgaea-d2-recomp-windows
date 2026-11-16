import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_windows
import launcher

ROOT = Path(__file__).resolve().parents[1]


class EuropeanProfileTests(unittest.TestCase):
    def test_hash_is_consistent_everywhere(self):
        manifest = json.loads((ROOT / 'patches/LIFTS.json').read_text(encoding='utf-8'))
        sha = manifest['versions']['eu']['elf_sha256']
        self.assertEqual(sha, launcher.EU_ELF_SHA)
        self.assertEqual(sha, build_windows.PROFILES['eu100']['sha'])
        self.assertNotEqual(sha, build_windows.PROFILES['us140']['sha'])

    def test_eu_uses_100_layout_with_its_own_directories(self):
        eu, us = build_windows.PROFILES['eu100'], build_windows.PROFILES['us140']
        self.assertEqual((eu['game'], us['game']), ('100', '140'))
        for key in ('build', 'generated', 'elf', 'recomp', 'out', 'spu'):
            self.assertNotEqual(eu[key], us[key], key)
        self.assertEqual(eu['lift'], 'eu')

    def test_launcher_paths_follow_the_profile(self):
        self.assertEqual(launcher.profile_name({}), 'us140')
        self.assertEqual(launcher.profile_name({'profile': 'eu100'}), 'eu100')
        exe, elf = launcher.profile_files({'profile': 'eu100'}, Path('w'))
        self.assertEqual((exe.as_posix(), elf.as_posix()), ('w/port/build-windows-eu/DisgaeaD2Recomp.exe', 'w/work/eu/EBOOT.elf'))
        exe, elf = launcher.profile_files({}, Path('w'))
        self.assertEqual(elf.as_posix(), 'w/work/v140/EBOOT.elf')

    def test_switching_profiles_keeps_both_saved(self):
        with tempfile.TemporaryDirectory() as tmp:
            home, work = Path(tmp) / 'home', Path(tmp) / 'work'
            for profile in ('us140', 'eu100'):
                exe = launcher.profile_files({'profile': profile}, work / profile)[0]
                exe.parent.mkdir(parents=True); exe.write_bytes(b'x')
            backend = launcher.Backend(lambda message: None, home=home)
            us = {'workspace': str(work / 'us140'), 'iso_path': 'us.iso'}
            eu = {'workspace': str(work / 'eu100'), 'iso_path': 'eu.iso', 'profile': 'eu100'}
            backend.save(us)
            backend.save(eu)
            active = lambda: json.loads((home / 'build-atual.json').read_text(encoding='utf-8'))
            self.assertEqual(active()['iso_path'], 'eu.iso')
            backend.activate_profile('us140')
            self.assertEqual(active()['iso_path'], 'us.iso')
            backend.activate_profile('eu100')
            self.assertEqual(active()['iso_path'], 'eu.iso')

    def test_wrong_executable_is_refused_before_any_work(self):
        with tempfile.TemporaryDirectory() as tmp:
            iso, elf = Path(tmp) / 'a.iso', Path(tmp) / 'EBOOT.elf'
            iso.write_bytes(b'0'); elf.write_bytes(b'not the executable')
            backend = launcher.Backend(lambda message: None, home=Path(tmp) / 'home')
            with self.assertRaises(ValueError):
                backend.prepare_eu(iso, elf)


if __name__ == '__main__':
    unittest.main()
