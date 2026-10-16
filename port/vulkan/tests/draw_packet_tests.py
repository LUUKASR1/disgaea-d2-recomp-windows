"""Exercise the actual reader, GPU comparison and stale-proof rejection."""
import json
from pathlib import Path
import subprocess
import struct
import math
import sys
import tempfile
import shutil

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
    fixture_shader = (packet / 'fp.hlsl').read_text()
    assert json.loads(report.read_text())['status'] == 'exact-match'
    reference = packet / 'reference.rgba'
    original = reference.read_bytes()
    changed = bytearray(original); changed[1] = 255
    reference.write_bytes(changed)
    def check(expected, status):
        result = subprocess.run([exe, *flags, '--input', str(packet), '--output', str(output)], capture_output=True, text=True)
        assert result.returncode == expected, result.stdout + result.stderr
        result_report = json.loads(report.read_text())
        assert result_report['status'] == status
        assert result_report.get('validation_errors', 0) == 0
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
    data = list(struct.unpack('<40I', saved))
    data[9] = 1  # blend enabled, default SRC_ALPHA / INV_SRC_ALPHA
    header.write_bytes(struct.pack('<40I', *data))
    (packet / 'fragment-constants.bin').write_bytes(struct.pack('<4f', 1, 1, 1, .5))
    reference.write_bytes(bytes([128, 0, 0, 128]) * (64 * 48))
    check(0, 'exact-match')
    data[12] = 3  # reverse subtract for color only
    header.write_bytes(struct.pack('<40I', *data))
    reference.write_bytes(bytes([0, 0, 0, 128]) * (64 * 48))
    check(0, 'exact-match')
    header.write_bytes(saved)
    (packet / 'fragment-constants.bin').write_bytes(struct.pack('<4f', 1, 1, 1, 1))
    fp = packet / 'fp.hlsl'
    fp.write_text(fp.read_text().replace('rsx_tex[0]', 'rsx_tex[1]').replace('rsx_samp[0]', 'rsx_samp[1]'))
    (packet / 'tex1.rgba').write_bytes(bytes([255, 0, 0, 255, 0, 0, 255, 255]))
    data = list(struct.unpack('<40I', saved)); data[22] = 2  # texture_width[1]
    for clamp in (False, True):
        data[20] = int(clamp)
        header.write_bytes(struct.pack('<40I', *data))
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
    data = list(struct.unpack('<40I', saved)); data[29] = 5
    header.write_bytes(struct.pack('<40I', *data)); reference.write_bytes(original)
    check(0, 'exact-match')
    assert json.loads(report.read_text())['discarded_shader_output_mask'] == 4
    data[29] = 1; header.write_bytes(struct.pack('<40I', *data))
    check(1, 'failed')  # Cannot pretend the extra shader output was absent.
    data[29] = 5; header.write_bytes(struct.pack('<40I', *data))
    # Native BC blocks stay compressed; compare decoded colors/alpha and
    # component selectors through the actual Vulkan image view and shader.
    fp.write_text('Texture2D rsx_tex[16]:register(t0);SamplerState rsx_samp[16]:register(s0);cbuffer Color:register(b1){float4 color;}struct In{float4 uv:TEXCOORD0;};float4 main(In i):SV_TARGET{return rsx_tex[0].Sample(rsx_samp[0],i.uv.xy)*color;}')
    for fmt, block in ((1, bytes.fromhex('00f8000000000000')),
                       (2, bytes([255])*8 + bytes.fromhex('00f8000000000000')),
                       (3, bytes([255,0])+bytes(6)+bytes.fromhex('00f8000000000000'))):
        data = list(struct.unpack('<40I', saved)); data[21] = 5; data[25] = 7
        data[32] = fmt; data[36] = 0x688
        header.write_bytes(struct.pack('<40I', *data))
        (packet / 'tex0.bc').write_bytes(block * 4)
        reference.write_bytes(bytes([255,0,0,255])*(64*48))
        check(0, 'exact-match')
        data[36] = 2 | (1<<3) | (0<<6) | (5<<9) # BGRA plus forced alpha 1
        header.write_bytes(struct.pack('<40I', *data))
        reference.write_bytes(bytes([0,0,255,255])*(64*48))
        check(0, 'exact-match')
        (packet / 'tex0.bc').write_bytes(block)
        check(1, 'failed') # Truncated two-by-two block grid.
    data[32] = 4; header.write_bytes(struct.pack('<40I', *data))
    check(1, 'failed')
    data[32] = 1; data[36] = 7; header.write_bytes(struct.pack('<40I', *data))
    (packet / 'tex0.bc').write_bytes(bytes.fromhex('00f8000000000000')*4)
    check(1, 'failed')
    # Both older packet versions remain readable with pre-remapped RGBA.
    fp.write_text('float4 main():SV_TARGET0{return float4(1,0,0,1);}')
    reference.write_bytes(original)
    for version in (1,2):
        legacy = list(struct.unpack('<40I', saved))[:32]; legacy[1] = version
        if version == 1: legacy[29] = 0
        header.write_bytes(struct.pack('<32I', *legacy))
        check(0, 'exact-match')
    header.write_bytes(saved)
    # True shared-attachment sequence: different textures/constants/geometry,
    # occlusion across submissions and blending over retained color.
    seq = root / 'sequence'
    seq.mkdir()
    for index in range(2):
        draw = seq / f'draw_{index:03d}'
        shutil.copytree(packet, draw)
        (draw / 'draw.json').write_text(json.dumps({'status':'captured','frame':42,'draw':index}, separators=(',',':')))
        (draw / 'fp.hlsl').write_text(fixture_shader)
        fields = list(struct.unpack('<40I', saved)); fields[6]=fields[7]=fields[9]=1
        (draw / 'packet.bin').write_bytes(struct.pack('<40I', *fields))
    second=seq/'draw_001'
    (second/'tex0.rgba').write_bytes(bytes([0,0,255,255]))
    (second/'fragment-constants.bin').write_bytes(struct.pack('<4f',1,1,1,.5))
    vertices=bytearray((second/'vertices.bin').read_bytes())
    def move_depth(z):
        for j in range(6):struct.pack_into('<f',vertices,j*256+8,z)
        (second/'vertices.bin').write_bytes(vertices)
    (seq/'sequence.json').write_text('{"status":"captured"}')
    seq_header=struct.pack('<6IQ4I',0x31535144,1,64,48,2,0,42,0,1,0,0)
    (seq/'sequence.bin').write_bytes(seq_header)
    def sequence_check(expected,status):
        r=subprocess.run([exe,*flags,'--sequence','--input',str(seq),'--output',str(output)],capture_output=True,text=True)
        assert r.returncode==expected,r.stdout+r.stderr
        result=json.loads((output/'sequence-vulkan-report.json').read_text())
        assert result['status']==status
        if expected==0:assert result['selected_draw_count']==2
        assert result['full_frame_equivalence_tested'] is False
        assert result.get('validation_errors',0)==0
    move_depth(.75);(seq/'sequence-reference.rgba').write_bytes(original)
    sequence_check(0,'exact-match') # Hidden blue cannot erase red/depth.
    move_depth(.25);(seq/'sequence-reference.rgba').write_bytes(bytes([127,0,128,255])*(64*48))
    sequence_check(0,'exact-match') # Front alpha blends over prior red.
    (second/'draw.json').write_text('{"status":"captured","frame":43,"draw":1}')
    sequence_check(1,'failed')
    assert not (output/'output.bmp').exists()
    (second/'draw.json').write_text('{"status":"captured","frame":42,"draw":1}')
    bad=bytearray(seq_header);struct.pack_into('<I',bad,36,0) # Duplicate IDs.
    (seq/'sequence.bin').write_bytes(bad);sequence_check(1,'failed')
    (seq/'sequence.bin').write_bytes(seq_header)
    bad=list(struct.unpack('<40I',(second/'packet.bin').read_bytes()));bad[16]=3
    (second/'packet.bin').write_bytes(struct.pack('<40I',*bad));sequence_check(1,'failed')
    # v2 permits different shaders and raster state on retained attachments.
    values=list(struct.unpack('<6IQ4I',seq_header));values[1]=2
    mixed_header=struct.pack('<6IQ8I',*(values+[0,0,0,0]))
    (seq/'sequence.bin').write_bytes(mixed_header)
    fields=list(struct.unpack('<40I',(second/'packet.bin').read_bytes()));fields[16]=1
    (second/'packet.bin').write_bytes(struct.pack('<40I',*fields))
    (second/'fp.hlsl').write_text('cbuffer C:register(b1){float4 color;}float4 main():SV_TARGET0{return float4(0,1,0,1)*color;}')
    (seq/'sequence-reference.rgba').write_bytes(bytes([127,128,0,255])*(64*48))
    sequence_check(0,'exact-match')
    mixed_report=json.loads((output/'sequence-vulkan-report.json').read_text())
    assert mixed_report['pipeline_switches']==1 and mixed_report['sequence_version']==2
    assert mixed_report['coverage_scope']=='selected-mixed-pipeline-subset-only'
    assert mixed_report['adapter']
    fields[19]=1 # New PSO writes red only; retain other channels.
    (second/'packet.bin').write_bytes(struct.pack('<40I',*fields))
    (seq/'sequence-reference.rgba').write_bytes(bytes([127,0,0,255])*(64*48))
    sequence_check(0,'exact-match')
    fields[19]=15;fields[8]=5 # GREATER: .25 must fail against prior .5.
    (second/'packet.bin').write_bytes(struct.pack('<40I',*fields))
    (seq/'sequence-reference.rgba').write_bytes(original)
    sequence_check(0,'exact-match')
    fields[16]=99;(second/'packet.bin').write_bytes(struct.pack('<40I',*fields))
    sequence_check(1,'failed')
    # Bound of eight draws, with unused slots and identity checks retained.
    for index in range(1,8):
        target=seq/f'draw_{index:03d}'
        if target.exists():shutil.rmtree(target)
        shutil.copytree(seq/'draw_000',target)
        (target/'draw.json').write_text(json.dumps({'status':'captured','frame':42,'draw':index},separators=(',',':')))
    (seq/'sequence.bin').write_bytes(struct.pack('<6IQ8I',0x31535144,2,64,48,8,0,42,*range(8)))
    (seq/'sequence-reference.rgba').write_bytes(original)
    r=subprocess.run([exe,*flags,'--sequence','--input',str(seq),'--output',str(output)],capture_output=True,text=True)
    assert r.returncode==0,r.stdout+r.stderr
    report8=json.loads((output/'sequence-vulkan-report.json').read_text())
    assert report8['selected_draw_count']==8 and report8['validation_errors']==0 and report8['status']=='exact-match'
    reference.unlink()
    check(1, 'failed')
print('PASS draw replay: exact pixels, mismatch, empty proof, bounds, completion, blend operations, linear wrap/clamp, native BC1/2/3, component remap, legacy packets, shared-depth/color sequences, mixed shaders/raster/masks/compare, identity/order rejection, missing input')
