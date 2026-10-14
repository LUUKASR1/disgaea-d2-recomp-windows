#include "temporal_probe.h"
#include <cstdio>
#include <d3dcompiler.h>
#include <cstring>
#include <stdexcept>
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    auto reads=d2_constant_reads("dcl_constantbuffer cb0[514], immediateIndexed\n"
        "dp4 o0.x, v0, cb0[256]\ndp4 o0.y, v0, cb0[257]\n"
        "dp4 o0.z, v0, cb0[258]\ndp4 o0.w, v0, cb0[259]\nadd r0, cb0[256], cb0[513]\n");
    require(reads.count==5 && !reads.indirect,"Read sets must skip declarations and deduplicate");
    require(reads.used[256]&&reads.used[257]&&reads.used[258]&&reads.used[259]&&reads.used[513],"Matrix and viewport rows retained");
    require(!reads.used[0],"Unreferenced row is not a camera guess");
    auto dynamic=d2_constant_reads("mov r0, cb0[r1.x + 32]\nmov r1, cb0[0]\n");
    require(dynamic.indirect && dynamic.count==1 && dynamic.used[0],"Indirect reads must be reported");
    require(d2_constant_reads(nullptr).count==0,"Missing shader has no invented constants");
    require(d2_constant_reads("mov r0, cb1[12]\n").count==0,"FP bank cannot be classified as VP bank");
    const char* shader="cbuffer C:register(b0){float4 vp_c[514];} float4 main(float4 p:POSITION):SV_POSITION { return float4(dot(p,vp_c[256]),dot(p,vp_c[257]),dot(p,vp_c[258]),dot(p,vp_c[259]));}";
    ID3DBlob *code=nullptr,*error=nullptr,*assembly=nullptr;
    HRESULT compiled=D3DCompile(shader,std::strlen(shader),"temporal probe fixture",nullptr,nullptr,"main","vs_5_0",0,0,&code,&error);
    if(error)error->Release();
    require(SUCCEEDED(compiled)&&code,"Actual vertex shader compilation");
    HRESULT decoded=D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,&assembly);
    code->Release();
    require(SUCCEEDED(decoded)&&assembly,"Actual DXBC disassembly");
    auto actual=d2_constant_reads(static_cast<const char*>(assembly->GetBufferPointer()));assembly->Release();
    require(actual.count==4 && actual.used[256] && actual.used[257] && actual.used[258] && actual.used[259] && !actual.indirect,
        "Actual DXBC matrix row reads match upload buffer layout");
    puts("PASS temporal constant discovery: literal/indirect reads, bounds and no matrix assumptions");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
