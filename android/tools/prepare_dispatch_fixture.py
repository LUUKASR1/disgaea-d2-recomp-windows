"""Build-time only: lift our synthetic PPC instructions with immutable SDK tools.

No input path from the user and no game bytes. The generated image, ABI header
and host C++ all share this address map. Nothing invokes the lifter on Android.
"""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

TOOLS = ('tools/ppu_lifter.py', 'tools/ppu_disasm.py', 'tools/elf_parser.py',
         'tools/nid_database.py')


def registry_subset(source):
    a = source.index(b'typedef void (*ppu_fn)(ppu_context*);')
    b = source.index(b'/* Host-RA -> guest-function resolver', a)
    c = source.index(b'static ppu_fn ppu_lookup(uint32_t a)')
    d = source.index(b'/* Correct native memmove override', c)
    result = source[a:b] + source[c:d]
    if hashlib.sha256(result).hexdigest() != '346bef1a4ebbd537b5adfbe3658c7836f0ec1de6b706180a04e703bf0d37d4d4':
        raise RuntimeError('Pinned function registry extraction changed')
    return b'/* Verbatim MIT-licensed SDK registry subset. */\n' + result


def generate(output):
    generated = output / 'generated'
    generated.mkdir(exist_ok=True)
    # lwz/add/blr; stw/lwz/addi/blr; tail b; mtctr/bctrl/blr; bl/blr; sc/blr.
    # Also test two real time syscalls and two firmware import stubs.
    words = {
        0x10000: [0x80a20000, 0x7c632a14, 0x4e800020],
        0x10020: [0x90620004, 0x80620004, 0x38630001, 0x4e800020],
        0x10040: [0x4bffffc0],
        0x10060: [0x7c8903a6, 0x4e800421, 0x4e800020],
        0x10080: [0x4bffff81, 0x4e800020],
        0x100a0: [0x44000002, 0x4e800020],
        0x100c0: [0x39600093, 0x44000002, 0x4e800020], # li r11,147
        0x100e0: [0x39600091, 0x44000002, 0x4e800020], # li r11,145
        0x10100: [0x4e800020], # lifted import: cellRtcGetCurrentTick
        0x10120: [0x4e800020], # lifted import: deliberately unavailable NID
    }
    import importlib.util
    spec=importlib.util.spec_from_file_location('d2_pinned_nids',output/'tools/nid_database.py')
    nids=importlib.util.module_from_spec(spec); spec.loader.exec_module(nids)
    rtc_nid=nids.compute_nid('cellRtcGetCurrentTick')
    image = bytearray(0x720)
    image[:7] = b'\x7fELF\x02\x02\x01'
    def put(off, value, size):
        image[off:off+size] = value.to_bytes(size, 'big')
    put(16,2,2); put(18,21,2); put(20,1,4); put(24,0x20000,8)
    put(32,64,8); put(52,64,2); put(54,56,2); put(56,2,2)
    for index, (off, va, size, flags) in enumerate(((0x200,0x10000,0x200,5),
                                                 (0x600,0x20000,0x120,6))):
        ph=64+index*56
        put(ph,1,4); put(ph+4,flags,4); put(ph+8,off,8); put(ph+16,va,8)
        put(ph+32,size,8); put(ph+40,size+16,8); put(ph+48,4,8)
    image[0x200:0x400] = struct.pack('>I',0x60000000)*128
    bounds=[]
    for index, (addr, code) in enumerate(words.items()):
        for n, word in enumerate(code): put(0x200+addr-0x10000+n*4,word,4)
        put(0x600+index*8,addr,4); put(0x604+index*8,0x20100,4)
        bounds.append({'start':hex(addr),'end':hex(addr+len(code)*4)})
    put(0x700,7,4)
    elf=generated/'dispatch-fixture.elf'
    elf.write_bytes(image)
    funcs=generated/'dispatch-functions.json'
    funcs.write_text(json.dumps(bounds,indent=2)+'\n')
    imports=generated/'dispatch-imports.json'
    imports.write_text(json.dumps([
        {'library':'cellRtc','nid':hex(rtc_nid),'stub':'0x10100'},
        {'library':'d2-unavailable-test','nid':'0xffffffff','stub':'0x10120'}],indent=2)+'\n')
    subprocess.run([sys.executable,str(output/'tools/ppu_lifter.py'),str(elf),
                    '--functions',str(funcs),'--code-end','0x10200','--hle-stubs',str(imports),
                    '--single-file','--jobs','1','--symbol-prefix','d2_fixture_',
                    '--output',str(generated)],
                   check=True,timeout=60,env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'})
    (generated/'dispatch_image.h').write_text(
        '// Synthetic PPC image owned by this project; no game data.\n'
        '#pragma once\n#include <stdint.h>\n'
        f'static const uint32_t d2_rtc_nid = {rtc_nid}u;\n'
        'static const uint8_t d2_dispatch_image[] = {\n'+
        ','.join(str(b) for b in image)+'\n};\n')
    return ('generated/dispatch-fixture.elf','generated/dispatch-functions.json',
            'generated/ppu_recomp.h','generated/ppu_recomp.c','generated/dispatch_image.h',
            'generated/dispatch-imports.json')
