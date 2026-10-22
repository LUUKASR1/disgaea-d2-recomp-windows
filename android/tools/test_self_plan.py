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
previous_cli=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else None
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
def layout_fixture():
    # Own scaled fixture of the reported topology, never copied game bytes.
    elf=bytearray(2048)
    elf[:16]=b'\x7fELF\x02\x02\x01'+bytes(9)
    struct.pack_into('>HHIQQQIHHHHHH',elf,16,2,21,1,0x20000,64,0,0,64,56,8,0,0,0)
    rows=[(1,5,0,0x10000,832,832,65536),
          (1,6,1024,0x20000,256,4096,1024),
          (1,4,1280,0,0,0,65536),(1,6,1280,0,0,0,65536),
          (1,0x6600006,1280,0,0,0,65536),
          (7,4,1088,0x20040,4,336,8),
          (0x60000001,0,700,0x10000+700,40,40,4),
          (0x60000002,0,740,0x10000+740,64,64,4)]
    for i,(typ,flags,off,va,filesz,memsz,align) in enumerate(rows):
        struct.pack_into('>II6Q',elf,64+56*i,typ,flags,off,va,0,filesz,memsz,align)
    struct.pack_into('>II',elf,1024,0x10200,0x20080)
    data=bytearray(1024)+elf
    struct.pack_into('>4sIHHIQQ',data,0,b'SCE\0',2,28,1,896,1024,len(elf))
    struct.pack_into('>10Q',data,32,3,112,144,208,0,656,0,0,0,0)
    struct.pack_into('>I',data,124,4)
    data[144:208]=elf[:64]; data[208:656]=elf[64:512]
    for i,row in enumerate(rows):
        struct.pack_into('>QQIIQ',data,656+32*i,1024+row[2],row[4],1,0,1 if i<5 else 0)
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
        if len(disc)>len(original):
            disc.extend(bytes((-len(disc))%2048))
            disc[16*2048+80:16*2048+88]=both(len(disc)//2048,4)
        path=root/(name+'.iso'); path.write_bytes(disc)
        result=subprocess.check_output([str(cli),str(path)],text=True,timeout=15)
        for field in (*expected,'executable_code_executed=false','executable_guest_memory_written=false'):
            assert field+'\n' in result,(name,field,result)
        if previous_cli and not name.startswith('layout-'):
            prior=subprocess.check_output([str(previous_cli),str(path)],text=True,stderr=subprocess.PIPE,timeout=15)
            def status(text):
                return next(line for line in text.splitlines() if line.startswith('executable_status='))
            assert status(prior)==status(result),(name,'acceptance changed',prior,result)
        cases+=1
        return result
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
        ('payload-overflow',320,'>Q',0xffffffffffffffff,'self-stored-segment-outside-container'),
        ('payload-in-header',320,'>Q',144,'self-stored-segment-inside-headers'),
        ('payload-outside',328,'>Q',0xffffffffffffffff,'self-stored-segment-outside-container'),
        ('bad-encryption',344,'>Q',3,'self-unsupported-encryption-flag'),
        ('bad-compression',336,'>I',3,'self-unsupported-compression-flag'),
        ('size-mismatch',328,'>Q',7,'self-plain-segment-size-mismatch'),
        ('guest-overflow',224,'>Q',0x100000000,'self-load-address-width'),
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
    for name,offset,value,reason,kind in (
        ('load-file-size',248,7,'filesz-exceeds-memsz','load-layout'),
        ('load-address-width',224,0x100000000,'address-width','load-layout'),
        ('load-budget',248,256*1024*1024+1,'segment-budget','inspection-budget'),
        ('load-align-power',256,3,'alignment-not-power-of-two','load-layout'),
        ('load-align-congruence',256,512,'offset-address-incongruent','load-layout'),
    ):
        data=fixture(); struct.pack_into('>Q',data,offset,value)
        result=check(name,data,'executable_reason=self-load-'+reason,'self_failure_checks='+reason,
                     'self_failure_segment_index=0','self_failure_class='+kind,
                     'self_declared_encrypted_segments=0','self_program_headers=2')
        assert 'self_segment_1=' in result, result  # Full table survives first-row rejection.
    data=fixture(); struct.pack_into('>Q',data,224,0xfffffffc); struct.pack_into('>Q',data,248,8)
    check('load-address-range',data,'executable_reason=self-load-address-range','self_failure_checks=address-range')
    data=fixture(); struct.pack_into('>Q',data,248,7); struct.pack_into('>Q',data,256,512)
    check('multiple-failures',data,'self_failure_checks=filesz-exceeds-memsz,offset-address-incongruent')
    data=fixture(); struct.pack_into('>Q',data,280,0x100000000)
    check('second-row-failure',data,'self_failure_segment_index=1','executable_reason=self-load-address-width')
    result=check('diagnostic-values',fixture(),'executable_status=self-plain-plan-valid',
                 'self_layout_diagnostics=public-numeric-fields-v1','self_container_bytes=800')
    row=dict(item.split(':',1) for item in next(line.split('=',1)[1] for line in result.splitlines() if line.startswith('self_segment_0=')).split(';'))
    assert row==dict(type='1',flags='5',elf_offset='256',virtual_address='65536',file_bytes='8',memory_bytes='8',alignment='4',offset_mod_alignment='0',address_mod_alignment='0',container_offset='512',stored_bytes='8',compression='1',encryption='0'), row
    data=layout_fixture()
    check('layout-eight-programs',data,'executable_status=self-plain-plan-valid',
          'self_empty_load_segments_ignored=3','self_load_segments=2',
          'executable_empty_load_segments_ignored=3','executable_load_segments=2',
          'self_overlap_validation=passed','self_overlap_compared_bytes=512')
    for index in (2,3,4):
        data=layout_fixture(); struct.pack_into('>Q',data,208+index*56+48,3)
        check('layout-empty-invalid-align-'+str(index),data,'executable_reason=self-load-alignment-not-power-of-two')
    data=layout_fixture(); struct.pack_into('>Q',data,208+2*56+40,16)
    check('layout-bss-must-align',data,'executable_reason=self-load-offset-address-incongruent')
    data=layout_fixture(); struct.pack_into('>Q',data,208+2*56+32,4); struct.pack_into('>Q',data,656+2*32+8,4)
    check('layout-empty-with-file-bytes',data,'executable_reason=self-load-filesz-exceeds-memsz')
    for name,off in (('header',1024+10),('program-table',1024+64+4)):
        data=layout_fixture(); data[off]^=1
        check('layout-conflicting-'+name,data,'executable_reason=self-plain-overlap-content-mismatch')
    # Nested record has a second physical copy; both identical and conflicting
    # copies must be checked against the containing LOAD, not only neighbors.
    for index in (5,6,7):
        for conflict in (False,True):
            data=layout_fixture(); seg=656+32*index
            source,length=struct.unpack_from('>QQ',data,seg)
            duplicate=data[source:source+length]
            if conflict: duplicate[-1]^=1
            struct.pack_into('>Q',data,seg,len(data)); data.extend(duplicate)
            check('layout-nested-'+str(index)+'-'+str(conflict),data,
                  'executable_reason=self-plain-overlap-content-mismatch' if conflict else 'executable_status=self-plain-plan-valid')
    data=layout_fixture(); payload=zlib.compress(data[1024:1024+832])
    struct.pack_into('>QQI',data,656,len(data),len(payload),2); data.extend(payload)
    check('layout-compressed-overlap',data,'executable_status=self-plain-plan-valid','self_decompressed_segments=1')
    data=layout_fixture(); payload=zlib.compress(data[1024:1024+832])
    struct.pack_into('>QQI',data,656,len(data),len(payload),2); data.extend(payload); data[144+10]^=1
    check('layout-compressed-header-conflict',data,'executable_reason=self-plain-overlap-content-mismatch')
    # Oversized identical duplicate must hit the comparison cap before I/O.
    data=layout_fixture(); size=4*1024*1024+1
    data.extend(bytes(1024+size-len(data)))
    struct.pack_into('>Q',data,24,size); struct.pack_into('>Q',data,168,0x800000)
    struct.pack_into('>QQ',data,208+32,size,size); struct.pack_into('>Q',data,656+8,size)
    struct.pack_into('>Q',data,208+56+16,0x800000)
    struct.pack_into('>II6Q',data,208+2*56,0x60000003,4,0,0,0,size,size,1)
    struct.pack_into('>QQIIQ',data,656+2*32,len(data),size,1,0,1)
    data[1024:1088]=data[144:208]; data[1088:1536]=data[208:656]
    struct.pack_into('>II',data,2048,0x10200,0x800080)
    data.extend(data[1024:1024+size])
    check('layout-overlap-budget',data,'executable_reason=self-overlap-validation-budget')
    check('valid-after-rejections' ,fixture(),'executable_status=self-plain-plan-valid')
print(f'PASS: {cases} SELF cases: plain plan, encrypted/compressed gates, bounded tables, recovery; no guest writes or execution')
