"""Synthetic ECMA-119 fixtures; no bytes copied from any game image."""
from pathlib import Path
import binascii
import ctypes
import os
import struct

B = 2048


def both(value, width):
    return value.to_bytes(width, 'little') + value.to_bytes(width, 'big')


def record(name, lba, size, flags=0):
    identifier = name if isinstance(name, bytes) else name.encode('ascii')
    data = bytearray(33 + len(identifier) + (len(identifier) % 2 == 0))
    data[0] = len(data)
    data[2:10] = both(lba, 4)
    data[10:18] = both(size, 4)
    data[18:25] = bytes([126, 10, 13, 12, 0, 0, 0])
    data[25] = flags
    data[28:32] = both(1, 2)
    data[32] = len(identifier)
    data[33:33 + len(identifier)] = identifier
    return data


def create(path, hybrid=False, large=False):
    blocks = (0x100000000 // B + 512) if large else 1024
    fragments = bytes((i * 17 + 3) & 255 for i in range(4865))
    sfo = (b'\x00PSF' + struct.pack('<IIII', 0x101, 36, 48, 1) +
           struct.pack('<HHIII', 0, 0x0204, 10, 16, 0) + b'TITLE_ID\0' + bytes(3) + b'BLUS31313\0')
    directories = {
        290: [('PS3_GAME', 291, B, 2)],
        291: [('PARAM.SFO;1', 350, len(sfo), 0), ('USRDIR', 292, B, 2)],
        292: [('EBOOT.BIN;1', 351, 4, 0), ('DATA', 293, B, 2)],
        293: [('FRAGMENT.BIN;1', 400, 4096, 128), ('FRAGMENT.BIN;1', 450, 769, 0),
              ('EMPTY.BIN;1', 0, 0, 0), ('VERS.BIN;1', 452, 3, 0), ('VERS.BIN;2', 453, 3, 0),
              ('NISANMSHADOW.TXF;1', 351, 4, 0), ('SOUND', 294, B, 2)],
        294: [('SNDPAKBGM', 295, B, 2)],
        295: [('SNDPAKBGM_0.PAK;1', 351, 4, 0)],
    }
    for stem in ('START', 'ANM_HI'):
        for part in range(7):
            directories[293].append((stem + (f'_{part}' if part else '') + '.DAT;1', 351, 4, 0))
    if large:
        directories[293] += [('LATE.BIN;1', blocks - 20, 4, 0),
                              ('BIG.BIN;1', 400, 0xfffff800, 128),
                              ('BIG.BIN;1', blocks - 10, 4096, 0)]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w+b') as out:
        if os.name == 'nt':
            import msvcrt
            returned = ctypes.c_ulong()
            ok = ctypes.windll.kernel32.DeviceIoControl(ctypes.c_void_p(msvcrt.get_osfhandle(out.fileno())),
                0x900c4, None, 0, None, 0, ctypes.byref(returned), None)
            if large and not ok: raise OSError('Unable to mark ISO9660 fixture sparse')
        out.truncate(blocks * B)

        def write(lba, data):
            out.seek(lba * B); out.write(data)

        pvd = bytearray(B); pvd[0] = 1; pvd[1:6] = b'CD001'; pvd[6] = 1
        pvd[80:88] = both(blocks, 4)
        pvd[120:124] = both(1, 2); pvd[124:128] = both(1, 2); pvd[128:132] = both(B, 2)
        root = record(b'\0', 290, B, 2); pvd[156:156 + len(root)] = root
        write(16, pvd)
        end = bytearray(B); end[0] = 255; end[1:6] = b'CD001'; end[6] = 1; write(18, end)
        if hybrid:
            for lba, identifier in [(19, b'BEA01'), (20, b'NSR03'), (21, b'TEA01')]:
                vrs = bytearray(B); vrs[1:6] = identifier; vrs[6] = 1; write(lba, vrs)
            # A valid anchor tag with deliberately unsupported/absent VDS data:
            # UDF mount fails, while the complete ISO9660 tree remains readable.
            anchor = bytearray(B); struct.pack_into('<IIII', anchor, 16, 4 * B, 257, 4 * B, 270)
            for location in (256, blocks - 1):
                struct.pack_into('<HHBBHHHI', anchor, 0, 2, 2, 0, 0, 1,
                    binascii.crc_hqx(anchor[16:512], 0), 496, location)
                anchor[4] = sum(anchor[:4] + anchor[5:16]) & 255
                write(location, anchor)
        for lba, entries in directories.items():
            content = bytearray(B)
            records = [record(b'\0', lba, B, 2), record(b'\1', 290, B, 2)] + [record(*item) for item in entries]
            position = 0
            for entry in records:
                assert position + len(entry) <= B
                content[position:position + len(entry)] = entry; position += len(entry)
            write(lba, content)
        for lba, payload in [(350, sfo), (351, b'D2OK'), (400, fragments[:4096]),
                             (450, fragments[4096:]), (452, b'OLD'), (453, b'NEW')]: write(lba, payload)
        if large:
            write(blocks - 20, b'LATE')
            write(blocks - 10, bytes((i * 7 + 5) & 255 for i in range(4096)))


def generate(root):
    create(root / 'iso9660-complete.iso')
    create(root / 'hybrid-fallback.iso', hybrid=True)
    create(root / 'iso9660-large.iso', large=True)
    data = bytearray((root / 'iso9660-complete.iso').read_bytes())
    data[16 * B + 84] ^= 1
    (root / 'iso9660-bad-endian.iso').write_bytes(data)
    data = bytearray((root / 'iso9660-complete.iso').read_bytes())
    data[290 * B] = 1
    (root / 'iso9660-bad-record.iso').write_bytes(data)
