"""Bounded PS3 PKG importer for owned USA 1.40 and NPUB31321 DLC content, and the
owned Japan BLJS10206 disc updates 1.10 + 1.20 + 1.30 (installed together).

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
from prepare_owned_self import SELF_SHA, SELF_SHA_JP

JP_UPDATES = ('01.10', '01.20', '01.30')   # incremental; 1.30 carries the supported EBOOT

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
            match = re.fullmatch(r'[A-Z]{2}\d{4}-(BLUS31313|NPUB31321|BLJS10206)_00-[A-Z0-9_]+', content)
            if not match: raise ValueError('PKG não corresponde à atualização/DLC suportada.')
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
            elif self.title in ('BLUS31313', 'BLJS10206'): raise ValueError('Update PARAM.SFO absent')
            self.app_ver = fields.get('APP_VER') if 'PARAM.SFO' in files else None
            if self.title == 'BLUS31313':
                if fields.get('APP_VER') != '01.40' or 'USRDIR/Data/START_7.dat' not in files:
                    raise ValueError('Atualização USA 1.40 ausente')
                off, size = files.get('USRDIR/EBOOT.BIN', (0, 0))
                if size > 16*1024*1024 or hashlib.sha256(self.read(off, size)).hexdigest() != SELF_SHA:
                    raise ValueError('EBOOT da atualização não corresponde à versão suportada')
            elif self.title == 'BLJS10206':
                if self.app_ver not in JP_UPDATES: raise ValueError('Atualização japonesa 1.10/1.20/1.30 esperada')
                if self.app_ver == '01.30':
                    off, size = files.get('USRDIR/EBOOT.BIN', (0, 0))
                    if size > 16*1024*1024 or hashlib.sha256(self.read(off, size)).hexdigest() != SELF_SHA_JP:
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
        if package.title == 'BLJS10206':
            raise ValueError('As atualizações japonesas são incrementais: informe 1.10, 1.20 e 1.30 juntas (--pkg três vezes).')
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


def sfo_bytes(fields):
    """Minimal PARAM.SFO writer (sorted keys, utf8-S and int32 values)."""
    keys, values, index = b'', b'', b''
    for key, value in sorted(fields.items()):
        if isinstance(value, int): raw, fmt, size, cap = struct.pack('<I', value), 0x0404, 4, 4
        else:
            raw = value.encode('utf-8') + b'\0'; fmt, size = 0x0204, len(raw); cap = (size + 3) // 4 * 4; raw = raw.ljust(cap, b'\0')
        index += struct.pack('<HHIII', len(keys), fmt, size, cap, len(values)); keys += key.encode() + b'\0'; values += raw
    keys = keys.ljust((len(keys) + 3) // 4 * 4, b'\0'); start = 20 + len(index)
    return b'\0PSF' + struct.pack('<IIII', 0x101, start, start + len(keys), len(fields)) + index + keys + values


def commit(staging, target, receipt, record):
    snapshot = {p.relative_to(staging).as_posix(): file_hash(p) for p in staging.rglob('*') if p.is_file()}
    staging.rename(target)
    receipt.parent.mkdir(parents=True, exist_ok=True)
    temporary = receipt.with_suffix('.pending')
    temporary.write_text(json.dumps(dict(record, files=snapshot)), encoding='utf-8')
    temporary.replace(receipt)
    return snapshot


def installed(target, receipt, record):
    if not target.exists(): return False
    if receipt.is_file():
        saved = json.loads(receipt.read_text())
        snapshot = {p.relative_to(target).as_posix(): file_hash(p) for p in target.rglob('*') if p.is_file()}
        if all(saved.get(k) == v for k, v in record.items()) and saved.get('files') == snapshot: return True
    raise ValueError('Conteúdo existente diferente; preservado. Use o perfil standalone separado.')


def install_jp(sources, hdd):
    """Layer the three Japanese disc updates into game/BLJS10206 and expose their
    data as the NPJB00300 game-data folder, where the 1.30 EBOOT looks for it
    (cellGameDataCheck, type GAMEDATA). Each step commits as a new directory."""
    packages = [Package(s) for s in sources]
    try:
        by_version = {p.app_ver: (p, s) for p, s in zip(packages, sources) if p.title == 'BLJS10206'}
        if len(packages) != 3 or sorted(by_version) != list(JP_UPDATES):
            raise ValueError('Informe as três atualizações de disco japonesas: 1.10, 1.20 e 1.30.')
        root = Path(hdd) / 'game'; root.mkdir(parents=True, exist_ok=True)
        receipts = Path(hdd) / 'package-receipts'
        record = {'packages': [file_hash(by_version[v][1]) for v in JP_UPDATES]}
        target = root / 'BLJS10206'
        if not installed(target, receipts / 'BLJS10206.json', record):
            staging = Path(tempfile.mkdtemp(prefix='.pkg-pending-', dir=root))
            try:
                for version in JP_UPDATES:                      # later updates replace earlier files
                    package = by_version[version][0]
                    for name, offset, size, kind in package.entries:
                        out = staging.joinpath(*name.split('/'))
                        if kind == 4: out.mkdir(parents=True, exist_ok=True); continue
                        out.parent.mkdir(parents=True, exist_ok=True)
                        out.unlink(missing_ok=True)
                        with out.open('xb') as f:
                            for position in range(0, size, 1024*1024):
                                f.write(package.read(offset+position, min(1024*1024, size-position)))
                files = commit(staging, target, receipts / 'BLJS10206.json', record)
                print('Independent PKG import PASS: BLJS10206 1.10+1.20+1.30, files=' + str(len(files)))
            finally:
                if staging.exists(): shutil.rmtree(staging)
        data = root / 'NPJB00300'
        if not installed(data, receipts / 'NPJB00300.json', record):
            staging = Path(tempfile.mkdtemp(prefix='.pkg-pending-', dir=root))
            try:
                (staging / 'PARAM.SFO').write_bytes(sfo_bytes({'ATTRIBUTE': 0, 'CATEGORY': 'GD', 'PARENTAL_LEVEL': 1,
                    'TITLE': 'Disgaea D2', 'TITLE_ID': 'NPJB00300', 'VERSION': '01.00'}))
                shutil.copytree(target / 'USRDIR/Data', staging / 'USRDIR/Data')
                files = commit(staging, data, receipts / 'NPJB00300.json', record)
                print('Japanese game data prepared: NPJB00300, files=' + str(len(files)))
            finally:
                if staging.exists(): shutil.rmtree(staging)
    finally:
        for p in packages: p.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pkg', required=True, type=Path, action='append',
                        help='Package to import; repeat for the three Japanese updates (1.10, 1.20, 1.30).')
    parser.add_argument('--hdd', required=True, type=Path)
    args = parser.parse_args()
    if len(args.pkg) == 1: install(args.pkg[0], args.hdd)
    else: install_jp(args.pkg, args.hdd)
