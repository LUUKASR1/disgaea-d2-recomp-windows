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

CRT_FILES = {'msvcp140_2.dll': '3ea06f0ee098b4823cb79599df3780e7f23cce52c19aac31d2a0d47efe33a5e9', 'vcruntime140_threads.dll': '219915cf20822f34d5e7c1fdd4e21ae7f3396881096c51036225fb8f84b47afa', 'vcruntime140.dll': 'd5e4d9a3e835fa679450145d6a7d94e36573a509317111904d9b3712c30d9066', 'msvcp140.dll': '0f885b509a685d2bbfa652fed26b5fb31d88fbdab0a978c641d1c7b8aa460aa9', 'msvcp140_atomic_wait.dll': '640b2aefced484d0368eea5bdd06addd0658a3a70a49256e560d6923b404a479', 'vcruntime140_1.dll': '1f2d41c4aa5db0bc33ebf7b66d72943a817d7ce6cbe880502a9403823633093f', 'msvcp140_1.dll': 'bfad5aef4c63a669e3c140655cdfdf395b6c979b400a447bd5dcb65ed8826c3d', 'concrt140.dll': '2405355f0a58067b258f8df33c327e3a3d716eaac5a3a5aebb757842d85bd376', 'msvcp140_codecvt_ids.dll': 'f2069a52880ec885ee7f0511186100eb7fada0411a2b4948fafea7735b878a18', 'vccorlib140.dll': '19839407c3fdbc824e5bce189bf68ddf8097f12ec28b757797ffa0415c144ddd'}
TOOLS['crt'] = ('https://download.visualstudio.microsoft.com/download/pr/45d3b8dd-bced-4b37-9974-142f748d710c/4aaf54db0bfc9435f7c3660e1a00237a4b556042bfeea64bde44c2e0194e6ee5/Microsoft.VC.14.44.17.14.CRT.Redist.X64.base.vsix', '4aaf54db0bfc9435f7c3660e1a00237a4b556042bfeea64bde44c2e0194e6ee5', 'Contents/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT', tuple(CRT_FILES))


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
