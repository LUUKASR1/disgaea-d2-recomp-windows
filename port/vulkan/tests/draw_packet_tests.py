"""Exercise the actual reader, GPU comparison and stale-proof rejection."""
import json
from pathlib import Path
import subprocess
import struct
import math
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
flags = ['--validation'] if '--validation' in sys.argv[2:] else []
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    output = root / 'fixture'
    run = subprocess.run([exe, *flags, '--self-test', '--output', str(output)], capture_output=True, text=True)
    if run.returncode == 77:
        sys.exit(77)
    assert run.returncode == 0, run.stdout + run.stderr
    packet = output / 'synthetic-input'
    report = output / 'draw-vulkan-report.json'
    assert json.loads(report.read_text())['status'] == 'exact-match'
    reference = packet / 'reference.rgba'
    original = reference.read_bytes()
    changed = bytearray(original); changed[1] = 255
    reference.write_bytes(changed)
    def check(expected, status):
        result = subprocess.run([exe, *flags, '--input', str(packet), '--output', str(output)], capture_output=True, text=True)
        assert result.returncode == expected, result.stdout + result.stderr
        assert json.loads(report.read_text())['status'] == status
    check(2, 'pixel-mismatch')
    reference.write_bytes(bytes(len(original)))
    check(2, 'empty-reference')
    reference.write_bytes(original)
    header = packet / 'packet.bin'; saved = header.read_bytes()
    header.write_bytes(saved[:20])
    check(1, 'failed')
    assert not (output / 'output.bmp').exists(), 'stale proof survived invalid input'
    header.write_bytes(saved)
    (packet / 'draw.json').write_text('{"status":"rejected"}')
    check(1, 'failed')
    (packet / 'draw.json').write_text('{"status":"captured"}')
    # Same GPU path, now exercise the captured blend operation and sampler ABI.
    data = list(struct.unpack('<32I', saved))
    data[9] = 1  # blend enabled, default SRC_ALPHA / INV_SRC_ALPHA
    header.write_bytes(struct.pack('<32I', *data))
    (packet / 'fragment-constants.bin').write_bytes(struct.pack('<4f', 1, 1, 1, .5))
    reference.write_bytes(bytes([128, 0, 0, 128]) * (64 * 48))
    check(0, 'exact-match')
    data[12] = 3  # reverse subtract for color only
    header.write_bytes(struct.pack('<32I', *data))
    reference.write_bytes(bytes([0, 0, 0, 128]) * (64 * 48))
    check(0, 'exact-match')
    header.write_bytes(saved)
    (packet / 'fragment-constants.bin').write_bytes(struct.pack('<4f', 1, 1, 1, 1))
    fp = packet / 'fp.hlsl'
    fp.write_text(fp.read_text().replace('rsx_tex[0]', 'rsx_tex[1]').replace('rsx_samp[0]', 'rsx_samp[1]'))
    (packet / 'tex1.rgba').write_bytes(bytes([255, 0, 0, 255, 0, 0, 255, 255]))
    data = list(struct.unpack('<32I', saved)); data[22] = 2  # texture_width[1]
    for clamp in (False, True):
        data[20] = int(clamp)
        header.write_bytes(struct.pack('<32I', *data))
        oracle = bytearray()
        for y in range(48):
            for x in range(64):
                position = (x + .5) / 64 * 2 - .5
                left = math.floor(position); fraction = position - left
                def red(i):
                    i = max(0, min(1, i)) if clamp else i % 2
                    return 255 if i == 0 else 0
                r = round(red(left) * (1 - fraction) + red(left + 1) * fraction)
                oracle.extend((r, 0, 255 - r, 255))
        reference.write_bytes(oracle)
        check(0, 'exact-match')
    # A reflected extra output must be ignored only to mirror the actual
    # captured single-target D3D binding, never by silent ABI relaxation.
    (packet / 'tex1.rgba').write_bytes(bytes(4))
    fp.write_text('cbuffer C:register(b1){float4 color;}struct Out{float4 a:SV_TARGET0;float4 b:SV_TARGET2;};Out main(){Out o;o.a=float4(1,0,0,1)*color;o.b=float4(0,1,0,1);return o;}')
    data = list(struct.unpack('<32I', saved)); data[29] = 5
    header.write_bytes(struct.pack('<32I', *data)); reference.write_bytes(original)
    check(0, 'exact-match')
    assert json.loads(report.read_text())['discarded_shader_output_mask'] == 4
    data[29] = 1; header.write_bytes(struct.pack('<32I', *data))
    check(1, 'failed')  # Cannot pretend the extra shader output was absent.
    data[29] = 5; header.write_bytes(struct.pack('<32I', *data))
    reference.unlink()
    check(1, 'failed')
print('PASS draw replay: exact pixels, mismatch, empty proof, bounds, completion, blend operations, linear wrap/clamp, missing input')
