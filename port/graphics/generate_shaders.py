"""Embed unchanged, hash-verified official FSR 1 headers into GPU shader sources."""
from pathlib import Path
import hashlib
import sys

source, output = map(Path, sys.argv[1:])
vendor = source / 'vendor/fsr1'
parts = []
for name, expected in [('ffx_a.h', 'f60e2722fcd13989523b9164d776ab382b3692791767f3bf8bb19967f763f3fb'),
                       ('ffx_fsr1.h', '93c3922362ea7fc99cbcc698ca30c98de4f8c246d1fbb0b09e015ddef38ce3a5')]:
    data = (vendor / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != expected: raise RuntimeError('FSR header drift: ' + name)
    parts.append(data.decode())
common = '''
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);
SamplerState clampSampler : register(s0);
cbuffer Parameters : register(b0) { uint4 con0, con1, con2, con3; uint2 outputSize; uint2 unused; };
'''
easu = ('#define A_GPU 1\n#define A_HLSL 1\n#define FSR_EASU_F 1\n'+parts[0]+common+
    '''AF4 FsrEasuRF(AF2 p) { return src.GatherRed(clampSampler,p); }
AF4 FsrEasuGF(AF2 p) { return src.GatherGreen(clampSampler,p); }
AF4 FsrEasuBF(AF2 p) { return src.GatherBlue(clampSampler,p); }
'''+parts[1]+'''[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
if(any(p.xy>=outputSize))return; AF3 rgb; FsrEasuF(rgb,p.xy,con0,con1,con2,con3); dst[p.xy]=float4(rgb,1);
}''')
rcas = ('#define A_GPU 1\n#define A_HLSL 1\n#define FSR_RCAS_F 1\n'+parts[0]+common+
    '''AF4 FsrRcasLoadF(ASU2 p) { return src.Load(int3(clamp(p,int2(0,0),int2(outputSize)-1),0)); }
void FsrRcasInputF(inout AF1 r,inout AF1 g,inout AF1 b) {}
'''+parts[1]+'''[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
if(any(p.xy>=outputSize))return; AF1 r,g,b; FsrRcasF(r,g,b,p.xy,con0); dst[p.xy]=float4(r,g,b,1);
}''')
bilinear = common+'''[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
if(any(p.xy>=outputSize))return; dst[p.xy]=src.SampleLevel(clampSampler,(float2(p.xy)+0.5)/float2(outputSize),0);
}'''
output.write_text('#pragma once\n'+''.join('static const char '+name+'[]=R"D2SHADER('+text+')D2SHADER";\n'
    for name,text in [('d2_easu',easu),('d2_rcas',rcas),('d2_bilinear',bilinear)]), encoding='utf-8')
