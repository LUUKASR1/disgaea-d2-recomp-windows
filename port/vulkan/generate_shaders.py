"""Compile project-owned GLSL; runtime uses embedded SPIR-V, no SDK required."""
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parent
lines = ['// Generated from project GLSL by generate_shaders.py.', '#pragma once', '#include <cstdint>']
with tempfile.TemporaryDirectory() as directory:
    for stage in ('vert', 'frag'):
        source = root / 'shaders' / ('triangle.' + stage)
        output = Path(directory) / (stage + '.spv')
        subprocess.run(['glslangValidator', '-V', '--target-env', 'vulkan1.1', '-g0', '-o', str(output), str(source)], check=True)
        subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(output)], check=True)
        data = output.read_bytes()
        words = struct.unpack('<' + 'I' * (len(data) // 4), data)
        lines += ['// Source SHA256: ' + hashlib.sha256(source.read_bytes()).hexdigest(),
                  'static constexpr uint32_t triangle_' + stage + '[] = {']
        for offset in range(0, len(words), 8):
            lines.append('    ' + ','.join('0x%08xu' % v for v in words[offset:offset+8]) + ',')
        lines.append('};')
(root / 'triangle_spv.h').write_text('\n'.join(lines) + '\n', encoding='utf-8')
