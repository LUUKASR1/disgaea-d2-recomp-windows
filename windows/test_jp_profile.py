import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_windows
import launcher

ROOT = Path(__file__).resolve().parents[1]


class JapanProfileTests(unittest.TestCase):
    def test_hash_is_consistent_everywhere(self):
        manifest = json.loads((ROOT / 'patches/LIFTS.json').read_text(encoding='utf-8'))
        sha = manifest['versions']['130']['elf_sha256']
        self.assertEqual(sha, launcher.JP_ELF_SHA)
        self.assertEqual(sha, build_windows.PROFILES['jp130']['sha'])

    def test_jp_has_its_own_directories_and_other_profiles_are_untouched(self):
        jp = build_windows.PROFILES['jp130']
        self.assertEqual((jp['game'], jp['lift']), ('130', '130'))
        for other in ('us140', 'eu100'):
            for key in ('build', 'generated', 'elf', 'recomp', 'out', 'spu'):
                self.assertNotEqual(jp[key], build_windows.PROFILES[other][key], key)
        self.assertEqual(build_windows.PROFILES['us140']['game'], '140')
        self.assertEqual(build_windows.PROFILES['eu100']['game'], '100')

    def test_launcher_paths_and_android_layout_follow_the_profile(self):
        self.assertEqual(launcher.profile_name({'profile': 'jp130'}), 'jp130')
        self.assertEqual(launcher.profile_name({'profile': 'nope'}), 'us140')
        exe, elf = launcher.profile_files({'profile': 'jp130'}, Path('w'))
        self.assertEqual((exe.as_posix(), elf.as_posix()), ('w/port/build-windows-130/DisgaeaD2Recomp.exe', 'w/work/v130/EBOOT.elf'))
        self.assertEqual([launcher.PROFILE_LAYOUT[k][4] for k in ('us140', 'eu100', 'jp130')], ['140', '100', '130'])
        jp = build_windows.PROFILES['jp130']
        gen, recomp, out, spu, _ = launcher.PROFILE_LAYOUT['jp130']
        self.assertEqual((gen, 'src/' + recomp, out, spu), (jp['generated'], jp['recomp'], jp['out'], jp['spu']))


if __name__ == '__main__':
    unittest.main()
