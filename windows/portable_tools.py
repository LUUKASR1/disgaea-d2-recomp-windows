"""Private, pinned Windows tools. No system installation or global PATH edits."""
import json
import hashlib
import os
from pathlib import Path
import shutil
import uuid
import zipfile

TOOLCHAIN_ID = 'llvm-mingw-20260616-ucrt-ms-extensions-static-v1'
TOOLS = {
    'llvm': (
        'https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip',
        'b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35',
        'llvm-mingw-20260616-ucrt-x86_64',
        ('bin/x86_64-w64-mingw32-clang.exe', 'bin/x86_64-w64-mingw32-clang++.exe',
         'bin/x86_64-w64-mingw32-windres.exe', 'include/windows.h')),
    'git': (
        'https://github.com/git-for-windows/git/releases/download/v2.56.0.windows.2/MinGit-2.56.0.2-64-bit.zip',
        'da35e72aa21c005a5a0d298cfbae110bc1609a815730ea0dde84b01a1b3cd3be',
        '', ('cmd/git.exe', 'ucrt64/bin/git.exe')),
}

CRT_FILES = {'msvcp140_1.dll': '4981865aeba0e8c5d8a7b81fa73c50e919da670c9deca0a01490084fc9ed412a', 'msvcp140_codecvt_ids.dll': '5ffc1ec682d5867f4e6047806c2e655bfb19d441187c0bfeface0ae4f54bf8cf', 'vcruntime140_1.dll': 'f8b039fdb1a0249a9db7f399d1730bf94a97836e37035d615070c91c6f9e4577', 'msvcp140_2.dll': '4d4cd5e1e34fd4c3dfff7a7f5498b09e8e43b4fa8795c789ae59003dfad8c270', 'vcruntime140_threads.dll': '5550e13c894e4b879cab0d9385f1c183a84d09585165dc18d2142995e53ee2aa', 'msvcp140_atomic_wait.dll': 'cfe8323ac03156930be2a894826b918efbb47dfe78991e93287a6993a841bc8f', 'vccorlib140.dll': '3c059c5f0aec7e6b580acfee2b8cc6ddcbe9adab95997eea4f9b3c7bc89412a1', 'vcruntime140.dll': 'aabdd441c96b7928184ab5623245624be576abce1a2c35a7d96dcedbcffd33f3', 'msvcp140.dll': '4a25710800a9e86464369d52e6e0e044533fc0dd12e3e1c35283f095cf9a65b1', 'concrt140.dll': '187d0ad3f3efd61e6dd1da5ebf3c9111d4c149421db8af1211d712a7af3ddf46'}
TOOLS['crt'] = ('https://download.visualstudio.microsoft.com/download/pr/fedfadc0-2ddc-42f8-afa2-24eda4792a5f/aafe93c23baefa98078ce3406b43defdb73979d249a598583de127e229595f75/Microsoft.VC.14.52.CRT.Redist.X64.base.vsix', 'aafe93c23baefa98078ce3406b43defdb73979d249a598583de127e229595f75', 'Contents/VC/Redist/MSVC/14.52.36725/x64/Microsoft.VC145.CRT', tuple(CRT_FILES))


def extract_checked(archive, staging):
    """Reject traversal, Windows path aliases and symlinks before extraction."""
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            name = entry.filename.replace('\\', '/')
            parts = name.rstrip('/').split('/')
            if (name.startswith('/') or any(p in ('', '.', '..') or ':' in p or
                    p.endswith((' ', '.')) for p in parts) or
                    (entry.external_attr >> 16) & 0o170000 == 0o120000):
                raise RuntimeError('Caminho inseguro no pacote de ferramentas: ' + name)
        package.extractall(staging)


def ensure_tool(home, name, download, notify):
    url, sha, prefix, required = TOOLS[name]
    root = Path(home) / 'ferramentas-portateis'
    target = root / (name + '-' + sha[:12])
    receipt = target / 'download.json'
    try:
        valid = json.loads(receipt.read_text()) == {'sha256': sha}
    except (OSError, ValueError):
        valid = False
    if valid and all((target / p).is_file() and (target / p).stat().st_size for p in required):
        return target
    root.mkdir(parents=True, exist_ok=True)
    archive = root / (sha + '.zip')
    notify('Baixando ferramentas portáteis: ' + name + '. Somente na primeira preparação.')
    download(url, archive, sha)
    staging = root / ('extraindo-' + uuid.uuid4().hex)
    staging.mkdir()
    try:
        extract_checked(archive, staging)
        source = staging / prefix if prefix else staging
        missing = [p for p in required if not (source / p).is_file() or not (source / p).stat().st_size]
        if missing:
            raise RuntimeError('Pacote de ferramentas incompleto: ' + name + ': ' + ', '.join(missing))
        (source / 'download.json').write_text(json.dumps({'sha256': sha}))
        if target.exists():
            target.rename(root / ('anterior-' + uuid.uuid4().hex))
        source.rename(target)
        return target
    finally:
        shutil.rmtree(staging, ignore_errors=True)
        archive.unlink(missing_ok=True)


def portable_environment(home, bundled, download, notify):
    llvm = ensure_tool(home, 'llvm', download, notify)
    git = ensure_tool(home, 'git', download, notify)
    env = {k: v for k, v in os.environ.items() if k.upper() not in
           ('PATH', 'LIB', 'INCLUDE', 'LIBPATH', 'CC', 'CXX', 'RC', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS')}
    paths = [llvm / 'bin', git / 'cmd', git / 'ucrt64/bin',
             Path(bundled) / 'vendor/bin', Path(bundled) / 'vendor/cmake/bin']
    env['PATH'] = os.pathsep.join(map(str, paths)) + os.pathsep + os.environ.get('PATH', os.environ.get('Path', ''))
    env['D2_TOOLCHAIN_ID'] = TOOLCHAIN_ID
    # CMakeRCCompiler.cmake does not normalize backslashes before quoting.
    env['D2_PORTABLE_COMPILER'] = (llvm / 'bin/x86_64-w64-mingw32-clang.exe').as_posix()
    env['D2_PORTABLE_CXX'] = (llvm / 'bin/x86_64-w64-mingw32-clang++.exe').as_posix()
    env['D2_PORTABLE_RC'] = (llvm / 'bin/x86_64-w64-mingw32-windres.exe').as_posix()
    for name in ('git', 'cmake', 'ninja'):
        if not shutil.which(name, path=env['PATH']):
            raise RuntimeError('Ferramenta portátil ausente: ' + name)
    return env


def ensure_helper_runtime(home, directory, download, notify):
    directory = Path(directory)
    def matches(path, sha):
        return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == sha
    if all(matches(directory / name, sha) for name, sha in CRT_FILES.items()):
        return
    source = ensure_tool(home, 'crt', download, notify)
    if not all(matches(source / name, sha) for name, sha in CRT_FILES.items()):
        raise RuntimeError('Bibliotecas locais do auxiliar falharam na verificação SHA-256.')
    notify('Preparando bibliotecas Microsoft no auxiliar local, sem instalar ferramentas no Windows.')
    for name, sha in CRT_FILES.items():
        target = directory / name
        if not matches(target, sha):
            part = target.with_suffix('.dll.part')
            shutil.copy2(source / name, part)
            part.replace(target)
