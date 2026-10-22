"""Independent, bounded decoder for the exact owned USA 1.40 free-license SELF.

This is not an emulator or a general retail-SELF decoder. Unsupported inputs
are rejected before decryption. Public format/key references are documented
in docs/NATIVE-PREPARATION.md; no firmware or game code is included here.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import zlib
from Crypto.Cipher import AES

SELF_SHA = 'c2902f309e7220884d03665ebb779e68bb31545019b039ec2976ae41384805b6'
ELF_SHA = '0ec183e580a270b0bb1b3a554794a9e4ec2d57a2be251ebdcf81fe21b78d225c'
# Public revision 0x1c NPDRM constants, not user/device keys.
ROOT_KEY = bytes.fromhex('8103ea9db790578219c4cedf0592b43064a7d98b601b6c7bc45108c4047aa80f')
ROOT_IV = bytes.fromhex('246f4b8328be6a2d394ede20479247c5')
FREE_KEY = bytes.fromhex('72f990788f9cff745725f08e4c128387')
WRAP_KEY = bytes.fromhex('f2fbca7a75b04edc1390638ccdfdd1ee')


def area(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError('SELF range outside input')
    return data[offset:offset + size]


def ctr(key, iv, data):
    return AES.new(key, AES.MODE_CTR, nonce=b'', initial_value=int.from_bytes(iv, 'big')).decrypt(data)


def decode(data):
    if hashlib.sha256(data).hexdigest() != SELF_SHA:
        raise ValueError('EBOOT.BIN diferente da atualização USA 1.40 suportada.')
    magic, version, revision, kind, meta, head_size, elf_size = struct.unpack('>IIHHIQQ', area(data, 0, 32))
    if (magic, version, revision, kind) != (0x53434500, 2, 28, 1) or elf_size > 16 * 1024 * 1024:
        raise ValueError('Unsupported SCE profile')
    ext, app, eh, ph, sh, seg, ver, control, control_size, padding = struct.unpack('>10Q', area(data, 32, 80))
    if struct.unpack('>I', area(data, app + 12, 4))[0] != 8:
        raise ValueError('Unsupported application type')
    cursor = control; found = False
    while cursor < control + control_size:
        typ, size, following = struct.unpack('>IIQ', area(data, cursor, 16))
        if size < 16 or cursor + size > control + control_size: raise ValueError('Bad control header')
        if typ == 3:
            if area(data, cursor + 16, 4) != b'NPD\0' or struct.unpack('>I', area(data, cursor + 24, 4))[0] != 3:
                raise ValueError('Only the supported free-license update is accepted')
            found = True
        cursor += size
    if not found: raise ValueError('Free-license NPD header absent')
    info = area(data, meta + 32, 64)
    free = AES.new(WRAP_KEY, AES.MODE_ECB).decrypt(FREE_KEY)
    info = AES.new(free, AES.MODE_CBC, iv=bytes(16)).decrypt(info)
    info = AES.new(ROOT_KEY, AES.MODE_CBC, iv=ROOT_IV).decrypt(info)
    if info[16:32] != bytes(16) or info[48:64] != bytes(16): raise ValueError('Invalid metadata padding')
    headers = ctr(info[:16], info[32:48], area(data, meta + 96, head_size - meta - 96))
    sig, unknown, count, keys, options, x, y = struct.unpack('>Q6I', area(headers, 0, 32))
    if count > 64 or keys > 256: raise ValueError('Metadata count out of bounds')
    key_table = area(headers, 32 + count * 48, keys * 16)
    elf = area(data, eh, 64)
    fields = struct.unpack('>16sHHIQQQIHHHHHH', elf)
    ident, typ, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = fields
    if ident[:7] != b'\x7fELF\x02\x02\x01' or machine != 21 or phsize != 56 or shsize != 64 or phnum > 64 or shnum > 256:
        raise ValueError('Unsupported ELF layout')
    # Match the canonical ELF file layout, including all advertised section headers.
    output = bytearray()
    def write(offset, blob):
        end = offset + len(blob)
        if end > 16 * 1024 * 1024: raise ValueError('ELF output exceeds limit')
        if end > len(output): output.extend(bytes(end - len(output)))
        output[offset:end] = blob
    write(0, elf)
    programs = area(data, ph, phnum * phsize)
    write(phoff, programs)
    for index in range(count):
        off, size, stype, program, hashed, sha, encrypted, ki, vi, compressed = struct.unpack('>QQ8I', area(headers, 32 + index * 48, 48))
        if stype != 2: continue
        if encrypted != 3 or ki >= keys or vi >= keys or program >= phnum:
            raise ValueError('Unsupported segment profile')
        phdr = struct.unpack('>II6Q', area(programs, program * phsize, phsize))
        destination, wanted = phdr[2], phdr[5]
        if wanted > 16 * 1024 * 1024: raise ValueError('Segment exceeds limit')
        blob = ctr(key_table[ki*16:ki*16+16], key_table[vi*16:vi*16+16], area(data, off, size))
        if compressed == 2:
            decoder = zlib.decompressobj()
            blob = decoder.decompress(blob, wanted + 1)
            if not decoder.eof or decoder.unconsumed_tail: raise ValueError('Invalid compressed segment')
        elif compressed != 1: raise ValueError('Unsupported compression')
        if len(blob) != wanted: raise ValueError('Segment size mismatch')
        write(destination, blob)
    if sh: write(shoff, area(data, sh, shnum * shsize))
    result = bytes(output)
    if hashlib.sha256(result).hexdigest() != ELF_SHA:
        raise ValueError('ELF gerado não corresponde à versão 1.40 validada; nada foi instalado.')
    return result


def prepare(source, destination):
    source, destination = Path(source), Path(destination)
    if source.stat().st_size > 16 * 1024 * 1024: raise ValueError('SELF exceeds supported limit')
    result = decode(source.read_bytes())
    if destination.exists():
        if destination.read_bytes() != result: raise ValueError('Existing ELF differs; preserved')
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix('.pending')
    temporary.write_bytes(result); temporary.replace(destination)
    print('Independent SELF preparation PASS; ELF SHA256=' + ELF_SHA)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self', dest='source', required=True, type=Path)
    parser.add_argument('--elf', required=True, type=Path)
    args = parser.parse_args()
    prepare(args.source, args.elf)
