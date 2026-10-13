import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_iso_preview as preview
from build_windows import generation_snapshot


class IsoPreviewTests(unittest.TestCase):
    def test_preview_reuses_verified_generation_and_preserves_original(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); source = root / 'source'; workspace = root / 'old'; output = root / 'iso'
            for path in (source / 'port', source / 'patches', source / 'tools', workspace / 'patches',
                         workspace / 'ps3recomp', workspace / 'work/v140', workspace / 'port-140-windows-generated', root / 'include/nlohmann'):
                path.mkdir(parents=True)
            sdk = workspace / 'ps3recomp'
            subprocess.run(['git', 'init', '-q', str(sdk)], check=True)
            (sdk / 'source.txt').write_text('Synthetic SDK fixture')
            subprocess.run(['git', '-C', str(sdk), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(sdk), '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                            'commit', '-qm', 'Synthetic fixture'], check=True)
            base = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
            tree = subprocess.check_output(['git', '-C', str(sdk), 'write-tree'], text=True).strip()
            lock = dict(base=base, tree=tree)
            for path in (source, workspace): (path / 'patches/SDK-WINDOWS.lock').write_text(json.dumps(lock))
            (source / 'port/CMakeLists.txt').write_text('Synthetic port fixture')
            (source / 'tools/build_iso_preview.py').write_text('Synthetic builder identity')
            (root / 'include/nlohmann/json.hpp').write_text('Synthetic header')
            (workspace / 'work/v140/EBOOT.elf').write_bytes(b'Synthetic owned ELF; no game code')
            generated = workspace / 'port-140-windows-generated'
            (generated / 'fixture.cpp').write_text('Synthetic generated input')
            (generated / 'windows-generation.json').write_text(json.dumps(dict(
                elf_sha256=preview.ELF_SHA, sdk_tree=tree, files=generation_snapshot(generated))))
            original = {str(p.relative_to(workspace)): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in workspace.rglob('*') if p.is_file()}
            actual_sha = preview.sha
            def fake_sha(path):
                return preview.ELF_SHA if Path(path).name == 'EBOOT.elf' else actual_sha(path)
            commands = []
            def run(command, env):
                commands.append(list(map(str, command)))
                if command[0] == 'git': subprocess.run(list(map(str, command)), env=env, check=True)
                elif command[:2] == ['cmake', '--build']:
                    exe = output / 'build/DisgaeaD2Recomp.exe'; exe.parent.mkdir(parents=True)
                    exe.write_bytes(b'Synthetic native executable')
            import os
            env = dict(os.environ, D2_TOOLCHAIN_ID='fixture', D2_PORTABLE_COMPILER='cc',
                       D2_PORTABLE_CXX='cxx', D2_PORTABLE_RC='rc')
            with patch.object(preview, 'sha', side_effect=fake_sha):
                exe = preview.build(source, workspace, output, root / 'include', run, env)
                configure = next(c for c in commands if c[0] == 'cmake' and '--build' not in c)
                self.assertIn('-DD2_ENABLE_ISO_VFS=ON', configure)
                self.assertIn('-DRECOMP_DIR=' + (generated.resolve() / 'src/recomp-140').as_posix(), configure)
                self.assertEqual(exe, output.resolve() / 'build/DisgaeaD2Recomp.exe')
                count = len(commands)
                self.assertEqual(preview.build(source, workspace, output, root / 'include', run, env), exe)
                self.assertEqual(len(commands), count, 'Cached ISO runtime must not rebuild')
                with self.assertRaises(RuntimeError):
                    preview.build(source, workspace, workspace / 'iso', root / 'include', run, env)
            after = {str(p.relative_to(workspace)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in workspace.rglob('*') if p.is_file()}
            self.assertEqual(original, after, 'Original SDK/index/ELF/generation must remain unchanged')

    def test_clean_iso_preparation_never_requires_extracted_assets(self):
        import launcher
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); payload = root / 'bundle/payload'; payload.mkdir(parents=True)
            (payload / 'tools').mkdir(); (payload / 'tools/build_windows.py').write_text('Synthetic')
            home = root / 'clean'; home.mkdir()
            metadata = home / 'metadata'; (metadata / 'PS3_GAME').mkdir(parents=True)
            (metadata / 'PS3_GAME/PARAM.SFO').write_bytes(b'Synthetic metadata only')
            helper = home / 'helper/rpcs3.exe'; helper.parent.mkdir(); helper.write_bytes(b'Synthetic helper')
            update = helper.parent / 'dev_hdd0/game/BLUS31313/USRDIR'
            (update / 'Data').mkdir(parents=True)
            (update / 'Data/START_7.dat').write_bytes(b'Synthetic update')
            (update / 'EBOOT.BIN').write_bytes(b'Synthetic owned binary')
            image = root / 'game.iso'; image.write_bytes(b'Synthetic ISO')
            backend = launcher.Backend(lambda message: None, home=home)
            commands = []
            def run(command, *args, **kwargs):
                commands.append(list(map(str, command)))
                if '--decrypt' in list(map(str, command)):
                    (home / 'entrada-local/EBOOT.elf').write_bytes(b'Synthetic owned ELF')
            with patch.object(launcher, 'ROOT', root / 'bundle'), \
                 patch.object(launcher, 'digest', return_value=launcher.ELF_SHA), \
                 patch.object(backend, 'prepare_iso_source', return_value=metadata), \
                 patch.object(backend, 'check_game_files', side_effect=AssertionError('Extracted assets forbidden')), \
                 patch.object(backend, 'helper', return_value=helper), \
                 patch.object(backend, 'ensure_firmware'), \
                 patch.object(backend, 'compiler_environment', return_value={'D2_TOOLCHAIN_ID': 'fixture'}), \
                 patch.object(backend, 'run', side_effect=run):
                backend.prepare(None, [], iso_path=image)
            config = json.loads((home / 'build-atual.json').read_text())
            self.assertTrue(config['native_iso'])
            self.assertEqual(config['iso_path'], str(image))
            self.assertEqual(config['game_root'], str(metadata))
            self.assertTrue(any('--iso-vfs' in command for command in commands))
            self.assertFalse((metadata / 'PS3_GAME/USRDIR').exists())

    def test_iso_play_requires_existing_preparation(self):
        import launcher
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); image = root / 'game.iso'; image.write_bytes(b'Fixture')
            backend = launcher.Backend(lambda message: None, home=root / 'home')
            with self.assertRaisesRegex(RuntimeError, 'Prepare o jogo'):
                backend.play_iso(image)


if __name__ == '__main__': unittest.main()
