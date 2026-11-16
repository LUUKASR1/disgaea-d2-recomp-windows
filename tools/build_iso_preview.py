#!/usr/bin/env python3
"""Compile an isolated ISO runtime using a verified existing local 1.40 lift."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_windows import ELF_SHA, verify_generation


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        while chunk := source.read(1024 * 1024): digest.update(chunk)
    return digest.hexdigest()


def source_hash(source):
    digest = hashlib.sha256()
    for directory in ('port', 'patches'):
        for path in sorted((source / directory).rglob('*')):
            if path.is_file():
                digest.update(path.relative_to(source).as_posix().encode())
                digest.update(path.read_bytes())
    return digest.hexdigest()


def build(source, workspace, preview, include, run, env):
    source, workspace, preview, include = map(lambda p: Path(p).resolve(), (source, workspace, preview, include))
    if preview == workspace or preview.is_relative_to(workspace):
        raise RuntimeError('A build ISO precisa ficar fora da instalacao funcional.')
    sdk = workspace / 'ps3recomp'
    generated = workspace / 'port-140-windows-generated'
    elf = workspace / 'work/v140/EBOOT.elf'
    if not elf.is_file() or sha(elf) != ELF_SHA:
        raise RuntimeError('A preparacao existente nao tem o ELF USA 1.40 suportado.')
    lock = json.loads((source / 'patches/SDK-WINDOWS.lock').read_text())
    existing_lock = json.loads((workspace / 'patches/SDK-WINDOWS.lock').read_text())
    if existing_lock != lock:
        raise RuntimeError('O SDK existente difere do SDK suportado pelo teste ISO.')
    if not (include / 'nlohmann/json.hpp').is_file():
        raise RuntimeError('Cabecalho C++ incorporado ausente.')
    identity = dict(source=source_hash(source), workspace=str(workspace), elf=ELF_SHA,
                    builder=sha(source / 'tools/build_iso_preview.py'),
                    generation=sha(generated / 'windows-generation.json'), toolchain=env.get('D2_TOOLCHAIN_ID'))
    receipt = preview / 'iso-runtime.json'
    executable = preview / 'build/DisgaeaD2Recomp.exe'
    if receipt.is_file():
        saved = json.loads(receipt.read_text())
        if saved.get('identity') != identity:
            raise RuntimeError('A pasta experimental pertence a outra build. Seus arquivos foram preservados.')
        if saved.get('exe_sha256') and executable.is_file() and sha(executable) == saved['exe_sha256']:
            return executable
    elif preview.exists():
        raise RuntimeError('Pasta experimental sem identificacao; nada foi sobrescrito.')
    # Validate the original SDK without modifying its index or source files.
    with tempfile.TemporaryDirectory() as temporary:
        sdk_env = dict(env, GIT_INDEX_FILE=str(Path(temporary) / 'verify.index'))
        run(['git', '-C', sdk, 'read-tree', lock['base']], sdk_env)
        run(['git', '-C', sdk, 'add', '-A'], sdk_env)
        tree = subprocess.check_output(['git', '-C', str(sdk), 'write-tree'], env=sdk_env, text=True).strip()
        if tree != lock['tree']: raise RuntimeError('SDK modificado; a instalacao funcional foi preservada.')
    verify_generation(generated, lock['tree'])
    preview.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps({'identity': identity}), encoding='utf-8')
    shutil.copytree(source / 'port', preview / 'port', dirs_exist_ok=True)
    shutil.copytree(include, preview / 'include', dirs_exist_ok=True)
    compiler_options = [
        '-DCMAKE_C_COMPILER=' + env['D2_PORTABLE_COMPILER'],
        '-DCMAKE_CXX_COMPILER=' + env['D2_PORTABLE_CXX'],
        '-DCMAKE_RC_COMPILER=' + env['D2_PORTABLE_RC'],
        '-DCMAKE_C_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
        '-DCMAKE_CXX_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
        '-DCMAKE_EXE_LINKER_FLAGS=-static -ldxguid -ld3d12 -ldxgi -ld3dcompiler -luuid -lpthread -lxinput9_1_0']
    run(['cmake', '-S', preview / 'port', '-B', preview / 'build', '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Release', *compiler_options, '-DBUILD_TESTING=OFF',
         '-DD2_GAME_VERSION=140', '-DD2_ENABLE_ISO_VFS=ON', '-DD2_SPU_SIMD_SSE41=ON',
         '-DPS3RECOMP_DIR=' + sdk.as_posix(), '-DPython3_EXECUTABLE=' + sys.executable,
         '-DD2_JSON_INCLUDE_DIR=' + (preview / 'include').as_posix(),
         '-DRECOMP_DIR=' + (generated / 'src/recomp-140').as_posix(),
         '-DD2_OUT_DIR=' + (generated / 'out-140').as_posix(),
         '-DSPU_DIR=' + (generated / 'spu-140').as_posix()], env)
    run(['cmake', '--build', preview / 'build', '--parallel', '4'], env)
    if not executable.is_file(): raise RuntimeError('Compilacao ISO terminou sem executavel.')
    receipt.write_text(json.dumps({'identity': identity, 'exe_sha256': sha(executable)}), encoding='utf-8')
    return executable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'workspace', 'preview', 'json-include'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    def run(command, env): subprocess.run(list(map(str, command)), env=env, check=True)
    executable = build(args.source, args.workspace, args.preview, args.json_include, run, os.environ)
    print('ISO runtime ready:', executable)


if __name__ == '__main__': main()
