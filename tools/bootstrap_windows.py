#!/usr/bin/env python3
"""Fetch the pinned Windows SDK and apply the verified consolidated patch."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def run(*command):
    subprocess.run(list(map(str, command)), check=True)


def bootstrap(destination):
    root = Path(__file__).resolve().parents[1]
    lock = json.loads((root / 'patches/SDK-WINDOWS.lock').read_text())
    patch = root / 'patches' / lock['patch']
    if hashlib.sha256(patch.read_bytes()).hexdigest() != lock['patch_sha256']:
        raise RuntimeError('Windows SDK patch checksum mismatch.')
    if destination.exists():
        raise RuntimeError('SDK destination already exists; choose a new directory. Nothing was overwritten.')
    run('git', 'clone', '-c', 'core.autocrlf=false', '--no-checkout', lock['repository'], destination)
    run('git', '-C', destination, 'checkout', '--detach', lock['base'])
    run('git', '-C', destination, 'apply', '--check', '--index', patch)
    run('git', '-C', destination, 'apply', '--index', patch)
    tree = subprocess.check_output(['git', '-C', str(destination), 'write-tree'], text=True).strip()
    if tree != lock['tree']:
        raise RuntimeError('Patched Windows SDK tree mismatch. Do not build this checkout.')
    print('Windows SDK verified:', tree)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', nargs='?', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'ps3recomp')
    args = parser.parse_args()
    bootstrap(args.destination.resolve())
