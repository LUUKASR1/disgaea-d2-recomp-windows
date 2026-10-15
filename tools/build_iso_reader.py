"""Build a source-only ISO reader in a private cache; no SDK/game input."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def build(source, output, env):
    source, output = Path(source).resolve(), Path(output).resolve()
    identity = hashlib.sha256()
    for path in sorted(source.rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:
            identity.update(path.relative_to(source).as_posix().encode())
            identity.update(path.read_bytes())
    identity.update(env['D2_TOOLCHAIN_ID'].encode())
    output = output / identity.hexdigest()[:24]
    exe = output / 'build/d2_iso_probe.exe'
    receipt = output / 'reader.json'
    if exe.is_file() and receipt.is_file():
        saved = json.loads(receipt.read_text())
        if hashlib.sha256(exe.read_bytes()).hexdigest() == saved.get('sha256'): return exe
    output.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, output / 'source', dirs_exist_ok=True)
    subprocess.run(['cmake', '-S', str(output / 'source'), '-B', str(output / 'build'), '-G', 'Ninja',
        '-DBUILD_TESTING=OFF', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_C_COMPILER=' + env['D2_PORTABLE_COMPILER'],
        '-DCMAKE_CXX_COMPILER=' + env['D2_PORTABLE_CXX'],
        '-DCMAKE_C_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
        '-DCMAKE_CXX_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
        '-DCMAKE_EXE_LINKER_FLAGS=-static'], env=env, check=True)
    subprocess.run(['cmake', '--build', str(output / 'build'), '--target', 'd2_iso_probe', '--parallel', '4'],
                   env=env, check=True)
    receipt.write_text(json.dumps({'sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}))
    return exe


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--result', required=True, type=Path)
    args = parser.parse_args()
    exe = build(args.source, args.output, os.environ)
    args.result.write_text(str(exe), encoding='utf-8')
