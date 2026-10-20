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


def adapt_android(name, data):
    text = data.decode()
    def replace(old, new, count=1):
        nonlocal text
        if text.count(old) != count:
            raise RuntimeError('SDK Android adaptation anchor changed: ' + name)
        text = text.replace(old, new)
    if name == 'runtime/platform/guest_poll.c':
        anchor = 'static void worker_key_init(void) { pthread_key_create(&s_worker_key, worker_finished); }'
        replace(anchor, anchor + '''
#ifdef __ANDROID__
/* Bionic has no pthread cancellation. Only registered guest workers stop,
 * at an explicit poll boundary with no bucket/registry mutex held. Existing
 * poll waits are bounded; other HLE blocking paths are outside this probe. */
static void d2_android_guest_stop_point(void)
{
    pthread_once(&s_worker_once, worker_key_init);
    if (!pthread_getspecific(s_worker_key)) return;
    pthread_mutex_lock(&s_workers_lock);
    int stopping = s_workers_stopping;
    pthread_mutex_unlock(&s_workers_lock);
    if (stopping) pthread_exit(NULL);
}
#endif
''')
        replace('if (stopping) pthread_cancel(pthread_self());', '''if (stopping) {
#ifdef __ANDROID__
        pthread_exit(NULL);
#else
        pthread_cancel(pthread_self());
#endif
    }''')
        replace('if (!w->done) { w->selected = 1; pthread_cancel(w->thread); }', '''if (!w->done) {
            w->selected = 1;
#ifndef __ANDROID__
            pthread_cancel(w->thread);
#endif
        }''')
        replace('    pthread_testcancel();', '''#ifdef __ANDROID__
    d2_android_guest_stop_point();
#else
    pthread_testcancel();
#endif''', count=3)
    elif name == 'runtime/platform/win32_compat.c':
        replace('    pthread_setaffinity_np(pt, sizeof set, &set);', '''#ifdef __ANDROID__
    pid_t tid = pthread_gettid_np(pt);
    if (tid > 0) sched_setaffinity(tid, sizeof set, &set);
#else
    pthread_setaffinity_np(pt, sizeof set, &set);
#endif''')
    return text.encode()


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
    adapted = {name: adapt_android(name, data) for name, data in files.items()}
    output.mkdir(parents=True)
    for name, data in adapted.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    receipt = {'schema': 1, 'sdk_tree': lock['tree'], 'game_sources_included': False,
               'android_adaptations': ['cooperative-guest-poll-stop', 'tid-based-affinity'],
               'original_files': {p: hashlib.sha256(b).hexdigest() for p, b in sorted(files.items())},
               'files': {p: hashlib.sha256(b).hexdigest() for p, b in sorted(adapted.items())}}
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
