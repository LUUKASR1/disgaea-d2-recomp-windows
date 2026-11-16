#!/usr/bin/env python3
"""Generate and compile the Windows build locally from the user's own executable (US 1.40 or EU)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ELF_SHA = '0ec183e580a270b0bb1b3a554794a9e4ec2d57a2be251ebdcf81fe21b78d225c'
# Supported executables. 'eu100' is the European disc/update executable (BLES01939); it has the US 1.00 layout
# (entry OPD 0x3E0EE8, TOC 0x3FDE60), so it is built with D2_GAME_VERSION=100 into its own directories.
PROFILES = {
    'us140': dict(sha=ELF_SHA, lift='140', game='140', build='port/build-windows-140', generated='port-140-windows-generated',
                  elf='work/v140/EBOOT.elf', recomp='src/recomp-140', out='out-140', spu='spu-140', label='US 1.40'),
    'eu100': dict(sha='0257bc3ab36a63f55b0a8722fa54f7d5278a568941bdff7817934a891fcb0cbb', lift='eu', game='100',
                  build='port/build-windows-eu', generated='port-eu-windows-generated', elf='work/eu/EBOOT.elf',
                  recomp='src/recomp-eu', out='out-eu', spu='spu-eu', label='EU BLES01939'),
}


def generation_snapshot(directory):
    return {p.relative_to(directory).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(directory.rglob('*')) if p.is_file() and p.name != 'windows-generation.json'}


def verify_generation(directory, sdk_tree, elf_sha=ELF_SHA):
    receipt = json.loads((directory / 'windows-generation.json').read_text(encoding='utf-8'))
    if receipt.get('elf_sha256') != elf_sha or receipt.get('sdk_tree') != sdk_tree:
        raise RuntimeError('Generated input provenance differs; existing files were preserved.')
    if not receipt.get('files') or receipt['files'] != generation_snapshot(directory):
        raise RuntimeError('Generated files changed or are incomplete; existing files were preserved.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf', required=True, type=Path)
    p.add_argument('--json-include', required=True, type=Path,
                   help='Directory containing nlohmann/json.hpp, supplied separately.')
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--iso-vfs', action='store_true', help='Build directly against ISO assets.')
    p.add_argument('--profile', choices=sorted(PROFILES), default='us140', help='Executable profile (default: US 1.40).')
    p.add_argument('--resume', action='store_true', help='Resume a verified, fully generated local build.')
    args = p.parse_args()
    if sys.version_info < (3, 11):
        p.error('Python 3.11 or newer required.')
    portable = bool(os.environ.get('D2_PORTABLE_COMPILER'))
    if os.name != 'nt' or (not portable and (not os.environ.get('LIB') or not os.environ.get('INCLUDE'))):
        p.error('Use the x64 Native Tools Command Prompt for Visual Studio with Windows SDK.')
    if args.jobs < 1:
        p.error('--jobs must be positive.')
    for name in ['git', 'cmake', 'ninja'] + ([] if portable else ['clang-cl']):
        if not shutil.which(name):
            p.error('Missing build tool: ' + name)
    profile = PROFILES[args.profile]
    elf = args.elf.resolve()
    if not elf.is_file() or hashlib.sha256(elf.read_bytes()).hexdigest() != profile['sha']:
        p.error('The supplied ELF is not the supported %s executable.' % profile['label'])
    include = args.json_include.resolve()
    if not (include / 'nlohmann/json.hpp').is_file():
        p.error('nlohmann/json.hpp not found in --json-include.')
    root = Path(__file__).resolve().parents[1]
    sdk = root / 'ps3recomp'
    if not sdk.is_dir():
        p.error('Run tools/bootstrap_windows.py first.')
    lock = json.loads((root / 'patches/SDK-WINDOWS.lock').read_text())
    # Verify the full worktree, including unstaged changes and new helper files,
    # using an isolated temporary index; never mutate the user's SDK index.
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(tmp) / 'verify.index'))
        subprocess.run(['git', '-C', str(sdk), 'read-tree', lock['base']], env=env, check=True)
        subprocess.run(['git', '-C', str(sdk), 'add', '-A'], env=env, check=True)
        tree = subprocess.check_output(['git', '-C', str(sdk), 'write-tree'], env=env, text=True).strip()
        if tree != lock['tree']:
            p.error('SDK sources differ from SDK-WINDOWS.lock.')
    build = root / profile['build']
    generated = root / profile['generated']
    if not args.resume and (build.exists() or generated.exists()):
        p.error('Fresh-build command requires new build/generated directories. Existing files were preserved.')
    def run(*cmd):
        subprocess.run(list(map(str, cmd)), cwd=root, check=True, stdout=sys.stdout, stderr=sys.stderr)
    local_elf = root / profile['elf']
    local_elf.parent.mkdir(parents=True, exist_ok=True)
    if local_elf.exists():
        if hashlib.sha256(local_elf.read_bytes()).hexdigest() != profile['sha']:
            p.error('Existing local ELF differs. It was preserved.')
    elif local_elf.resolve() != elf:
        shutil.copy2(elf, local_elf)
    # The Windows lock is verified above; generate_lifts uses the original
    # macOS lock, so permit this explicitly validated Windows SDK worktree.
    if args.resume:
        verify_generation(generated, lock['tree'], profile['sha'])
        print('Resuming verified generated files.', flush=True)
    else:
        run(sys.executable, root / 'tools/generate_lifts.py', '--version', profile['lift'], '--elf', local_elf,
            '--allow-sdk-worktree', '--port-dir', generated)
        receipt = dict(elf_sha256=profile['sha'], sdk_tree=lock['tree'], files=generation_snapshot(generated))
        (generated / 'windows-generation.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    compiler_options = ['-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl',
                        '-DCMAKE_EXE_LINKER_FLAGS=dxguid.lib']
    if portable:
        for name in ('D2_PORTABLE_COMPILER', 'D2_PORTABLE_CXX', 'D2_PORTABLE_RC'):
            if not Path(os.environ[name]).is_file():
                p.error('Portable compiler incomplete: ' + name)
        compiler_options = [
            '-DCMAKE_C_COMPILER=' + Path(os.environ['D2_PORTABLE_COMPILER']).as_posix(),
            '-DCMAKE_CXX_COMPILER=' + Path(os.environ['D2_PORTABLE_CXX']).as_posix(),
            '-DCMAKE_RC_COMPILER=' + Path(os.environ['D2_PORTABLE_RC']).as_posix(),
            '-DCMAKE_C_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
            '-DCMAKE_CXX_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
            '-DCMAKE_EXE_LINKER_FLAGS=-static -ldxguid -ld3d12 -ldxgi -ld3dcompiler -luuid -lpthread -lxinput9_1_0']
    def configure_and_build(vulkan):
        run('cmake', '-S', root / 'port', '-B', build, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', *compiler_options,
            '-DCMAKE_SUPPRESS_REGENERATION=ON', '-DD2_ENABLE_ISO_VFS=' + ('ON' if args.iso_vfs else 'OFF'),
            '-DD2_GAME_VERSION=' + profile['game'],
            '-DPS3RECOMP_DIR=' + sdk.as_posix(), '-DPython3_EXECUTABLE=' + Path(sys.executable).as_posix(),
            '-DD2_JSON_INCLUDE_DIR=' + include.as_posix(), '-DRECOMP_DIR=' + (generated / profile['recomp']).as_posix(),
            '-DD2_OUT_DIR=' + (generated / profile['out']).as_posix(), '-DSPU_DIR=' + (generated / profile['spu']).as_posix(),
            '-DD2_SPU_SIMD_SSE41=ON', '-DBUILD_TESTING=OFF',
            '-DD2_VULKAN_GAME_BACKEND=' + ('ON' if vulkan else 'OFF'))
        run('cmake', '--build', build, '--parallel', args.jobs)
    # Experimental Vulkan backend. D2_VULKAN_GAME_BACKEND: unset/0 = off (default, stable DirectX 12 build),
    # 1 = required (a failure fails the build), auto = try with Vulkan and, if that cannot be configured or
    # built (no network for the pinned DXC, missing headers...), rebuild DirectX 12 only so the game still builds.
    vulkan_mode = os.environ.get('D2_VULKAN_GAME_BACKEND', '0')
    if vulkan_mode == 'auto':
        try:
            configure_and_build(True)
            print('Vulkan backend: included (experimental, opt-in at runtime).', flush=True)
        except subprocess.CalledProcessError:
            print('Vulkan backend could not be built; building DirectX 12 only.', flush=True)
            configure_and_build(False)
    else:
        configure_and_build(vulkan_mode == '1')
    print('Local build created:', build / 'DisgaeaD2Recomp.exe')
    print('Keep the executable and generated game code local. See docs/WINDOWS.md to run.')


if __name__ == '__main__':
    main()
