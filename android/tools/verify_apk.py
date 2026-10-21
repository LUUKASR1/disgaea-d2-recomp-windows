"""Inspect the built package, not source text: ABI, native entry and 16 KiB ELF alignment."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    parser.add_argument('--aapt', required=True)
    parser.add_argument('--apksigner', required=True)
    parser.add_argument('--readelf', required=True)
    args = parser.parse_args()
    subprocess.run([args.apksigner, 'verify', '--verbose', str(args.apk)], check=True)
    badging = subprocess.check_output([args.aapt, 'dump', 'badging', str(args.apk)], text=True)
    assert "name='org.disgaead2.bootstrap'" in badging
    assert "native-code: 'arm64-v8a'" in badging
    assert "launchable-activity: name='org.disgaead2.bootstrap.BootstrapActivity'" in badging
    assert not any(line.startswith('uses-permission:') for line in badging.splitlines())
    with zipfile.ZipFile(args.apk) as archive:
        libs = [p for p in archive.namelist() if p.startswith('lib/') and p.endswith('.so')]
        assert set(libs) == {'lib/arm64-v8a/libd2_android_bootstrap.so',
                             'lib/arm64-v8a/libd2_android_udfread.so'}, libs
        assert 'assets/libudfread-COPYING.txt' in archive.namelist()
        for name in libs:
            data = archive.read(name)
            assert data[:6] == b'\x7fELF\x02\x01'
            assert struct.unpack_from('<H', data, 18)[0] == 183
            phoff = struct.unpack_from('<Q', data, 32)[0]
            phsize, phcount = struct.unpack_from('<HH', data, 54)
            load_count = 0
            for i in range(phcount):
                header = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize)
                if header[0] == 1:
                    load_count += 1
                    assert header[7] >= 16384, (name, header)
                    assert header[2] % 16384 == header[3] % 16384, (name, header)
            assert load_count
        with tempfile.TemporaryDirectory() as temporary:
            library = Path(temporary) / 'bootstrap.so'
            library.write_bytes(archive.read('lib/arm64-v8a/libd2_android_bootstrap.so'))
            symbols = subprocess.check_output([args.readelf, '--dyn-syms', str(library)], text=True)
            assert any('ANativeActivity_onCreate' in line and 'GLOBAL' in line and 'UND' not in line
                       for line in symbols.splitlines()), 'NativeActivity entry is not exported'
        forbidden = ('.iso', '.pkg', '.elf', '.dat', '.rap', '.rif')
        assert not any(p.lower().endswith(forbidden) for p in archive.namelist())
    print('PASS: signed diagnostic APK; AArch64 NativeActivity entry; 16 KiB load alignment; no permissions or game files')
    print('SHA256 ' + hashlib.sha256(args.apk.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
