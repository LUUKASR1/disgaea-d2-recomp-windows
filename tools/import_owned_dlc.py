"""Install DLC flags the user owns into a Disgaea D2 DLC game-data folder, any region.

Region-free: the Japanese, USA and European releases read the same DLC flag
table (USRDIR/Data/flag/flagGGGGIIII.edat in a GAMEDATA folder found through
cellGameDataCheck). Owned flags from any region therefore work in the JP
(NPJB00300), USA (NPUB31321) and EU (NPEB01818) builds.

This tool contains no keys and decrypts nothing. Free-license (type 3) flags
need nothing more. Paid (license type 1/2) flags need the owner's own 16-byte
RIF key for their content ID, given as <CONTENT_ID>.rifkey in --keys (or as a
<flag>.edat.rifkey file next to the flag); it is written next to the installed
flag as <flag>.edat.rifkey, the sidecar the runtime already reads. The runtime
checks every key at boot and logs "[NPDRM] rejected" for one that does not open
its flag. Use only your own DLC and licenses, and never share .rifkey files.

  python tools/import_owned_dlc.py --target us --hdd D:\\MyData\\dev_hdd0 \\
      --dlc D:\\MyData\\JP-dev_hdd0\\game\\NPJB00300 --keys D:\\MyData\\keys
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile

TARGETS = {'jp': 'NPJB00300', 'us': 'NPUB31321', 'eu': 'NPEB01818'}
FLAG_NAME = re.compile(r'flag\d{8}\.edat')
CONTENT_ID = re.compile(r'[A-Z]{2}\d{4}-(NPJB00300|NPUB31321|NPEB01818)_00-[A-Z0-9]{16}')
MAX_FLAG_SIZE = 64 * 1024


class Flag:
    def __init__(self, path):
        path = Path(path)
        if path.is_symlink() or not path.is_file(): raise ValueError('Flag DLC inválido: ' + path.name)
        if not FLAG_NAME.fullmatch(path.name): raise ValueError('Nome de flag DLC inválido: ' + path.name)
        data = path.read_bytes()
        if not 0x100 <= len(data) <= MAX_FLAG_SIZE or data[:4] != b'NPD\0': raise ValueError('Não é um EDAT: ' + path.name)
        version, license_type = struct.unpack_from('>II', data, 4)
        content = data[0x10:0x40].split(b'\0', 1)[0]
        if not 1 <= version <= 4 or license_type not in (1, 2, 3): raise ValueError('EDAT não suportado: ' + path.name)
        if not CONTENT_ID.fullmatch(content.decode('ascii', 'replace')): raise ValueError('Content ID de outro jogo: ' + path.name)
        self.path, self.name, self.data = path, path.name, data
        self.content_id, self.license = content.decode('ascii'), license_type


def read_key(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file(): return None
    data = path.read_bytes()
    if len(data) != 16: raise ValueError('.rifkey deve ter 16 bytes: ' + path.name)
    return data


def flag_folder(source):
    """Accept a DLC game folder (…/NPJB00300), its USRDIR/Data, or the flag folder itself."""
    source = Path(source)
    for candidate in (source / 'USRDIR/Data/flag', source / 'Data/flag', source / 'flag', source):
        if candidate.is_dir() and not candidate.is_symlink(): return candidate
    raise ValueError('Pasta de DLC não encontrada: ' + str(source))


def plan(sources, keys_dir=None):
    flags, keys, missing = {}, {}, {}
    for source in sources:
        folder = flag_folder(source)
        for path in sorted(folder.iterdir()):
            if not FLAG_NAME.fullmatch(path.name): continue
            flag = Flag(path)
            if flag.name in flags and flags[flag.name].data != flag.data:
                raise ValueError('Flag repetido com conteúdo diferente: ' + flag.name)
            flags[flag.name] = flag
            sidecar = read_key(path.with_name(path.name + '.rifkey'))
            if sidecar: keys.setdefault(flag.content_id, sidecar)
    if keys_dir:
        for path in sorted(Path(keys_dir).glob('*.rifkey')):
            if CONTENT_ID.fullmatch(path.stem): keys[path.stem] = read_key(path)
    ready = []
    for flag in flags.values():
        key = keys.get(flag.content_id) if flag.license != 3 else None
        if flag.license != 3 and key is None:
            missing.setdefault(flag.content_id, []).append(flag.name); continue
        ready.append((flag, key))
    if not flags: raise ValueError('Nenhum flag DLC (flagGGGGIIII.edat) encontrado.')
    return ready, missing


def sfo_bytes(fields):
    keys, values, index = b'', b'', b''
    for key, value in sorted(fields.items()):
        if isinstance(value, int): raw, fmt, size, cap = struct.pack('<I', value), 0x0404, 4, 4
        else:
            raw = value.encode('utf-8') + b'\0'; fmt, size = 0x0204, len(raw); cap = (size + 3) // 4 * 4; raw = raw.ljust(cap, b'\0')
        index += struct.pack('<HHIII', len(keys), fmt, size, cap, len(values)); keys += key.encode() + b'\0'; values += raw
    keys = keys.ljust((len(keys) + 3) // 4 * 4, b'\0'); start = 20 + len(index)
    return b'\0PSF' + struct.pack('<IIII', 0x101, start, start + len(keys), len(fields)) + index + keys + values


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, name = tempfile.mkstemp(prefix='.dlc-pending-', dir=path.parent)
    os.close(handle)
    temporary = Path(name)
    try:
        temporary.write_bytes(data); temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def install(target, hdd, sources, keys_dir=None, replace=False, check=False):
    title = TARGETS[target]
    ready, missing = plan(sources, keys_dir)
    game = Path(hdd) / 'game' / title
    flag_dir = game / 'USRDIR/Data/flag'
    conflicts = [f.name for f, k in ready if (flag_dir / f.name).exists() and (flag_dir / f.name).read_bytes() != f.data
                 or k and (flag_dir / (f.name + '.rifkey')).exists() and (flag_dir / (f.name + '.rifkey')).read_bytes() != k]
    if conflicts and not replace:
        raise ValueError('Flags já instalados com conteúdo diferente (use --replace): ' + ', '.join(sorted(conflicts)))
    for content_id, names in sorted(missing.items()):
        print('Sem .rifkey para ' + content_id + ' (não instalado): ' + ', '.join(sorted(names)))
    if check or not ready:
        print(('Verificação: ' if check else 'Nada instalado: ') + str(len(ready)) + ' flag(s) prontos para ' + title)
        return ready, missing
    if not (game / 'PARAM.SFO').is_file():
        write(game / 'PARAM.SFO', sfo_bytes({'ATTRIBUTE': 0, 'CATEGORY': 'GD', 'PARENTAL_LEVEL': 1,
                                             'TITLE': 'Disgaea D2', 'TITLE_ID': title, 'VERSION': '01.00'}))
    for flag, key in ready:
        write(flag_dir / flag.name, flag.data)
        if key: write(flag_dir / (flag.name + '.rifkey'), key)
    receipt = Path(hdd) / 'package-receipts' / ('dlc-' + title + '.json')
    record = {f.name: {'content_id': f.content_id, 'license': f.license, 'sha256': hashlib.sha256(f.data).hexdigest()}
              for f, k in ready}
    if receipt.is_file(): record = dict(json.loads(receipt.read_text(encoding='utf-8')), **record)
    write(receipt, json.dumps(record, indent=2, sort_keys=True).encode('utf-8'))
    print('DLC instalado em ' + title + ': ' + str(len(ready)) + ' flag(s); ' +
          str(sum(len(n) for n in missing.values())) + ' sem chave.')
    return ready, missing


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--target', required=True, choices=sorted(TARGETS), help='Build: jp, us or eu.')
    parser.add_argument('--hdd', required=True, type=Path, help='The dev_hdd0 folder used by that build.')
    parser.add_argument('--dlc', required=True, type=Path, action='append', help='Owned DLC folder; repeat for several.')
    parser.add_argument('--keys', type=Path, help='Folder with your own <CONTENT_ID>.rifkey files.')
    parser.add_argument('--replace', action='store_true', help='Replace flags already installed with different content.')
    parser.add_argument('--check', action='store_true', help='Only report what would be installed.')
    args = parser.parse_args(argv)
    try:
        install(args.target, args.hdd, args.dlc, args.keys, args.replace, args.check)
    except (OSError, ValueError) as error:
        print('ERRO: ' + str(error), file=sys.stderr); return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
