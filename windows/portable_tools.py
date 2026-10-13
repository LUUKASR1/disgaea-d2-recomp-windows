"""Private, pinned Windows tools. No system installation or global PATH edits."""
import json
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
        '', ('cmd/git.exe', 'mingw64/bin/git.exe')),
}


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
    paths = [llvm / 'bin', git / 'cmd', git / 'mingw64/bin',
             Path(bundled) / 'vendor/bin', Path(bundled) / 'vendor/cmake/bin']
    env['PATH'] = os.pathsep.join(map(str, paths)) + os.pathsep + os.environ.get('PATH', os.environ.get('Path', ''))
    env['D2_TOOLCHAIN_ID'] = TOOLCHAIN_ID
    env['D2_PORTABLE_COMPILER'] = str(llvm / 'bin/x86_64-w64-mingw32-clang.exe')
    env['D2_PORTABLE_CXX'] = str(llvm / 'bin/x86_64-w64-mingw32-clang++.exe')
    env['D2_PORTABLE_RC'] = str(llvm / 'bin/x86_64-w64-mingw32-windres.exe')
    for name in ('git', 'cmake', 'ninja'):
        if not shutil.which(name, path=env['PATH']):
            raise RuntimeError('Ferramenta portátil ausente: ' + name)
    return env
