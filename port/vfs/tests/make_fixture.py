#!/usr/bin/env python3
"""Build small synthetic UDF 1.02 images, containing no game data.

Descriptor layouts follow ECMA-167 (FE, FID, short_ad, long_ad).
The large fixture is sparse and tests physical reads beyond 4 GiB.
"""
import binascii
import ctypes
import os
from pathlib import Path
import struct
import sys

B = 2048
PART = 300


def put(data, offset, fmt, *values):
    struct.pack_into('<' + fmt, data, offset, *values)


def tagged(kind, location, payload, length=None):
    data = bytearray(payload)
    size = len(data) - 16 if length is None else length
    put(data, 0, 'HHBBHHHI', kind, 2, 0, 0, 1,
        binascii.crc_hqx(data[16:16 + size], 0), size, location)
    data[4] = sum(data[:4] + data[5:16]) & 255
    return data


def long_ad(size, lba):
    return struct.pack('<IIH6x', size, lba, 0)


def fid(name, icb, directory, location):
    cs0 = b'\x10' + name.encode('utf-16-be') if any(ord(c) > 127 for c in name) else b'\x08' + name.encode('ascii')
    data = bytearray((38 + len(cs0) + 3) & ~3)
    put(data, 16, 'HBB', 1, 2 if directory else 0, len(cs0))
    data[20:36] = long_ad(B, icb)
    data[38:38 + len(cs0)] = cs0
    return tagged(257, location, data)


def fe(icb, directory, size, ads=(), inline=None, long=False):
    data = bytearray(B)
    put(data, 20, 'H', 4)  # ICB strategy
    data[27] = 4 if directory else 5
    put(data, 34, 'H', 3 if inline is not None else 1 if long else 0)
    put(data, 44, 'I', 0x14a5)  # read/execute permissions
    put(data, 48, 'H', 1)
    put(data, 56, 'Q', size)
    put(data, 64, 'Q', (size + B - 1) // B)
    content = inline if inline is not None else b''.join(
        long_ad(length, block) if long else struct.pack('<II', length, block)
        for length, block in ads)
    put(data, 172, 'I', len(content))
    data[176:176 + len(content)] = content
    return tagged(261, icb, data, 160 + len(content))


def make(path, large=False, complete=False):
    blocks = (0x100000000 // B + 512) if large else 1024
    fragments = bytes((i * 17 + 3) & 255 for i in range(4865))
    # Synthetic metadata, built here; no bytes copied from a game dump.
    sfo = (b'\x00PSF' + struct.pack('<IIII', 0x101, 36, 48, 1) +
           struct.pack('<HHIII', 0, 0x0204, 10, 16, 0) + b'TITLE_ID\0' + bytes(3) + b'BLUS31313\0')
    late = blocks - PART - 300 if large else 180
    nodes = {
        1: ('', True, [('PS3_GAME', 2, True)]),
        2: ('PS3_GAME', True, [('PARAM.SFO', 5, False), ('USRDIR', 3, True)]),
        3: ('USRDIR', True, [('Data', 4, True)]),
        4: ('Data', True, [('fragment.bin', 6, False), ('long.bin', 7, False),
                            ('empty.bin', 8, False), ('Olá.bin', 9, False), ('late.bin', 10, False)]),
    }
    additional = []
    if complete:
        nodes[3][2].append(('EBOOT.BIN', 50, False))
        additional.append(50)
        icb = 51
        for stem in ('START', 'ANM_HI'):
            for part in range(7):
                name = stem + (f'_{part}' if part else '') + '.dat'
                nodes[4][2].append((name, icb, False)); additional.append(icb); icb += 1
        nodes[4][2].extend([('NisAnmShadow.txf', 65, False), ('SOUND', 66, True)])
        nodes[66] = ('SOUND', True, [('SndPakBgm', 67, True)])
        nodes[67] = ('SndPakBgm', True, [('SndPakBgm_0.pak', 68, False)])
        additional += [65, 68]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w+b') as out:
        if os.name == 'nt':
            import msvcrt
            returned = ctypes.c_ulong()
            ok = ctypes.windll.kernel32.DeviceIoControl(
                ctypes.c_void_p(msvcrt.get_osfhandle(out.fileno())), 0x900c4,
                None, 0, None, 0, ctypes.byref(returned), None)
            if large and not ok:
                raise OSError('Unable to mark the >4GiB test image sparse')
        out.truncate(blocks * B)

        def write(lba, data):
            out.seek(lba * B)
            out.write(data)

        for lba, identifier in [(16, b'BEA01'), (17, b'NSR02'), (18, b'TEA01')]:
            data = bytearray(B); data[1:6] = identifier; data[6] = 1; write(lba, data)
        anchor = bytearray(B); put(anchor, 16, 'IIII', 4 * B, 257, 4 * B, 270)
        for lba in [256, blocks - 257, blocks - 1]: write(lba, tagged(2, lba, anchor, 496))
        pvd = bytearray(B); pvd[24:32] = b'\x08D2TEST\0'; pvd[55] = 7
        partition = bytearray(B); put(partition, 22, 'H', 0)
        put(partition, 188, 'II', PART, blocks - PART - 257)
        logical = bytearray(B); put(logical, 212, 'I', B)
        domain = b'*OSTA UDF Compliant'; logical[217:217 + len(domain)] = domain
        put(logical, 240, 'H', 0x0102)
        logical[248:264] = long_ad(B, 0)
        put(logical, 264, 'II', 6, 1); logical[440:446] = struct.pack('<BBHH', 1, 6, 1, 0)
        for start in [257, 270]:
            for offset, (kind, data) in enumerate([(1, pvd), (5, partition), (6, logical), (8, bytearray(B))]):
                write(start + offset, tagged(kind, start + offset, data))
        fsd = bytearray(B); fsd[400:416] = long_ad(B, 1); write(PART, tagged(256, 0, fsd, 496))
        for icb, (_, directory, entries) in nodes.items():
            block = 20 + icb
            content = b''.join(fid(name, child, is_dir, block) for name, child, is_dir in entries)
            write(PART + block, content)
            write(PART + icb, fe(icb, directory, len(content), [(len(content), block)]))
        write(PART + 5, fe(5, False, len(sfo), inline=sfo))
        for icb, long in [(6, False), (7, True)]:
            write(PART + icb, fe(icb, False, len(fragments), [(4096, 100), (769, 150)], long=long))
        write(PART + 8, fe(8, False, 0, inline=b''))
        write(PART + 9, fe(9, False, 4, inline=b'D2OK'))
        write(PART + 10, fe(10, False, 4, [(4, late)]))
        write(PART + 100, fragments[:4096]); write(PART + 150, fragments[4096:])
        write(PART + late, b'LATE')
        for icb in additional:
            write(PART + icb, fe(icb, False, 4, [(4, 190)]))
        if complete: write(PART + 190, b'D2OK')
    return fragments


def main(root):
    root.mkdir(parents=True, exist_ok=True)
    fragments = make(root / 'RETRÔ-ação' / 'D2.iso')
    (root / 'expected.bin').write_bytes(fragments)
    make(root / 'large.iso', large=True)
    make(root / 'complete.iso', complete=True)
    (root / 'bad.iso').write_bytes(bytes(1024 * B))
    data = (root / 'RETRÔ-ação' / 'D2.iso').read_bytes()
    (root / 'unaligned.iso').write_bytes(data[:-1])
    corrupt = bytearray(data)
    put(corrupt, (PART + 5) * B + 176 + 8, 'I', 0xffffffff)
    (root / 'bad-sfo.iso').write_bytes(corrupt)
    print('Synthetic UDF fixtures ready; large.iso is sparse.')


if __name__ == '__main__':
    main(Path(sys.argv[1]))
