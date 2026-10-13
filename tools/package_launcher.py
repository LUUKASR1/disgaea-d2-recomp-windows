"""Build a Windows launcher without including game-derived files."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

onefile = '--onefile' in sys.argv
package_name = 'DisgaeaD2Launcher-Capture-Preview' if onefile else 'DisgaeaD2Launcher'

root = Path(__file__).resolve().parents[1]
staging = root / 'launcher-package'
if staging.exists():
    shutil.rmtree(staging)
payload = staging / 'payload'
payload.mkdir(parents=True)
# Explicit source-only allowlist, independent of untracked local game files.
for directory in ('tools', 'port', 'patches', 'windows'):
    files = subprocess.check_output(['git', '-C', str(root), 'ls-files', '--', directory], text=True).splitlines()
    for name in files:
        if name.endswith(('.elf', '.bin', '.exe', '.dat', '.pkg')):
            raise RuntimeError('Game/binary input cannot be packaged: ' + name)
        source = root / name
        destination = payload / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Read committed bytes, preserving patch hashes across Windows CRLF checkout.
        destination.write_bytes(subprocess.check_output(['git', '-C', str(root), 'show', 'HEAD:' + name]))
for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
    if (root / name).is_file():
        shutil.copy2(root / name, payload / name)
vendor = staging / 'vendor'
(vendor / 'bin').mkdir(parents=True)
if onefile:
    (vendor / 'standalone.json').write_text(json.dumps({'version': 1}), encoding='utf-8')
    (vendor / 'graphics.json').write_text(json.dumps({'version': 1}), encoding='utf-8')
for name in ('cmake', 'ninja'):
    tool = shutil.which(name)
    if not tool:
        raise RuntimeError('Missing packaging tool: ' + name)
    if name == 'cmake':
        import cmake
        shutil.copytree(Path(cmake.CMAKE_BIN_DIR).parent, vendor / 'cmake')
        # CMake locates its share data relative to its binary.
    else:
        import ninja
        shutil.copy2(Path(ninja.BIN_DIR) / 'ninja.exe', vendor / 'bin/ninja.exe')
url = 'https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp'
data = urllib.request.urlopen(url, timeout=60).read()
if hashlib.sha256(data).hexdigest() != '9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6':
    raise RuntimeError('nlohmann/json header checksum mismatch')
header = vendor / 'include/nlohmann/json.hpp'
header.parent.mkdir(parents=True)
header.write_bytes(data)
(vendor / 'JSON-LICENSE.MIT').write_bytes(urllib.request.urlopen('https://raw.githubusercontent.com/nlohmann/json/v3.11.3/LICENSE.MIT', timeout=60).read())
(vendor / 'json-sha256.txt').write_text(hashlib.sha256(data).hexdigest() + '\n')
subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean',
                '--onefile' if onefile else '--onedir', '--windowed', '--name', package_name,
                '--icon', str(root / 'windows/assets/launcher.ico'), '--collect-all', 'capstone',
                '--collect-all', 'construct', '--collect-all', 'Crypto', '--collect-all', 'tabulate',
                '--collect-all', 'tqdm', '--collect-all', 'py7zr', '--add-data', str(payload) + ';payload',
                '--add-data', str(root / 'windows') + ';windows', '--add-data', str(vendor) + ';vendor',
                str(root / 'windows/launcher.py')], cwd=root, check=True)
print('Launcher ready:', package_name, '. No game executable was packaged.')
