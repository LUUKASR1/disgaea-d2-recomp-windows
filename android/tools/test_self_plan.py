"""Independent synthetic public-layout SELF fixtures; no keys or game bytes."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'port/vfs/tests'))
from make_iso9660 import create, both

cli=Path(sys.argv[1]).resolve()
def fixture():
    data=bytearray(800)
    struct.pack_into('>4sIHHIQQ',data,0,b'SCE\0',2,0,1,416,512,544)
    struct.pack_into('>10Q',data,32,3,112,144,208,0,320,0,384,0,0)
    struct.pack_into('>I',data,124,4)
    data[144:160]=b'\x7fELF\x02\x02\x01'+bytes(9)
    struct.pack_into('>HHIQQQIHHHHHH',data,160,2,21,1,0x20000,64,0,0,64,56,2,0,0,0)
    for i in range(2):
        struct.pack_into('>II6Q',data,208+i*56,1,6 if i else 5,512 if i else 256,
                         0x20000 if i else 0x10000,0,32 if i else 8,64 if i else 8,4)
        struct.pack_into('>QQIIQ',data,320+i*32,768 if i else 512,32 if i else 8,1,0,0)
    struct.pack_into('>II',data,768,0x10000,0x20010)
    return data
cases=0
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp); create(root/'base.iso'); original=(root/'base.iso').read_bytes()
    marker=original.index(b'EBOOT.BIN;1')-33
    def check(name,payload,*expected):
        global cases
        disc=bytearray(original)
        disc[marker+10:marker+18]=both(len(payload),4)
        disc[351*2048:351*2048+len(payload)]=payload
        path=root/(name+'.iso'); path.write_bytes(disc)
        result=subprocess.check_output([str(cli),str(path)],text=True,timeout=15)
        for field in (*expected,'executable_code_executed=false','executable_guest_memory_written=false'):
            assert field+'\n' in result,(name,field,result)
        cases+=1
    check('plain',fixture(),'executable_status=self-plain-plan-valid','self_load_segments=2',
          'executable_planned_memory_bytes=72','self_signature_verified=false')
    def mutate(name,offset,fmt,value,reason=None,status='rejected'):
        data=fixture(); struct.pack_into(fmt,data,offset,value)
        expected=['executable_status='+status]
        if reason: expected+=['executable_reason='+reason]
        check(name,data,*expected)
    mutate('encrypted',344,'>Q',2,status='self-decryption-required')
    mutate('completed',344,'>Q',1,status='self-plain-plan-valid')
    mutate('compressed-invalid',336,'>I',2,reason='self-invalid-compressed-segment')
    mutate('version',4,'>I',3,status='self-profile-unsupported')
    mutate('kind',10,'>H',2,status='self-profile-unsupported')
    mutate('ext-version',32,'>Q',0,status='self-profile-unsupported')
    for length in (4,32,64,111):
        check('truncated-'+str(length),fixture()[:length], 'executable_reason=truncated-self-header')
    for name,off,fmt,value,reason in (
        ('header-budget',16,'>Q',1024*1024+1,'self-header-bounds-or-budget'),
        ('header-outside',16,'>Q',801,'self-header-bounds-or-budget'),
        ('elf-budget',24,'>Q',0xffffffffffffffff,'self-header-bounds-or-budget'),
        ('metadata-bounds',12,'>I',0xffffffff,'self-header-bounds-or-budget'),
        ('application-bounds',40,'>Q',0xfffffffffffffff0,'self-public-table-bounds'),
        ('elf-header-bounds',48,'>Q',500,'self-public-table-bounds'),
        ('control-bounds',96,'>Q',65537,'self-public-table-bounds'),
        ('elf-endian',149,'B',1,'self-requires-elf64-big-endian-ppc64-executable'),
        ('ph-count',200,'>H',129,'self-program-or-segment-table-bounds'),
        ('ph-offset',56,'>Q',0xfffffffffffffff0,'self-program-or-segment-table-bounds'),
        ('seg-offset',72,'>Q',0xfffffffffffffff0,'self-program-or-segment-table-bounds'),
        ('payload-overflow',320,'>Q',0xffffffffffffffff,'self-segment-bounds-or-flags'),
        ('payload-in-header',320,'>Q',144,'self-segment-bounds-or-flags'),
        ('payload-outside',328,'>Q',0xffffffffffffffff,'self-segment-bounds-or-flags'),
        ('bad-encryption',344,'>Q',3,'self-segment-bounds-or-flags'),
        ('bad-compression',336,'>I',3,'self-segment-bounds-or-flags'),
        ('size-mismatch',328,'>Q',7,'self-plain-segment-size-mismatch'),
        ('guest-overflow',224,'>Q',0x100000000,'self-load-segment-bounds-or-alignment'),
        ('guest-overlap',280,'>Q',0x10004,'self-overlapping-load-segments'),
        ('bad-entry',168,'>Q',0x30000,'entry-opd-not-file-backed'),
        ('bad-opd',768,'>I',0x30000,'entry-code-or-toc-outside-load-plan'),
    ):
        mutate(name,off,fmt,value,reason)
    data=fixture(); struct.pack_into('>Q',data,96,16); struct.pack_into('>IIQ',data,384,1,16,0)
    check('control',data,'executable_status=self-plain-plan-valid')
    struct.pack_into('>Q',data,392,1)
    check('control-next',data,'executable_reason=self-control-chain-bounds')
    data=fixture(); struct.pack_into('>Q',data,96,144); struct.pack_into('>Q',data,88,368)
    # Control occupies unused header space and must not overwrite the tables.
    # This malformed placement is used only to verify NPD rejection.
    struct.pack_into('>IIQ',data,368,3,144,0)
    check('npd-missing-magic',data,'executable_reason=self-npd-header-invalid')
    # Replace the code segment with an independent zlib stream.
    for name,blob,valid in (
        ('zlib-valid',zlib.compress(bytes(8)),True),
        ('zlib-truncated',zlib.compress(bytes(8))[:-1],False),
        ('zlib-trailing',zlib.compress(bytes(8))+b'x',False),
        ('zlib-too-large',zlib.compress(bytes(9)),False),
        ('zlib-too-small',zlib.compress(bytes(7)),False),
    ):
        data=fixture(); data[512:512+len(blob)]=blob
        struct.pack_into('>Q',data,328,len(blob)); struct.pack_into('>I',data,336,2)
        check(name,data,'executable_status=self-plain-plan-valid' if valid else 'executable_reason=self-invalid-compressed-segment')
    data=fixture(); struct.pack_into('>I',data,336,2); struct.pack_into('>Q',data,240,16*1024*1024+1)
    # Expand logical/guest sizes so the inflate cap is the reason for rejection.
    struct.pack_into('>Q',data,248,16*1024*1024+1); struct.pack_into('>Q',data,24,32*1024*1024)
    struct.pack_into('>Q',data,280,0x3000000)
    check('inflate-budget',data,'executable_reason=self-decompression-budget')
    data=fixture(); code=data[512:520]; opd=data[768:800]
    data.extend(bytes(1312-len(data))); data[384:640]=bytes(256)
    struct.pack_into('>I',data,12,512); struct.pack_into('>Q',data,16,640)
    struct.pack_into('>Q',data,96,144); struct.pack_into('>Q',data,320,1024); struct.pack_into('>Q',data,352,1280)
    struct.pack_into('>IIQ',data,384,3,144,0); data[400:404]=b'NPD\0'; struct.pack_into('>I',data,408,3)
    data[1024:1032]=code; data[1280:1312]=opd
    check('npd-valid',data,'self_npd_license_type=3','executable_status=self-plain-plan-valid')
    check('valid-after-rejections' ,fixture(),'executable_status=self-plain-plan-valid')
print(f'PASS: {cases} SELF cases: plain plan, encrypted/compressed gates, bounded tables, recovery; no guest writes or execution')
