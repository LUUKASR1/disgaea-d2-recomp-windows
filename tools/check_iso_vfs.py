#!/usr/bin/env python3
"""Build and test ISO VFS on CI; Windows uses our verified portable compiler."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def download(url, destination, expected):
    digest = hashlib.sha256()
    req = urllib.request.Request(url, headers={'User-Agent': 'DisgaeaD2-IsoVfs-CI'})
    with urllib.request.urlopen(req, timeout=90) as response, destination.open('wb') as output:
        while chunk := response.read(1024 * 1024):
            digest.update(chunk)
            output.write(chunk)
    if digest.hexdigest() != expected:
        destination.unlink(missing_ok=True)
        raise RuntimeError('Portable compiler checksum mismatch')


def run(*args, env=None):
    subprocess.run(list(map(str, args)), check=True, env=env)


def main():
    sdk = ROOT / 'ps3recomp'
    run(sys.executable, ROOT / 'tools/bootstrap_windows.py', sdk)
    build = ROOT / 'build-iso-vfs'
    options = ['-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=Release', '-DD2_ISO_SDK_DIR=' + sdk.as_posix()]
    env = os.environ.copy()
    if os.name == 'nt':
        sys.path.insert(0, str(ROOT / 'windows'))
        from portable_tools import ensure_tool
        llvm = ensure_tool(ROOT / 'ci-tools', 'llvm', download, print)
        env = {k: v for k, v in env.items() if k.upper() not in
               ('PATH', 'LIB', 'INCLUDE', 'LIBPATH', 'CC', 'CXX', 'RC', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS')}
        env['PATH'] = str(llvm / 'bin') + os.pathsep + os.environ.get('PATH', os.environ.get('Path', ''))
        options += ['-G', 'Ninja',
                    '-DCMAKE_C_COMPILER=' + (llvm / 'bin/x86_64-w64-mingw32-clang.exe').as_posix(),
                    '-DCMAKE_CXX_COMPILER=' + (llvm / 'bin/x86_64-w64-mingw32-clang++.exe').as_posix(),
                    '-DCMAKE_C_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
                    '-DCMAKE_CXX_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
                    '-DCMAKE_EXE_LINKER_FLAGS=-static']
    run('cmake', '-S', ROOT / 'port/vfs', '-B', build, *options, env=env)
    run('cmake', '--build', build, '--parallel', '4', env=env)
    run('ctest', '--test-dir', build, '--output-on-failure', '-V', env=env)
    if os.name == 'nt':
        output = ROOT / 'iso-probe-windows'
        output.mkdir(exist_ok=True)
        shutil.copy2(build / 'd2_iso_probe.exe', output / 'DisgaeaD2-ISO-Check.exe')
        shutil.copy2(ROOT / 'docs/ISO-VFS.md', output / 'README.md')
        shutil.copy2(ROOT / 'port/vfs/vendor/libudfread/COPYING', output / 'libudfread-LGPL-2.1.txt')
        shutil.copy2(ROOT / 'port/vfs/vendor/libudfread/PROVENANCE.md', output / 'libudfread-PROVENANCE.md')
        # Preserve object files and link response so users can relink the LGPL
        # reader; no game code or data exists in this diagnostic executable.
        for target in ('d2_iso_probe', 'd2_iso_vfs', 'd2_udfread'):
            source = build / 'CMakeFiles' / (target + '.dir')
            if source.exists(): shutil.copytree(source, output / 'relink' / source.name, dirs_exist_ok=True)
        for source in build.glob('*.a'): shutil.copy2(source, output)
        imports = subprocess.check_output([str(llvm / 'bin/llvm-readobj.exe'), '--coff-imports',
                                          str(output / 'DisgaeaD2-ISO-Check.exe')], text=True)
        (output / 'imports.txt').write_text(imports, encoding='utf-8')
        if any(name in imports.lower() for name in ('libc++', 'libwinpthread', 'msvcp140', 'vcruntime140')):
            raise RuntimeError('Probe unexpectedly imports an external C++ runtime')
        print('Windows ISO checker is standalone; only Windows system DLL imports.')


if __name__ == '__main__':
    main()
