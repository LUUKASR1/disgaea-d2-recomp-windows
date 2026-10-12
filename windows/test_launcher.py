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


def dlc_fixture(root, package):
    files = ['PARAM.SFO', 'ICON0.PNG', 'USRDIR/Data/flag/flag00010001.edat',
             'USRDIR/Data/flag/flag00040001.edat']
    for name in files:
        path = root / 'dev_hdd0/game/NPUB31321' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'owned DLC fixture')
    log = root / 'log/RPCS3.log'
    log.parent.mkdir(parents=True, exist_ok=True)
    text = '--installpkg\n' + ''.join("PKG: Entry: type=0x80000003, name='" + name + "'\n" for name in files)
    text += 'GUI: Successfully installed ' + str(package) + ' (title_id=NPUB31321, title=DisgaeaD2, version=01.00).\n'
    log.write_text(text)
    return log, text, files


class PreparationTests(unittest.TestCase):
    def test_dlc_recovery_requires_exact_fresh_success_and_entire_file_inventory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package = root / 'owned DLC.pkg'
            log, success, files = dlc_fixture(root, package)
            check = lambda p=package, previous=None, code=3221226505: launcher.completed_dlc_install(root, p, previous, code)
            self.assertTrue(check())
            self.assertFalse(check(previous=launcher.digest(log)))
            self.assertFalse(check(code=1))
            self.assertFalse(check(p=root / 'other.pkg'))
            for text in [success.replace('NPUB31321', 'NPUB00000'), success.replace('01.00', '01.01'),
                         success.replace('Successfully installed', 'Failed to install'),
                         success + "PKG: Entry: type=0x80000003, name='../outside.edat'\n",
                         success + "PKG: Entry: type=0x80000009, name='unknown.bin'\n",
                         success + "PKG: Entry: type=0x80000004, name='../outside'\n",
                         success + "PKG: Entry: type=0x80000003, name='USRDIR/EBOOT.BIN'\n"]:
                log.write_text(text)
                self.assertFalse(check())
            log.write_text(success)
            flag = root / 'dev_hdd0/game/NPUB31321' / files[-1]
            flag.write_bytes(b'')
            self.assertFalse(check())
            flag.unlink()
            self.assertFalse(check())

    def test_dlc_recovery_rejects_missing_metadata_or_flag_inventory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package = root / 'owned DLC.pkg'
            log, text, files = dlc_fixture(root, package)
            for missing in ['PARAM.SFO', 'USRDIR/Data/flag/']:
                log.write_text('\n'.join(line for line in text.splitlines() if missing not in line))
                self.assertFalse(launcher.completed_dlc_install(root, package, None, 3221226505))
            log.write_text(text)
            (root / 'dev_hdd0/game/NPUB31321/PARAM.SFO').unlink()
            self.assertFalse(launcher.completed_dlc_install(root, package, None, 3221226505))

    def test_verified_dlc_recovery_reuses_build_after_elf_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            helper = root / 'helper/rpcs3.exe'
            firmware(helper.parent)
            game = root / 'game'
            binary = game / 'PS3_GAME/USRDIR/EBOOT.BIN'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'owned game fixture')
            update = helper.parent / 'dev_hdd0/game/BLUS31313/USRDIR'
            (update / 'Data').mkdir(parents=True)
            (update / 'EBOOT.BIN').write_bytes(b'update fixture')
            (update / 'Data/START_7.dat').write_bytes(b'update data fixture')
            package = root / 'dlc.pkg'
            package.write_bytes(b'owned package fixture')
            messages = []
            backend = launcher.Backend(messages.append, root / 'home')
            backend.home.mkdir()
            (backend.home / 'preparacao.log').touch()
            previous = dict(workspace='preserved-build', game_root='old', hdd0_root='old')
            elf_bytes = b'supported ELF test fixture'
            def run(args, **kwargs):
                if '--installpkg' in args:
                    log, _, _ = dlc_fixture(helper.parent, package.resolve())
                    raise launcher.StageFailure('PKG', 3221226505, log)
                elif '--decrypt' in args:
                    Path(args[-1]).with_suffix('.elf').write_bytes(elf_bytes)
                else:
                    self.fail('Compatible build must not be recompiled')
            with patch.object(backend, 'cached_build', return_value=previous), \
                 patch.object(backend, 'helper', return_value=helper), \
                 patch.object(backend, 'run', side_effect=run), \
                 patch.object(launcher, 'ELF_SHA', launcher.hashlib.sha256(elf_bytes).hexdigest()):
                backend.prepare(game, [package])
            saved = json.loads((backend.home / 'build-atual.json').read_text())
            self.assertEqual(saved['workspace'], 'preserved-build')
            self.assertEqual(saved['hdd0_root'], str(helper.parent / 'dev_hdd0'))
            self.assertTrue(any('DLC NPUB31321 instalado' in message for message in messages))

    def test_update_abort_requires_exact_package_fresh_success_and_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            package = root / 'owned update.pkg'
            log = root / 'log/RPCS3.log'
            log.parent.mkdir()
            files = [root / 'dev_hdd0/game/BLUS31313' / name for name in
                     ('PARAM.SFO', 'USRDIR/EBOOT.BIN', 'USRDIR/Data/START_7.dat')]
            for path in files:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'local fixture')
            success = '--installpkg\nGUI: Successfully installed ' + str(package) + ' (title_id=BLUS31313, title=Disgaea D2, version=01.40).\n'
            log.write_text(success)
            check = lambda p=package, previous=None, code=3221226505: launcher.completed_update_install(root, p, previous, code)
            self.assertTrue(check())
            self.assertFalse(check(previous=launcher.digest(log)))
            self.assertFalse(check(code=1))
            self.assertFalse(check(p=root / 'different.pkg'))
            for replacement in (success.replace('01.40', '01.30'), success.replace('BLUS31313', 'NPUB31321'), '--installpkg\nInstallation failed'):
                log.write_text(replacement)
                self.assertFalse(check())
            log.write_text(success)
            files[-1].unlink()
            self.assertFalse(check())

    def test_firmware_abort_recovery_requires_fresh_success_and_modules(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            firmware(root)
            log = root / 'log/RPCS3.log'
            log.parent.mkdir()
            log.write_text("argv_utf8: --headless --installfw fixture.pup\nGUI: Successfully installed PS3 firmware version 4.46.\n")
            self.assertTrue(launcher.completed_firmware_install(root, None, 3221226505))
            self.assertFalse(launcher.completed_firmware_install(root, launcher.digest(log), 3221226505))
            self.assertFalse(launcher.completed_firmware_install(root, None, 1))
            log.write_text('argv_utf8: --installfw fixture.pup\nGUI: installation failed\n')
            self.assertFalse(launcher.completed_firmware_install(root, None, 3221226505))
            log.write_text('--installfw\nGUI: Successfully installed PS3 firmware version 4.46.')
            (root / 'dev_flash/sys/external/liblv2.sprx').unlink()
            self.assertFalse(launcher.completed_firmware_install(root, None, 3221226505))

    def test_recovered_package_still_rejects_wrong_elf(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            helper = root / 'helper/rpcs3.exe'
            firmware(helper.parent)
            game = root / 'game'
            eboot = game / 'PS3_GAME/USRDIR/EBOOT.BIN'
            eboot.parent.mkdir(parents=True)
            eboot.write_bytes(b'owned fixture')
            package = root / 'update.pkg'
            package.write_bytes(b'package fixture')
            for name in ('PARAM.SFO', 'USRDIR/EBOOT.BIN', 'USRDIR/Data/START_7.dat'):
                path = helper.parent / 'dev_hdd0/game/BLUS31313' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'update fixture')
            backend = launcher.Backend(lambda message: None, root / 'home')
            def run(args, **kwargs):
                if '--installpkg' in args:
                    log = helper.parent / 'log/RPCS3.log'
                    log.parent.mkdir()
                    log.write_text('--installpkg\nGUI: Successfully installed ' + str(package.resolve()) + ' (title_id=BLUS31313, title=Disgaea D2, version=01.40).')
                    backend.home.mkdir(parents=True, exist_ok=True)
                    (backend.home / 'preparacao.log').touch()
                    raise launcher.StageFailure('PKG', 3221226505, log)
                if '--decrypt' in args:
                    Path(args[-1]).with_suffix('.elf').write_bytes(b'unsupported ELF fixture')
                else:
                    self.fail('Compilation must not start with an unsupported ELF')
            with patch.object(backend, 'helper', return_value=helper), patch.object(backend, 'run', side_effect=run):
                with self.assertRaisesRegex(RuntimeError, 'Executavel diferente'):
                    backend.prepare(game, [package])
            self.assertFalse((backend.home / 'build-atual.json').exists())

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
