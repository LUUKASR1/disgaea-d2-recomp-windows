"""Own synthetic ELFv1 fixtures inside an ISO; no game files or execution."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'port/vfs/tests'))
from make_iso9660 import create, both

cli=Path(sys.argv[1]).resolve()
def elf():
    data=bytearray(544)
    data[:16]=b'\x7fELF\x02\x02\x01'+bytes(9)
    struct.pack_into('>HHIQQQIHHHHHH',data,16,2,21,1,0x20000,64,0,0,64,56,2,0,0,0)
    struct.pack_into('>IIQQQQQQ',data,64,1,5,256,0x10000,0,8,8,4)
    struct.pack_into('>IIQQQQQQ',data,120,1,6,512,0x20000,0,32,64,4)
    struct.pack_into('>II',data,512,0x10000,0x20010)
    return data
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp); create(root/'base.iso'); original=(root/'base.iso').read_bytes()
    marker=original.index(b'EBOOT.BIN;1')-33
    def check(name,payload,*expected):
        disc=bytearray(original)
        disc[marker+10:marker+18]=both(len(payload),4)
        disc[351*2048:351*2048+len(payload)]=payload
        path=root/(name+'.iso'); path.write_bytes(disc)
        result=subprocess.check_output([str(cli),str(path)],text=True,timeout=15)
        for field in expected: assert field+'\n' in result,(name,field,result)
        if 'executable_format=elf\n' in result:
            assert 'executable_guest_memory_written=false\n' in result
    check('valid',elf(),'executable_status=elf-plan-valid','executable_load_segments=2',
          'executable_planned_memory_bytes=72','executable_entry_opd=structurally-valid')
    check('self',b'SCE\0'+bytes(60),'executable_format=self-container','executable_status=self-loader-required')
    check('unknown',b'D2OK','executable_status=unrecognized')
    check('short',b'\x7fELF','executable_reason=truncated-elf-header')
    def mutate(name,offset,fmt,value,reason):
        data=elf(); struct.pack_into(fmt,data,offset,value)
        check(name,data,'executable_status=rejected','executable_reason='+reason)
    mutate('little-endian',5,'B',1,'requires-elf64-big-endian-ppc64-executable')
    mutate('wrong-cpu',18,'>H',62,'requires-elf64-big-endian-ppc64-executable')
    mutate('oversize-table',56,'>H',129,'program-table-bounds')
    mutate('table-overflow',32,'>Q',0xfffffffffffffff0,'program-table-bounds')
    mutate('file-overflow',64+8,'>Q',0xfffffffffffffff0,'load-segment-bounds-or-alignment')
    mutate('guest-overflow',64+16,'>Q',0x100000000,'load-segment-bounds-or-alignment')
    mutate('bss-budget',64+40,'>Q',0x10000001,'load-segment-bounds-or-alignment')
    mutate('bad-align',64+48,'>Q',3,'load-segment-bounds-or-alignment')
    mutate('overlap',120+16,'>Q',0x10004,'overlapping-load-segments')
    mutate('entry-outside',24,'>Q',0x30000,'entry-opd-not-file-backed')
    mutate('entry-offset-overflow',24,'>Q',0x100020000,'invalid-entry-opd')
    mutate('code-outside',512,'>I',0x30000,'entry-code-or-toc-outside-load-plan')
    mutate('toc-outside',516,'>I',0x30000,'entry-code-or-toc-outside-load-plan')
print('PASS: 18 executable format/load-plan cases, no guest writes or execution')
