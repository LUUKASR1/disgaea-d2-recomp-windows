"""Metadata cache must not contain extracted game assets, even on retry."""
from pathlib import Path
import subprocess
import sys
import tempfile

probe, fixtures = sys.argv[1], Path(sys.argv[2])
with tempfile.TemporaryDirectory(prefix='d2-iso-metadata-') as temporary:
    root = Path(temporary) / 'metadata'
    for _ in range(2):
        subprocess.run([probe, '--metadata-root', str(root), str(fixtures / 'hybrid-fallback.iso')], check=True)
        assert [p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()] == ['PS3_GAME/PARAM.SFO']
        assert (root / 'PS3_GAME/PARAM.SFO').read_bytes().startswith(b'\0PSF')
    (root / 'PS3_GAME/PARAM.SFO').write_bytes(b'Preserve different user metadata')
    result = subprocess.run([probe, '--metadata-root', str(root), str(fixtures / 'hybrid-fallback.iso')])
    assert result.returncode != 0
    assert (root / 'PS3_GAME/PARAM.SFO').read_bytes() == b'Preserve different user metadata'
print('Metadata-only cache, retry and preservation passed')
