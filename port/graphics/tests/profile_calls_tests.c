#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <string.h>
#include <stdio.h>
#include "frame_profile_state.h"

static int enabled, clock_calls, resource_calls, pso_calls, compile_calls, bad_arguments;
static HRESULT response = S_OK;
static ID3D12Device device;
static D3D12_HEAP_PROPERTIES heap;
static D3D12_RESOURCE_DESC resource;
static D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline;
static IID iid;
static void* output;
static int d2_lite_enabled(void) { return enabled; }
static double d2_lite_now(void) { return ++clock_calls * .001; }

static HRESULT STDMETHODCALLTYPE create_resource(ID3D12Device* self,
    const D3D12_HEAP_PROPERTIES* hp, D3D12_HEAP_FLAGS flags,
    const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES state,
    const D3D12_CLEAR_VALUE* clear, REFIID requested, void** result)
{
    resource_calls++;
    if(self!=&device || hp!=&heap || flags!=D3D12_HEAP_FLAG_NONE || desc!=&resource ||
       state!=D3D12_RESOURCE_STATE_COPY_DEST || clear || requested!=&iid || result!=&output) bad_arguments++;
    *result = (void*)123;
    return response;
}
static HRESULT STDMETHODCALLTYPE create_pso(ID3D12Device* self,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID requested, void** result)
{
    pso_calls++;
    if(self!=&device || desc!=&pipeline || requested!=&iid || result!=&output) bad_arguments++;
    return response;
}
static HRESULT test_compile(LPCVOID data, SIZE_T size, LPCSTR source,
    const D3D_SHADER_MACRO* macros, ID3DInclude* include, LPCSTR entry,
    LPCSTR target, UINT flags1, UINT flags2, ID3DBlob** code, ID3DBlob** errors)
{
    compile_calls++;
    if(size!=3 || memcmp(data,"abc",3) || strcmp(source,"test") || macros || include ||
       strcmp(entry,"main") || strcmp(target,"ps_5_0") || flags1!=1 || flags2!=2 || code || errors) bad_arguments++;
    return response;
}
#define D3DCompile test_compile
#include "frame_profile_calls.h"
#undef D3DCompile

static int invoke(void)
{
    if(d2_profile_create_resource(&device,&heap,D3D12_HEAP_FLAG_NONE,&resource,
        D3D12_RESOURCE_STATE_COPY_DEST,NULL,&iid,&output)!=response || output!=(void*)123) return 1;
    if(d2_profile_create_pso(&device,&pipeline,&iid,&output)!=response) return 2;
    if(d2_profile_compile("abc",3,"test",NULL,NULL,"main","ps_5_0",1,2,NULL,NULL)!=response) return 3;
    return bad_arguments ? 4 : 0;
}
int main(void)
{
    static ID3D12DeviceVtbl table;
    table.CreateCommittedResource=create_resource;
    table.CreateGraphicsPipelineState=create_pso;
    device.lpVtbl=&table;
    resource.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    if(invoke() || clock_calls || d2_profile_buffers || d2_profile_shader_calls || d2_profile_pso_calls) return 1;
    enabled=1;
    if(invoke()) return 2;
    resource.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    response=E_OUTOFMEMORY;
    if(invoke()) return 3;
    if(resource_calls!=3 || pso_calls!=3 || compile_calls!=3 || clock_calls!=12 ||
       d2_profile_buffers!=1 || d2_profile_textures!=1 || d2_profile_resource_failures!=1 ||
       d2_profile_shader_calls!=2 || d2_profile_pso_calls!=2 || d2_profile_pso_failures!=1) return 4;
    if(d2_profile_resource_ms<1.99 || d2_profile_resource_ms>2.01 ||
       d2_profile_shader_ms<1.99 || d2_profile_shader_ms>2.01 ||
       d2_profile_pso_ms<1.99 || d2_profile_pso_ms>2.01) return 5;
    puts("PASS: profiling wrappers preserve API arguments, HRESULTs, output and disabled clock behavior");
    return 0;
}
