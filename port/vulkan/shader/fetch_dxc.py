"""Fetch checksum-pinned DXC development inputs; not an end-user installer."""
import hashlib
import io
from pathlib import Path
import sys
import tarfile
import urllib.request
import zipfile

root = Path(sys.argv[1]).resolve()
platform = sys.argv[2]
name, digest = {
    'windows': ('dxc_2025_07_14.zip', '9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4'),
    'linux': ('linux_dxc_2025_07_14.x86_64.tar.gz', 'f2213da1fc99dc8778c8823078e16ba97c7f80f86a1d4520ab1adf4b462bc48c'),
}[platform]
root.mkdir(parents=True, exist_ok=True)
archive = root / name
if not archive.exists():
    data = urllib.request.urlopen('https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/' + name, timeout=120).read()
    if hashlib.sha256(data).hexdigest() != digest:
        raise RuntimeError('DXC archive checksum mismatch')
    archive.write_bytes(data)
data = archive.read_bytes()
if hashlib.sha256(data).hexdigest() != digest:
    raise RuntimeError('DXC cache checksum mismatch')
files = {}
if platform == 'windows':
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        files = {'include/dxcapi.h': z.read('inc/dxcapi.h'), 'dxcompiler.dll': z.read('bin/x64/dxcompiler.dll')}
        files.update({n: z.read(n) for n in ('LICENSE-MS.txt','LICENSE-LLVM.txt','LICENSE-MIT.txt')})
else:
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as z:
        for n in ('include/dxc/dxcapi.h','include/dxc/WinAdapter.h','lib/libdxcompiler.so','LICENSE-MS.txt','LICENSE-LLVM.txt'):
            files[n.replace('include/dxc/', 'include/').replace('lib/', '')] = z.extractfile('./' + n).read()
for relative, content in files.items():
    target = root / platform / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(target.suffix + '.tmp')
    temporary.write_bytes(content)
    temporary.replace(target)
print('Verified DXC v1.8.2505.1:', root / platform)
