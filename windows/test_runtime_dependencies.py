import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch, Mock

import runtime_dependencies as runtime


class RuntimeTests(unittest.TestCase):
    def test_corrupt_second_installer_prevents_all_installations(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            good, bad = root / 'good.exe', root / 'bad.exe'
            good.write_bytes(b'good'); bad.write_bytes(b'broken')
            manifest = root / 'install.json'
            manifest.write_text(json.dumps([{'name': 'vc', 'path': str(good)}, {'name': 'vulkan', 'path': str(bad)}]))
            definitions = {'vc': ('url', hashlib.sha256(b'good').hexdigest(), []),
                           'vulkan': ('url', hashlib.sha256(b'correct').hexdigest(), [])}
            run = Mock()
            with patch.dict(runtime.RUNTIMES, definitions, clear=True):
                with self.assertRaisesRegex(RuntimeError, 'SHA-256'):
                    runtime.install_manifest(manifest, run)
            run.assert_not_called()

    def test_installed_runtimes_need_no_download_or_elevation(self):
        with patch.object(runtime, 'missing_runtimes', return_value=[]), patch.object(runtime, 'elevate_install') as elevate:
            runtime.ensure_runtimes('home', lambda *args: self.fail('Unneeded download'), lambda text: None)
            elevate.assert_not_called()

    def test_newer_microsoft_runtime_is_accepted_and_receipted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            installer = root / 'vc.exe'; installer.write_bytes(b'official fixture')
            manifest = root / 'install.json'
            manifest.write_text(json.dumps([{'name': 'vc', 'path': str(installer)}]))
            with patch.dict(runtime.RUNTIMES, {'vc': ('url', hashlib.sha256(installer.read_bytes()).hexdigest(), ['/install'])}, clear=True):
                runtime.install_manifest(manifest, lambda *args, **kwargs: SimpleNamespace(returncode=1638))
            self.assertEqual(json.loads((root / 'runtime-install-result.json').read_text()), [{'name': 'vc', 'returncode': 1638}])


if __name__ == '__main__':
    unittest.main()
