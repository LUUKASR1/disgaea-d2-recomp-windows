#!/usr/bin/env python3
"""Build the recompiled game core (libd2core.so) for Android ARM64 from the user's own generated lift.

Runs on the PC (the Windows launcher's tools, or Linux CI). Nothing from the game is distributed: the input is
the C/C++ the lifter generated locally from the user's ISO/ELF, the output stays on the user's computer.
Needs cmake and ninja on PATH and git (for SDL2). The Android NDK is downloaded once (pinned, SHA-1 checked)."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile

NDK_VERSION = '28.2.13676358'
NDK_FOLDER = 'android-ndk-r28c'
NDK_WINDOWS = ('https://dl.google.com/android/repository/android-ndk-r28c-windows.zip',
               '086bba43ff2f5eb0e387b15c8278bb4e0d89ba1d')
SDL_TAG = 'release-2.30.9'
ROOT = Path(__file__).resolve().parents[1]


def run(command, cwd=None, env=None):
    print('+', ' '.join(map(str, command)), flush=True)
    # stdout/stderr are passed explicitly: the windowed launcher has no usable standard handles to inherit.
    subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True, stdout=sys.stdout, stderr=sys.stderr)


def sha1_of(path):
    digest = hashlib.sha1()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def ensure_ndk(work, given):
    if given:
        ndk = Path(given)
        if not (ndk / 'build/cmake/android.toolchain.cmake').is_file():
            raise SystemExit('NDK invalido: ' + str(ndk))
        return ndk
    ndk = work / 'ndk' / NDK_FOLDER
    if (ndk / 'build/cmake/android.toolchain.cmake').is_file():
        return ndk
    if sys.platform != 'win32':
        raise SystemExit('Passe --ndk com um NDK ' + NDK_VERSION + ' neste sistema.')
    url, sha1 = NDK_WINDOWS
    archive = work / 'ndk' / 'ndk.zip'
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.is_file() or sha1_of(archive) != sha1:
        print('Baixando o Android NDK r28c (cerca de 750 MB, somente uma vez)...', flush=True)
        part = archive.with_suffix('.part')
        with urllib.request.urlopen(url, timeout=120) as response, part.open('wb') as out:
            shutil.copyfileobj(response, out, 1 << 20)
        if sha1_of(part) != sha1:
            part.unlink(missing_ok=True)
            raise SystemExit('O NDK baixado falhou na verificacao SHA-1.')
        part.replace(archive)
    print('Extraindo o NDK...', flush=True)
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            name = entry.filename.replace('\\', '/')
            if name.startswith('/') or '..' in name.split('/'):
                raise SystemExit('Caminho inseguro no NDK: ' + name)
        package.extractall(archive.parent)
    archive.unlink(missing_ok=True)
    return ndk


def read_text(path):
    with open(path, encoding='utf-8', newline='') as handle:
        return handle.read()


def write_text(path, text):
    with open(path, 'w', encoding='utf-8', newline='') as handle:
        handle.write(text)


def prepare_sdk(source, destination):
    """Copy the (already project-patched) ps3recomp checkout and make it buildable for ARM64 Android."""
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns('.git', 'build*', '_probe'))
    cmake = destination / 'CMakeLists.txt'
    text = read_text(cmake)
    # SSE4.1 is an x86 flag; keep it for x86-64 only.
    text, count = re.subn(r'(?m)^( *)-msse4\.1(\r?)$',
                          r'\1$<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},x86_64>:-msse4.1>\2', text)
    if not count and 'PS3RECOMP_HAS_MSSE41' not in text:
        raise SystemExit('SDK mudou: opcao -msse4.1 nao encontrada.')   # the project patch already tests for it
    text += ('\nif(ANDROID)\n'
             '    target_sources(ps3recomp_runtime PRIVATE libs/spurs/d2_android_ucontext.c)\n'
             '    target_compile_options(ps3recomp_runtime PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/libs/spurs/d2_android_ucontext.h)\n'
             'endif()\n')
    write_text(cmake, text)
    # bionic has no thread affinity call: leave placement to the Android scheduler.
    affinity = destination / 'runtime/platform/win32_compat.c'
    body = read_text(affinity)
    if 'pthread_setaffinity_np(pt, sizeof set, &set);' not in body:
        raise SystemExit('SDK mudou: pthread_setaffinity_np nao encontrado.')
    write_text(affinity, body.replace('pthread_setaffinity_np(pt, sizeof set, &set);', '(void)pt; (void)set;'))
    for name in ('d2_android_ucontext.c', 'd2_android_ucontext.h'):
        shutil.copyfile(ROOT / 'port/android' / name, destination / 'libs/spurs' / name)


def toolchain_args(ndk):
    return ['-G', 'Ninja', '-DCMAKE_TOOLCHAIN_FILE=' + str(ndk / 'build/cmake/android.toolchain.cmake'),
            '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-29', '-DCMAKE_BUILD_TYPE=Release']


def build_sdl(work, ndk):
    install = work / 'sdl-install'
    if (install / 'lib/cmake/SDL2').is_dir():
        return install
    source = work / 'SDL'
    if not source.is_dir():
        run(['git', 'clone', '-q', '--depth', '1', '--branch', SDL_TAG, 'https://github.com/libsdl-org/SDL.git', source])
    run(['cmake', '-S', source, '-B', work / 'sdl-build', *toolchain_args(ndk), '-DSDL_SHARED=OFF', '-DSDL_STATIC=ON',
         '-DSDL_TEST=OFF', '-DCMAKE_INSTALL_PREFIX=' + str(install)])
    run(['cmake', '--build', work / 'sdl-build'])
    run(['cmake', '--install', work / 'sdl-build'])
    return install


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True, help='project-patched ps3recomp checkout')
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--ndk', type=Path)
    parser.add_argument('--runtime-only', action='store_true', help='CI: build only the ps3recomp runtime library')
    parser.add_argument('--recomp-dir', type=Path)
    parser.add_argument('--out-dir', type=Path, help='loader metadata (out-140 / out-eu)')
    parser.add_argument('--spu-dir', type=Path)
    parser.add_argument('--game-version', default='140')
    parser.add_argument('--json-include', type=Path)
    parser.add_argument('--output', type=Path, help='where libd2core.so is copied')
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    ndk = ensure_ndk(work, args.ndk)
    sdk = work / 'sdk-android'
    prepare_sdk(args.sdk.resolve(), sdk)
    sdl = build_sdl(work, ndk)
    prefix = ['-DCMAKE_PREFIX_PATH=' + str(sdl), '-DSDL2_DIR=' + str(sdl / 'lib/cmake/SDL2')]
    if args.runtime_only:
        build = work / 'runtime-build'
        run(['cmake', '-S', sdk, '-B', build, *toolchain_args(ndk), '-DBUILD_TESTING=OFF', *prefix])
        run(['cmake', '--build', build, '--target', 'ps3recomp_runtime'])
        print('Runtime ARM64 pronto:', build / 'libps3recomp_runtime.a')
        return
    for name in ('recomp_dir', 'out_dir', 'spu_dir', 'json_include', 'output'):
        if getattr(args, name) is None:
            parser.error('--' + name.replace('_', '-') + ' e obrigatorio sem --runtime-only')
    build = work / 'core-build'
    # The launcher unpacks its sources into a new temporary folder on every run; CMake refuses a build
    # directory configured from another source path, so start clean when the path changed.
    cache = build / 'CMakeCache.txt'
    if cache.is_file():
        home = re.search(r'(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$', cache.read_text(encoding='utf-8', errors='replace'))
        if not home or Path(home.group(1)).resolve() != (ROOT / 'port').resolve():
            shutil.rmtree(build)
    command = ['cmake', '-S', ROOT / 'port', '-B', build, *toolchain_args(ndk), *prefix,
               '-DD2_GAME_VERSION=' + args.game_version, '-DPS3RECOMP_DIR=' + str(sdk),
               '-DRECOMP_DIR=' + str(args.recomp_dir.resolve()), '-DD2_OUT_DIR=' + str(args.out_dir.resolve()),
               '-DSPU_DIR=' + str(args.spu_dir.resolve()), '-DD2_JSON_INCLUDE_DIR=' + str(args.json_include.resolve()),
               '-DD2_ENABLE_ISO_VFS=OFF', '-DD2_SPU_SIMD_SSE41=OFF', '-DBUILD_TESTING=OFF',
               '-DPython3_EXECUTABLE=' + sys.executable]
    run(command)
    run(['cmake', '--build', build])
    library = next(build.glob('libd2core.so'), None)
    if not library:
        raise SystemExit('A compilacao terminou sem libd2core.so.')
    args.output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(library, args.output / 'libd2core.so')
    print('Biblioteca Android pronta:', args.output / 'libd2core.so')


if __name__ == '__main__':
    main()
