"""Materialize SDK core sources from the project's exact patched Git tree.

No generated game sources are read. Dirty SDK worktrees are never modified or
used as source bytes: git show reads the immutable tree fixed by the lock.
"""
import argparse
import hashlib
import json
from pathlib import Path
import posixpath
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SEEDS = (
    'runtime/platform/win32_compat.c', 'runtime/platform/guest_poll.c',
    'runtime/ppu/ppu_ops.h', 'runtime/memory/vm.h',
    'runtime/spu/spu_coherency.c',
    'runtime/spu/tests/test_spu_helpers.c',
    'runtime/spu/tests/test_spu_numeric_semantics.c',
)


def prepare(sdk, output):
    lock = json.loads((ROOT / 'patches/SDK-WINDOWS.lock').read_text())
    patch = ROOT / 'patches' / lock['patch']
    assert hashlib.sha256(patch.read_bytes()).hexdigest() == lock['patch_sha256']
    if not sdk.exists():
        sdk.mkdir(parents=True)
        def run(*args):
            subprocess.run(['git', '-C', str(sdk), *args], check=True)
        run('init')
        run('remote', 'add', 'origin', lock['repository'])
        run('fetch', '--depth=1', 'origin', lock['base'])
        run('checkout', '--detach', 'FETCH_HEAD')
        run('apply', '--index', str(patch))
        assert subprocess.check_output(['git', '-C', str(sdk), 'write-tree'], text=True).strip() == lock['tree']
    def git(*args):
        return subprocess.check_output(['git', '-C', str(sdk), *args])
    assert git('cat-file', '-t', lock['tree']).strip() == b'tree'
    available = set(git('ls-tree', '-r', '--name-only', lock['tree']).decode().splitlines())
    pending = list(SEEDS)
    files = {}
    while pending:
        name = pending.pop()
        if name in files:
            continue
        if name not in available or name.startswith('../'):
            raise RuntimeError('Missing pinned SDK source: ' + name)
        data = git('show', lock['tree'] + ':' + name)
        files[name] = data
        for inc in re.findall(rb'^\s*#\s*include\s*"([^"]+)"', data, re.M):
            inc = inc.decode()
            choices = (posixpath.normpath(posixpath.join(posixpath.dirname(name), inc)),
                       'include/' + inc, 'runtime/spu/' + inc, 'runtime/platform/' + inc)
            found = next((p for p in choices if p in available), None)
            if found is None:
                raise RuntimeError('Unresolved pinned include: ' + name + ' -> ' + inc)
            pending.append(found)
    if output.exists():
        raise RuntimeError('Output already exists; use a new directory. Nothing overwritten.')
    output.mkdir(parents=True)
    for name, data in files.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    receipt = {'schema': 1, 'sdk_tree': lock['tree'], 'game_sources_included': False,
               'files': {p: hashlib.sha256(b).hexdigest() for p, b in sorted(files.items())}}
    (output / 'core-provenance.json').write_text(json.dumps(receipt, indent=2) + '\n')
    licenses = output / 'licenses'
    licenses.mkdir()
    for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
        (licenses / (name + '.txt')).write_bytes((ROOT / name).read_bytes())
    print('Pinned SDK core prepared:', lock['tree'], len(files), 'source files')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, default=ROOT / 'android-core-sdk')
    p.add_argument('--output', type=Path, default=ROOT / 'android-runtime-core')
    args = p.parse_args()
    prepare(args.sdk.resolve(), args.output.resolve())
