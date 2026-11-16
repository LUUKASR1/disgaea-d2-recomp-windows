"""Asset-free crypto/parser/import regressions; contains no game data."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from Crypto.Cipher import AES
from Crypto.Util.strxor import strxor
import import_owned_pkg as pkg
import prepare_owned_self as self_decoder


def sfo(title='NPUB31321', version='01.00'):
    keys = b'TITLE_ID\0APP_VER\0'; values = title.encode()+b'\0'+version.encode()+b'\0'
    return (b'\0PSF'+struct.pack('<4I', 0x101, 52, 52+len(keys), 2) +
        struct.pack('<HHIII', 0, 0x204, 10, 10, 0) + struct.pack('<HHIII', 9, 0x204, 6, 6, 10) + keys + values)


def package(entries=None, debug=False):
    entries = entries or [('PARAM.SFO', sfo(), 3), ('USRDIR/Data/flag/flag00000001.edat', b'Synthetic DLC fixture', 3)]
    table = bytearray(len(entries)*32); names = b''; payloads = b''
    data_offset = len(table)+sum(len(n.encode()) for n, data, kind in entries)
    for i, (name, data, kind) in enumerate(entries):
        raw = name.encode(); no = len(table)+len(names); off = data_offset+len(payloads)
        struct.pack_into('>IIQQII', table, i*32, no, len(raw), off, len(data), kind, 0)
        names += raw; payloads += data
    plain = bytes(table)+names+payloads; header = bytearray(160)
    header[:4] = b'\x7fPKG'; struct.pack_into('>HH', header, 4, 0 if debug else 0x8000, 1)
    struct.pack_into('>I', header, 20, len(entries))
    struct.pack_into('>QQQ', header, 24, 160+len(plain), 160, len(plain))
    content = b'UP1063-NPUB31321_00-SYNTHETICFIXTURE'; header[48:48+len(content)] = content
    header[96:112] = bytes(range(16)); header[112:128] = bytes(range(16, 32))
    if debug:
        qa = header[96:112]; seed = qa[:8]*2+qa[8:]*2+bytes(24)
        stream = b''.join(hashlib.sha1(seed+i.to_bytes(8,'big')).digest()[:16] for i in range((len(plain)+15)//16))
        encrypted = strxor(plain, stream[:len(plain)])
    else:
        encrypted = AES.new(pkg.PKG_KEY,AES.MODE_CTR,nonce=b'',initial_value=int.from_bytes(header[112:128],'big')).encrypt(plain)
    return bytes(header)+encrypted


def synthetic_self():
    body = b'Synthetic executable fixture; no console game code.'
    elf = struct.pack('>16sHHIQQQIHHHHHH', b'\x7fELF\x02\x02\x01'+bytes(9), 2, 21, 1, 0, 64, 0, 0, 64, 56, 1, 64, 0, 0)
    ph = struct.pack('>II6Q', 1, 5, 256, 0x10000, 0x10000, len(body), len(body), 16)
    expected = elf+ph+bytes(256-120)+body
    key, iv = bytes(range(16)), bytes(range(16, 32))
    compressed = zlib.compress(body)
    segment = AES.new(key,AES.MODE_CTR,nonce=b'',initial_value=int.from_bytes(iv,'big')).encrypt(compressed)
    data = bytearray(0x280+len(segment))
    struct.pack_into('>IIHHIQQ', data, 0, 0x53434500, 2, 28, 1, 0x180, 0x280, len(expected))
    struct.pack_into('>10Q', data, 32, 3, 0x70, 0x90, 0xd0, 0, 0, 0, 0x108, 144, 0)
    struct.pack_into('>QIIQQ', data, 0x70, 0, 0, 8, 0, 0)
    data[0x90:0xd0] = elf; data[0xd0:0x108] = ph
    struct.pack_into('>IIQ', data, 0x108, 3, 144, 0); data[0x118:0x11c]=b'NPD\0'
    struct.pack_into('>I', data, 0x120, 3)
    info = key+bytes(16)+iv+bytes(16)
    info = AES.new(self_decoder.ROOT_KEY,AES.MODE_CBC,iv=self_decoder.ROOT_IV).encrypt(info)
    free = AES.new(self_decoder.WRAP_KEY,AES.MODE_ECB).decrypt(self_decoder.FREE_KEY)
    data[0x1a0:0x1e0]=AES.new(free,AES.MODE_CBC,iv=bytes(16)).encrypt(info)
    metadata = (struct.pack('>Q6I', 0, 0, 1, 2, 0, 0, 0) +
        struct.pack('>QQ8I', 0x280, len(segment), 2, 0, 0, 0, 3, 0, 1, 2)+key+iv)
    metadata += bytes(0xa0-len(metadata))
    data[0x1e0:0x280]=AES.new(key,AES.MODE_CTR,nonce=b'',initial_value=int.from_bytes(iv,'big')).encrypt(metadata)
    data[0x280:]=segment
    return bytes(data), expected


class NativePreparationTests(unittest.TestCase):
    def test_retail_and_debug_import_and_verified_retry(self):
        for debug in (False, True):
            with self.subTest(debug=debug), tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp); source=root/'fixture.pkg'; source.write_bytes(package(debug=debug))
                pkg.install(source, root/'hdd0'); pkg.install(source, root/'hdd0')
                flag=root/'hdd0/game/NPUB31321/USRDIR/Data/flag/flag00000001.edat'
                self.assertEqual(flag.read_bytes(), b'Synthetic DLC fixture')
                flag.write_bytes(b'Preserve modified content')
                with self.assertRaisesRegex(ValueError,'preservado'):pkg.install(source,root/'hdd0')
                self.assertEqual(flag.read_bytes(),b'Preserve modified content')

    def test_hostile_paths_rejected_before_output(self):
        for name in ('../outside', '/root', 'C:/file', 'USRDIR/CON.txt', 'USRDIR/a.', 'USRDIR\\evil'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp); source=root/'fixture.pkg'
                source.write_bytes(package([('PARAM.SFO',sfo(),3),(name,b'Bad name',3)]))
                with self.assertRaises(ValueError):pkg.install(source,root/'hdd0')
                self.assertFalse((root/'hdd0').exists())

    def test_truncation_duplicate_and_missing_dlc_rejected(self):
        bad = [package()[:-1], package([('PARAM.SFO',sfo(),3),('param.sfo',sfo(),3)]), package([('PARAM.SFO',sfo(),3)])]
        for data in bad:
            with tempfile.TemporaryDirectory() as tmp:
                source=Path(tmp)/'fixture.pkg';source.write_bytes(data)
                with self.assertRaises(ValueError):pkg.install(source,Path(tmp)/'hdd0')

    def test_independent_self_crypto_and_output_preservation(self):
        data, expected = synthetic_self()
        with patch.object(self_decoder,'SELF_SHA',hashlib.sha256(data).hexdigest()), \
             patch.object(self_decoder,'ELF_SHA',hashlib.sha256(expected).hexdigest()):
            self.assertEqual(self_decoder.decode(data),expected)
            with tempfile.TemporaryDirectory() as tmp:
                source=Path(tmp)/'self.bin';source.write_bytes(data); out=Path(tmp)/'owned.elf'
                self_decoder.prepare(source,out);self_decoder.prepare(source,out)
                out.write_bytes(b'Preserve previous ELF')
                with self.assertRaisesRegex(ValueError,'preserved'):self_decoder.prepare(source,out)
                self.assertEqual(out.read_bytes(),b'Preserve previous ELF')

    def test_unrecognized_self_rejected_without_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'self.bin';source.write_bytes(b'Synthetic unsupported input')
            out=Path(tmp)/'out.elf'
            with self.assertRaises(ValueError):self_decoder.prepare(source,out)
            self.assertFalse(out.exists())


if __name__ == '__main__': unittest.main()
