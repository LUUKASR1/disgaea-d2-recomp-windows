"""Release channel isolation: stable must keep the validated saves profile."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


class OfficialReleaseTests(unittest.TestCase):
    def load_channel(self, root, official):
        vendor = root / 'vendor'
        vendor.mkdir(exist_ok=True)
        for name in ('standalone.json', 'graphics.json'):
            (vendor / name).write_text('{}')
        marker = vendor / 'official-release.json'
        if official:
            marker.write_text(json.dumps({'version': '0.2.3', 'graphics_api': 'd3d12'}))
        else:
            marker.unlink(missing_ok=True)
        spec = importlib.util.spec_from_file_location('channel_under_test', Path(__file__).with_name('launcher.py'))
        module = importlib.util.module_from_spec(spec)
        with patch.object(sys, '_MEIPASS', str(root), create=True), patch.dict(os.environ, {'LOCALAPPDATA': str(root / 'local')}):
            spec.loader.exec_module(module)
        return module

    def test_official_upgrade_preserves_preview_profile_and_existing_save(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            preview = self.load_channel(root, False)
            save = preview.HOME / 'hdd0/home/00000001/savedata/fixture/save.bin'
            save.parent.mkdir(parents=True)
            save.write_bytes(b'user-owned test save')
            stable = self.load_channel(root, True)
            self.assertTrue(stable.OFFICIAL_RELEASE)
            self.assertEqual(stable.VERSION, '0.2.3')
            self.assertTrue(stable._standalone)
            self.assertEqual(stable.HOME, preview.HOME)
            self.assertEqual(save.read_bytes(), b'user-owned test save')

    def test_development_package_remains_separate_channel(self):
        with tempfile.TemporaryDirectory() as temporary:
            development = self.load_channel(Path(temporary), False)
            self.assertFalse(development.OFFICIAL_RELEASE)
            self.assertIn('preview', development.VERSION)


if __name__ == '__main__':
    unittest.main()
