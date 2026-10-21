"""Native descriptor VFS integration; synthetic images only, no game files."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'port/vfs/tests'))
from make_fixture import make
from make_iso9660 import generate

cli=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as temporary:
    root=Path(temporary)
    make(root/'complete.iso',complete=True)
    generate(root)
    def probe(name,base=None,length=None):
        args=[str(cli),str(root/name)]
        if base is not None: args += [str(base),str(length)]
        return subprocess.check_output(args,text=True,timeout=15)
    def expect(name,*lines,base=None,length=None):
        result=probe(name,base,length)
        for line in lines: assert line+'\n' in result,(name,line,result)
    expect('complete.iso','disc_backend=UDF','disc_title_id=BLUS31313',
           'disc_required_files_checked=18','disc_required_files_failed=0','disc_status=base-files-readable')
    expect('iso9660-complete.iso','disc_backend=ISO9660','disc_title_id=BLUS31313','disc_status=base-files-readable')
    data=(root/'complete.iso').read_bytes()
    (root/'slice.bin').write_bytes(b'prefix'*7+data+b'suffix')
    expect('slice.bin','disc_backend=UDF','disc_status=base-files-readable',base=42,length=len(data))
    expect('slice.bin','document_status=rejected',base=42,length=len(data)+100)
    make(root/'partial.iso')
    expect('partial.iso','disc_status=missing-or-unreadable-base-files')
    wrong=data.replace(b'BLUS31313\0',b'BLUS00000\0')
    (root/'wrong.iso').write_bytes(wrong)
    expect('wrong.iso','disc_status=other-title')
    (root/'truncated.iso').write_bytes(data[:40*2048])
    expect('truncated.iso','disc_status=rejected')
    (root/'plain.txt').write_text('D2 descriptor test')
    expect('plain.txt','disc_status=not-disc','document_status=readable')
    print('PASS: descriptor UDF/ISO9660, 18 base files, slices, wrong title, missing files, truncation and plain documents')
