"""Regression tests for preparation failures and reuse, without game inputs."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import launcher

spec = importlib.util.spec_from_file_location('build_windows', Path(__file__).resolve().parents[1] / 'tools/build_windows.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


def firmware(directory, value=b'firmware fixture'):
    for name in ('sys/external/liblv2.sprx', 'vsh/module/vsh.self'):
        path = directory / 'dev_flash' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value)


class PreparationTests(unittest.TestCase):
    def test_failed_command_keeps_command_and_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            backend = launcher.Backend(lambda message: None, tmp)
            with self.assertRaisesRegex(RuntimeError, r'fixture failed.*7|codigo 7'):
                backend.run([sys.executable, '-c', 'import sys; print("compiler failure"); sys.exit(7)'],
                            label='fixture failed')
            log = (Path(tmp) / 'preparacao.log').read_text(encoding='utf-8')
            self.assertIn('compiler failure', log)
            self.assertIn('codigo_saida=7', log)
            self.assertIn('comando=', log)

    def test_missing_firmware_is_rejected_before_pkg_install(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            backend = launcher.Backend(lambda message: None, directory)
            with patch.object(backend, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'Firmware ausente'):
                    backend.ensure_firmware(directory / 'rpcs3.exe', None)
                run.assert_not_called()

    def test_copy_firmware_preserves_original_and_backup(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, destination = root / 'user-rpcs3', root / 'helper'
            firmware(source, b'new fixture')
            firmware(destination, b'old fixture')
            backend = launcher.Backend(lambda message: None, root)
            backend.ensure_firmware(destination / 'rpcs3.exe', source)
            self.assertTrue(launcher.firmware_present(destination))
            name = 'sys/external/liblv2.sprx'
            self.assertEqual((source / 'dev_flash' / name).read_bytes(), b'new fixture')
            self.assertEqual((destination / 'dev_flash' / name).read_bytes(), b'new fixture')
            backups = list(destination.glob('dev_flash-anterior-*'))
            self.assertEqual(len(backups), 1)
            self.assertEqual((backups[0] / name).read_bytes(), b'old fixture')

    def test_ready_build_updates_game_path_without_toolchain_or_helper(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            eboot = root / 'owned-game/PS3_GAME/USRDIR/EBOOT.BIN'
            eboot.parent.mkdir(parents=True)
            eboot.write_bytes(b'user fixture')
            previous = dict(workspace='existing-build', game_root='old-folder', hdd0_root='existing-hdd')
            backend = launcher.Backend(lambda message: None, root / 'home')
            with patch.object(backend, 'cached_build', return_value=previous), \
                 patch.object(backend, 'helper') as helper, \
                 patch.object(launcher, 'build_environment', side_effect=AssertionError('unneeded compiler')):
                backend.prepare(eboot.parents[2], [])
                helper.assert_not_called()
            saved = json.loads((root / 'home/build-atual.json').read_text())
            self.assertEqual(saved['game_root'], str(eboot.parents[2].resolve()))
            self.assertEqual(saved['hdd0_root'], 'existing-hdd')

    def test_reuse_signature_changes_when_runtime_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            payload = root / 'payload'
            p = payload / 'port/main.cpp'
            p.parent.mkdir(parents=True)
            p.write_text('fixture')
            with patch.object(launcher, 'ROOT', root):
                before = launcher.build_source_signature(payload)
                p.write_text('changed fixture')
                self.assertNotEqual(before, launcher.build_source_signature(payload))

    def test_resume_rejects_modified_or_missing_generated_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'fixture.c'
            source.write_text('fixture')
            receipt = dict(elf_sha256=build.ELF_SHA, sdk_tree='fixture-tree',
                           files=build.generation_snapshot(root))
            (root / 'windows-generation.json').write_text(json.dumps(receipt))
            build.verify_generation(root, 'fixture-tree')
            source.write_text('modified fixture')
            with self.assertRaisesRegex(RuntimeError, 'changed or are incomplete'):
                build.verify_generation(root, 'fixture-tree')
            source.unlink()
            with self.assertRaisesRegex(RuntimeError, 'changed or are incomplete'):
                build.verify_generation(root, 'fixture-tree')


if __name__ == '__main__':
    unittest.main()
