#!/usr/bin/env python3
"""Generate and compile Windows 1.40 locally from the user's own executable."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ELF_SHA = '0ec183e580a270b0bb1b3a554794a9e4ec2d57a2be251ebdcf81fe21b78d225c'


def generation_snapshot(directory):
    return {p.relative_to(directory).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(directory.rglob('*')) if p.is_file() and p.name != 'windows-generation.json'}


def verify_generation(directory, sdk_tree):
    receipt = json.loads((directory / 'windows-generation.json').read_text(encoding='utf-8'))
    if receipt.get('elf_sha256') != ELF_SHA or receipt.get('sdk_tree') != sdk_tree:
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
    elf = args.elf.resolve()
    if not elf.is_file() or hashlib.sha256(elf.read_bytes()).hexdigest() != ELF_SHA:
        p.error('The supplied ELF is not the supported US 1.40 executable.')
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
    build = root / 'port/build-windows-140'
    generated = root / 'port-140-windows-generated'
    if not args.resume and (build.exists() or generated.exists()):
        p.error('Fresh-build command requires new build/generated directories. Existing files were preserved.')
    def run(*cmd):
        subprocess.run(list(map(str, cmd)), cwd=root, check=True, stdout=sys.stdout, stderr=sys.stderr)
    local_elf = root / 'work/v140/EBOOT.elf'
    local_elf.parent.mkdir(parents=True, exist_ok=True)
    if local_elf.exists():
        if hashlib.sha256(local_elf.read_bytes()).hexdigest() != ELF_SHA:
            p.error('Existing local ELF differs. It was preserved.')
    elif local_elf.resolve() != elf:
        shutil.copy2(elf, local_elf)
    # The Windows lock is verified above; generate_lifts uses the original
    # macOS lock, so permit this explicitly validated Windows SDK worktree.
    if args.resume:
        verify_generation(generated, lock['tree'])
        print('Resuming verified generated files.', flush=True)
    else:
        run(sys.executable, root / 'tools/generate_lifts.py', '--version', '140', '--elf', local_elf,
            '--allow-sdk-worktree', '--port-dir', generated)
        receipt = dict(elf_sha256=ELF_SHA, sdk_tree=lock['tree'], files=generation_snapshot(generated))
        (generated / 'windows-generation.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    compiler_options = ['-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl',
                        '-DCMAKE_EXE_LINKER_FLAGS=dxguid.lib']
    if portable:
        for name in ('D2_PORTABLE_COMPILER', 'D2_PORTABLE_CXX', 'D2_PORTABLE_RC'):
            if not Path(os.environ[name]).is_file():
                p.error('Portable compiler incomplete: ' + name)
        compiler_options = [
            '-DCMAKE_C_COMPILER=' + os.environ['D2_PORTABLE_COMPILER'],
            '-DCMAKE_CXX_COMPILER=' + os.environ['D2_PORTABLE_CXX'],
            '-DCMAKE_RC_COMPILER=' + os.environ['D2_PORTABLE_RC'],
            '-DCMAKE_C_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
            '-DCMAKE_CXX_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
            '-DCMAKE_EXE_LINKER_FLAGS=-static -ldxguid -ld3d12 -ldxgi -ld3dcompiler -luuid -lpthread -lxinput9_1_0']
    run('cmake', '-S', root / 'port', '-B', build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', *compiler_options,
        '-DCMAKE_SUPPRESS_REGENERATION=ON', '-DD2_ENABLE_ISO_VFS=' + ('ON' if args.iso_vfs else 'OFF'),
        '-DD2_GAME_VERSION=140',
        '-DPS3RECOMP_DIR=' + str(sdk), '-DPython3_EXECUTABLE=' + sys.executable,
        '-DD2_JSON_INCLUDE_DIR=' + str(include), '-DRECOMP_DIR=' + str(generated / 'src/recomp-140'),
        '-DD2_OUT_DIR=' + str(generated / 'out-140'), '-DSPU_DIR=' + str(generated / 'spu-140'),
        '-DD2_SPU_SIMD_SSE41=ON', '-DBUILD_TESTING=OFF')
    run('cmake', '--build', build, '--parallel', args.jobs)
    print('Local build created:', build / 'DisgaeaD2Recomp.exe')
    print('Keep the executable and generated game code local. See docs/WINDOWS.md to run.')


if __name__ == '__main__':
    main()
