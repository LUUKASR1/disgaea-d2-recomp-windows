"""Bounded PS3 PKG importer for owned USA 1.40 and NPUB31321 DLC content.

Streams AES-CTR retail or SHA-1 debug packages. Validates paths and supported
metadata before writing; commits each package as a new directory. No firmware,
emulator, external executable, license generation or EDAT decryption is used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile
from Crypto.Cipher import AES
from Crypto.Util.strxor import strxor
from prepare_owned_self import SELF_SHA

PKG_KEY = bytes.fromhex('2e7b71d7c9c9a14ea3221f188828b8f8')


def safe_name(raw):
    name = raw.decode('ascii')
    parts = name.split('/')
    reserved = {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(10)), *(f'LPT{i}' for i in range(10))}
    if not parts or len(name) > 512 or any(not part or part in ('.', '..') or
        part.endswith((' ', '.')) or part.split('.')[0].upper() in reserved or
        any(ord(c) < 32 or c in '\\:*?"<>|' for c in part) for part in parts):
        raise ValueError('Unsafe PKG entry path')
    return '/'.join(parts)


def sfo_fields(data):
    if len(data) < 20 or len(data) > 1024 * 1024 or data[:8] != b'\0PSF\x01\x01\0\0':
        raise ValueError('Invalid PARAM.SFO')
    keys, values, count = struct.unpack_from('<III', data, 8)
    if count > 256 or not 20 + count * 16 <= keys <= values <= len(data): raise ValueError('Bad SFO tables')
    result = {}
    for i in range(count):
        ko, fmt, size, capacity, offset = struct.unpack_from('<HHIII', data, 20 + 16*i)
        key_start = keys + ko; key_end = data.find(b'\0', key_start, values)
        if key_start >= values or key_end < 0 or size > capacity or values + offset + capacity > len(data):
            raise ValueError('Bad SFO field')
        key = data[key_start:key_end].decode('ascii')
        if key in result: raise ValueError('Duplicate SFO field')
        if fmt == 0x204:
            raw = data[values+offset:values+offset+size]
            if not raw or raw[-1] != 0: raise ValueError('Unterminated SFO string')
            result[key] = raw[:-1].decode('utf-8')
    return result


class Package:
    def __init__(self, path):
        self.file = Path(path).open('rb')
        try:
            self.file.seek(0, 2); self.length = self.file.tell(); self.file.seek(0)
            header = self.file.read(160)
            if len(header) != 160 or header[:4] != b'\x7fPKG': raise ValueError('Invalid PKG header')
            self.revision, kind = struct.unpack_from('>HH', header, 4)
            if self.revision not in (0, 0x8000) or kind != 1: raise ValueError('Only PS3 retail/debug PKGs supported')
            count = struct.unpack_from('>I', header, 20)[0]
            declared, self.offset, self.size = struct.unpack_from('>QQQ', header, 24)
            if declared != self.length or not 160 <= self.offset <= self.length or self.size > self.length-self.offset:
                raise ValueError('Truncated PKG payload')
            if not 0 < count <= 50000 or count*32 > self.size: raise ValueError('Bad PKG item count')
            content = header[48:96].split(b'\0', 1)[0].decode('ascii')
            match = re.fullmatch(r'[A-Z]{2}\d{4}-(BLUS31313|NPUB31321)_00-[A-Z0-9_]+', content)
            if not match: raise ValueError('PKG não corresponde à atualização/DLC USA suportada.')
            self.title = match[1]; self.iv = header[112:128]; self.qa = header[96:112]
            self.entries = []; names = set()
            table = self.read(0, count * 32)
            for i in range(count):
                no, ns, off, size, flags, pad = struct.unpack_from('>IIQQII', table, i*32)
                if not 0 < ns <= 512 or no < count*32 or no+ns > self.size: raise ValueError('Bad PKG name range')
                name = safe_name(self.read(no, ns)); folded = name.casefold()
                if folded in names: raise ValueError('Duplicate PKG path')
                names.add(folded)
                kind = flags & 255
                if flags & 0x10000000 or kind not in (1, 2, 3, 4): raise ValueError('Unsupported PKG entry type')
                if kind == 4 and size: raise ValueError('Nonempty directory entry')
                if off > self.size or size > self.size-off or size > 2*1024**3: raise ValueError('Bad PKG file range')
                self.entries.append((name, off, size, kind))
            files = {name: (off, size) for name, off, size, kind in self.entries if kind != 4}
            # Reject file/directory collisions before creating a staging tree.
            file_names = {n.casefold() for n in files}
            for name, off, size, kind in self.entries:
                parts = name.split('/')
                if any('/'.join(parts[:i]).casefold() in file_names for i in range(1, len(parts))):
                    raise ValueError('PKG file shadows a directory')
            if sum(size for name, off, size, kind in self.entries) > 8*1024**3: raise ValueError('PKG extraction quota exceeded')
            if 'PARAM.SFO' in files:
                off, size = files['PARAM.SFO']
                if size > 1024*1024: raise ValueError('SFO exceeds limit')
                fields = sfo_fields(self.read(off, size))
                if fields.get('TITLE_ID') != self.title: raise ValueError('PKG/SFO title mismatch')
            elif self.title == 'BLUS31313': raise ValueError('Update PARAM.SFO absent')
            if self.title == 'BLUS31313':
                if fields.get('APP_VER') != '01.40' or 'USRDIR/Data/START_7.dat' not in files:
                    raise ValueError('Atualização USA 1.40 ausente')
                off, size = files.get('USRDIR/EBOOT.BIN', (0, 0))
                if size > 16*1024*1024 or hashlib.sha256(self.read(off, size)).hexdigest() != SELF_SHA:
                    raise ValueError('EBOOT da atualização não corresponde à versão suportada')
            elif not any(re.fullmatch(r'USRDIR/Data/flag/flag\d{8}\.edat', n) and size for n, (off, size) in files.items()):
                raise ValueError('Pacote não contém os flags DLC NPUB31321 esperados')
        except BaseException:
            self.file.close(); raise

    def read(self, offset, size):
        if size < 0 or size > 16*1024*1024 or offset < 0 or offset+size > self.size:
            raise ValueError('PKG read outside bounds')
        start = offset//16*16; prefix = offset-start
        self.file.seek(self.offset+start); encrypted = self.file.read(prefix+size)
        if len(encrypted) != prefix+size: raise ValueError('Short PKG read')
        if self.revision == 0x8000:
            counter = int.from_bytes(self.iv, 'big') + start//16
            plain = AES.new(PKG_KEY, AES.MODE_CTR, nonce=b'', initial_value=counter).decrypt(encrypted)
        else:
            seed = self.qa[:8]*2 + self.qa[8:]*2 + bytes(24)
            stream = b''.join(hashlib.sha1(seed + i.to_bytes(8, 'big')).digest()[:16]
                              for i in range(start//16, (start+len(encrypted)+15)//16))
            plain = strxor(encrypted, stream[:len(encrypted)])
        return plain[prefix:]

    def close(self): self.file.close()


def file_hash(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as f:
        while chunk := f.read(1024*1024): result.update(chunk)
    return result.hexdigest()


def install(source, hdd):
    package = Package(source)
    try:
        root = Path(hdd) / 'game'; root.mkdir(parents=True, exist_ok=True)
        target = root / package.title; digest = file_hash(source)
        receipt = Path(hdd) / 'package-receipts' / (package.title + '.json')
        if target.exists():
            if receipt.is_file():
                saved = json.loads(receipt.read_text())
                snapshot = {p.relative_to(target).as_posix(): file_hash(p) for p in target.rglob('*') if p.is_file()}
                if saved.get('package') == digest and saved.get('files') == snapshot:
                    print('Verified package already installed: ' + package.title); return
            raise ValueError('Conteúdo existente diferente; preservado. Use o perfil standalone separado.')
        staging = Path(tempfile.mkdtemp(prefix='.pkg-pending-', dir=root))
        try:
            for name, offset, size, kind in package.entries:
                out = staging.joinpath(*name.split('/'))
                if kind == 4: out.mkdir(parents=True, exist_ok=True); continue
                out.parent.mkdir(parents=True, exist_ok=True)
                with out.open('xb') as f:
                    for position in range(0, size, 1024*1024):
                        f.write(package.read(offset+position, min(1024*1024, size-position)))
            snapshot = {p.relative_to(staging).as_posix(): file_hash(p) for p in staging.rglob('*') if p.is_file()}
            staging.rename(target)
            receipt.parent.mkdir(parents=True, exist_ok=True)
            temporary = receipt.with_suffix('.pending')
            temporary.write_text(json.dumps({'package': digest, 'files': snapshot}), encoding='utf-8')
            temporary.replace(receipt)
            print('Independent PKG import PASS: ' + package.title + ', files=' + str(len(snapshot)))
        finally:
            if staging.exists(): shutil.rmtree(staging)
    finally: package.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pkg', required=True, type=Path)
    parser.add_argument('--hdd', required=True, type=Path)
    args = parser.parse_args(); install(args.pkg, args.hdd)
