"""Windows GPU/WARP and audited renderer compile checks; no game input."""
import os
from pathlib import Path
import subprocess
import sys
from check_iso_vfs import download

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'windows'))
from portable_tools import ensure_tool


if __name__ == '__main__':
    llvm = ensure_tool(ROOT / 'ci-tools', 'llvm', download, print)
    env = {key: value for key, value in os.environ.items() if key.upper() not in
           ('PATH', 'LIB', 'INCLUDE', 'LIBPATH', 'CC', 'CXX', 'RC', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS')}
    env['PATH'] = str(llvm / 'bin') + os.pathsep + os.environ.get('PATH', os.environ.get('Path', ''))
    def run(*args): subprocess.run(list(map(str, args)), env=env, check=True)
    sdk = ROOT / 'ps3recomp'
    run(sys.executable, ROOT / 'tools/bootstrap_windows.py', sdk)
    build = ROOT / 'build-graphics'
    run('cmake', '-S', ROOT / 'port/graphics', '-B', build, '-G', 'Ninja', '-DBUILD_TESTING=ON',
        '-DCMAKE_BUILD_TYPE=Release', '-DD2_GRAPHICS_SDK_DIR=' + sdk.as_posix(),
        '-DCMAKE_C_COMPILER=' + (llvm / 'bin/x86_64-w64-mingw32-clang.exe').as_posix(),
        '-DCMAKE_CXX_COMPILER=' + (llvm / 'bin/x86_64-w64-mingw32-clang++.exe').as_posix(),
        '-DCMAKE_C_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions',
        '-DCMAKE_CXX_FLAGS=-D_WIN32_WINNT=0x0A00 -fms-extensions', '-DCMAKE_EXE_LINKER_FLAGS=-static')
    run('cmake', '--build', build, '--parallel', '4')
    run('ctest', '--test-dir', build, '--output-on-failure', '-V')
