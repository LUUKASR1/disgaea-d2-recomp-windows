#include "graphics.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#define A_CPU 1
#include "vendor/fsr1/ffx_a.h"
#include "vendor/fsr1/ffx_fsr1.h"
#include "fsr_shaders.h"
#include "texture_pack.h"

namespace {
template<class T> struct Ref {
    T* p=nullptr;
    Ref()=default; Ref(const Ref&)=delete; Ref& operator=(const Ref&)=delete;
    ~Ref(){if(p)p->Release();}
    T** put(){if(p)p->Release();p=nullptr;return &p;}
    T* operator->()const{return p;}
};
struct Config {UINT gw=1280,gh=720,rw=1280,rh=720,ow=1280,oh=720;bool active=false,fsr=false,post=false,hdr=false;float sharpness=0.2f;} cfg;
// Image filters of the in-game options panel (FXAA, sharpen, scanlines, CRT mask, vignette, vivid colours), set from the UI thread.
float post_values[8]={};std::atomic<int> post_enabled{0};
// HDR (scRGB FP16 swap chain): level 0..3 = how far highlights are expanded; paper/peak come from the display.
std::atomic<int> pause_unfocused{1};std::atomic<int> menu_pause{0};std::atomic<int> hdr_level{0};float hdr_paper_scale=203.0f/80.0f,hdr_peak_nits=1000.0f;bool hdr_display=false;
struct State {
    ID3D12Device* device=nullptr; ID3D12CommandQueue* queue=nullptr;
    std::array<Ref<ID3D12Resource>,2> output;
    std::array<Ref<ID3D12CommandAllocator>,2> allocator;
    Ref<ID3D12GraphicsCommandList> commands;
    Ref<ID3D12Resource> middle,final,post,hdrbuf;
    Ref<ID3D12DescriptorHeap> heap;
    Ref<ID3D12RootSignature> root;
    Ref<ID3D12PipelineState> easu,rcas,bilinear,filters,hdr;
    UINT stride=0;
};
State* gpu=nullptr;
// Screenshots: the final swap buffer is copied to a read-back buffer after the upscaler and filters ran.
std::atomic<int> outline_level{0};std::atomic<int> shot_request{0};std::atomic<unsigned> shot_count{0};std::mutex shot_lock;std::string shot_path;
Ref<ID3D12Fence> shot_fence;HANDLE shot_event=nullptr;UINT64 shot_value=0;
std::string shot_directory(){
    std::string dir;
    if(const char* e=std::getenv("D2_SCREENSHOT_DIR"))dir=e;
    else if(const char* u=std::getenv("USERPROFILE"))dir=std::string(u)+"\\Pictures\\Disgaea D2";
    else if(const char* l=std::getenv("LOCALAPPDATA"))dir=std::string(l)+"\\DisgaeaD2Recomp\\screenshots";
    return dir;
}
float half_to_float(uint16_t h){
    const uint32_t sign=(h>>15)&1u,exp=(h>>10)&31u,man=h&1023u;float v;
    if(exp==0)v=std::ldexp(static_cast<float>(man),-24);else if(exp==31)v=man?0.0f:65504.0f;else v=std::ldexp(static_cast<float>(man|1024u),static_cast<int>(exp)-25);
    return sign?-v:v;
}
// HDR screenshots are saved as ordinary SDR PNGs: scRGB / SDR white, clipped, sRGB encoded.
uint8_t scrgb_to_srgb8(uint16_t h,float scale){
    float v=half_to_float(h)/scale;v=v<0.0f?0.0f:v>1.0f?1.0f:v;
    v=v<=0.0031308f?v*12.92f:1.055f*std::pow(v,1.0f/2.4f)-0.055f;
    return static_cast<uint8_t>(v*255.0f+0.5f);
}
void shot_save(std::vector<uint8_t> rgba,UINT w,UINT h,bool bgra){
    if(bgra)for(size_t i=0;i+3<rgba.size();i+=4)std::swap(rgba[i],rgba[i+2]);
    for(size_t i=3;i<rgba.size();i+=4)rgba[i]=255;
    std::string dir=shot_directory();
    if(dir.empty()){std::fprintf(stderr,"[D2SHOT] no folder for screenshots\n");return;}
    d2tex::make_dirs(dir);
    SYSTEMTIME t;GetLocalTime(&t);
    char name[96];std::snprintf(name,sizeof name,"\\DisgaeaD2_%04u%02u%02u_%02u%02u%02u_%03u.png",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    const std::string path=dir+name;
    if(stbi_write_png(path.c_str(),static_cast<int>(w),static_cast<int>(h),4,rgba.data(),static_cast<int>(w)*4)){
        {std::lock_guard<std::mutex> g(shot_lock);shot_path=path;}
        shot_count.fetch_add(1);
        std::fprintf(stderr,"[D2SHOT] saved %s (%ux%u)\n",path.c_str(),w,h);
    }else std::fprintf(stderr,"[D2SHOT] could not write %s\n",path.c_str());
}
void checked(HRESULT hr,const char* what){if(FAILED(hr))throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(static_cast<unsigned long>(hr)));}
UINT dimension(const char* key,UINT fallback){const char* s=std::getenv(key);if(!s)return fallback;char* end=nullptr;auto n=std::strtoul(s,&end,10);if(!*s||*end||n<320||n>4096)throw std::runtime_error("Invalid graphics dimension");return static_cast<UINT>(n);}
void texture(ID3D12Resource** out,UINT w,UINT h,D3D12_RESOURCE_FLAGS flags,DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
    d.DepthOrArraySize=1;d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;d.Flags=flags;
    checked(gpu->device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,
        IID_ID3D12Resource,reinterpret_cast<void**>(out)),"Graphics texture");
}
void transition(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;
    b.Transition.StateBefore=before;b.Transition.StateAfter=after;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    gpu->commands->ResourceBarrier(1,&b);
}
void pipeline(const char* shader,Ref<ID3D12PipelineState>& out){
    Ref<ID3DBlob> code,error;
    HRESULT hr=D3DCompile(shader,std::strlen(shader),"D2 FSR",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.put(),error.put());
    if(FAILED(hr)&&error.p)std::fprintf(stderr,"[D2GRAPHICS] shader: %s\n",static_cast<char*>(error->GetBufferPointer()));
    checked(hr,"Graphics shader compilation");
    D3D12_COMPUTE_PIPELINE_STATE_DESC d{};d.pRootSignature=gpu->root.p;d.CS={code->GetBufferPointer(),code->GetBufferSize()};
    checked(gpu->device->CreateComputePipelineState(&d,IID_ID3D12PipelineState,reinterpret_cast<void**>(out.put())),"Graphics compute pipeline");
}
void descriptors(UINT index,ID3D12Resource* input,ID3D12Resource* output,DXGI_FORMAT uav_format=DXGI_FORMAT_R8G8B8A8_UNORM){
    auto cpu=gpu->heap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=index*gpu->stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    gpu->device->CreateShaderResourceView(input,&srv,cpu);cpu.ptr+=gpu->stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=uav_format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    gpu->device->CreateUnorderedAccessView(output,nullptr,&uav,cpu);
}
// Full-screen filters (same maths as the Vulkan post pass, without FSR). Reads t0, writes u0, 20 root constants:
// pp[0] = (width, height, width, height), pp[1] = (sharpen, scanlines, CRT mask, vignette), pp[2] = (saturation, contrast, fxaa, 0).
const char* d2_post_cs = R"HLSL(
cbuffer PostParams : register(b0) { float4 pp[5]; };
Texture2D<float4> scene : register(t0);
RWTexture2D<float4> result : register(u0);
SamplerState smp : register(s0);
float3 fxaa(float2 uv, float2 texel) {
    float3 lw = float3(0.299, 0.587, 0.114);
    float3 rgbNW = scene.SampleLevel(smp, uv + float2(-1.0, -1.0) * texel, 0).rgb;
    float3 rgbNE = scene.SampleLevel(smp, uv + float2( 1.0, -1.0) * texel, 0).rgb;
    float3 rgbSW = scene.SampleLevel(smp, uv + float2(-1.0,  1.0) * texel, 0).rgb;
    float3 rgbSE = scene.SampleLevel(smp, uv + float2( 1.0,  1.0) * texel, 0).rgb;
    float3 rgbM = scene.SampleLevel(smp, uv, 0).rgb;
    float lNW = dot(rgbNW, lw), lNE = dot(rgbNE, lw), lSW = dot(rgbSW, lw), lSE = dot(rgbSE, lw), lM = dot(rgbM, lw);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    float2 dir = float2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float dirReduce = max((lNW + lNE + lSW + lSE) * (0.25 * (1.0 / 8.0)), 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -8.0, 8.0) * texel;
    float3 rgbA = 0.5 * (scene.SampleLevel(smp, uv + dir * (1.0 / 3.0 - 0.5), 0).rgb + scene.SampleLevel(smp, uv + dir * (2.0 / 3.0 - 0.5), 0).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (scene.SampleLevel(smp, uv + dir * -0.5, 0).rgb + scene.SampleLevel(smp, uv + dir * 0.5, 0).rgb);
    float lB = dot(rgbB, lw);
    return (lB < lMin || lB > lMax) ? rgbA : rgbB;
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)pp[0].z || id.y >= (uint)pp[0].w) return;
    float2 uv = (float2(id.xy) + 0.5) / pp[0].zw;
    float2 texel = 1.0 / pp[0].xy;
    float sharpen = pp[1].x, scan = pp[1].y, crt = pp[1].z, vig = pp[1].w, sat = pp[2].x, con = pp[2].y, aa = pp[2].z;
    float3 c = (aa > 0.0) ? fxaa(uv, texel) : scene.SampleLevel(smp, uv, 0).rgb;
    if (sharpen > 0.0) {
        float3 n = scene.SampleLevel(smp, uv + float2(0.0, -texel.y), 0).rgb;
        float3 s = scene.SampleLevel(smp, uv + float2(0.0, texel.y), 0).rgb;
        float3 e = scene.SampleLevel(smp, uv + float2(texel.x, 0.0), 0).rgb;
        float3 w = scene.SampleLevel(smp, uv + float2(-texel.x, 0.0), 0).rgb;
        float3 blur = (n + s + e + w) * 0.25;
        c = saturate(c + (c - blur) * (sharpen * 2.0));
    }
    if (sat != 1.0 || con != 1.0) {
        float l = dot(c, float3(0.299, 0.587, 0.114));
        c = lerp(float3(l, l, l), c, sat);
        c = saturate((c - 0.5) * con + 0.5);
    }
    float2 px = uv * pp[0].zw;
    if (scan > 0.0) {
        float period = max(2.0, pp[0].w / 360.0);
        float lw = 0.5 + 0.5 * cos(6.2831853 * px.y / period);
        c = c * (1.0 - scan * lw) * (1.0 + scan * 0.3);
    }
    if (crt > 0.0) {
        float m = fmod(floor(px.x), 3.0);
        float3 tri = float3(m < 0.5 ? 1.0 : 0.0, (m > 0.5 && m < 1.5) ? 1.0 : 0.0, m > 1.5 ? 1.0 : 0.0);
        c = c * (float3(1.0, 1.0, 1.0) - crt * (float3(1.0, 1.0, 1.0) - tri)) * (1.0 + crt * 0.6);
    }
    if (vig > 0.0) {
        float2 d = (uv - 0.5) * float2(1.2, 1.0);
        c = c * (1.0 - vig * smoothstep(0.25, 0.85, length(d) * 1.6));
    }
    result[id.xy] = float4(saturate(c), 1.0);
}
)HLSL";
// SDR -> HDR: sRGB decode, highlight expansion on the brightest channel (keeps hue), scale to scRGB (1.0 = 80 nits).
// pp[0] = (width, height, paper scale (SDR white / 80 nits), highlight boost).
const char* d2_hdr_cs = R"HLSL(
cbuffer HdrParams : register(b0) { float4 pp[5]; };
Texture2D<float4> scene : register(t0);
RWTexture2D<float4> result : register(u0);
float3 srgb_to_linear(float3 c) {
    return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)pp[0].x || id.y >= (uint)pp[0].y) return;
    float3 c = saturate(scene.Load(int3(id.xy, 0)).rgb);
    float3 lin = srgb_to_linear(c);
    float m = max(lin.r, max(lin.g, lin.b));
    float t = saturate((m - 0.55) / 0.45);
    float gain = 1.0 + pp[0].w * t * t;
    result[id.xy] = float4(lin * gain * pp[0].z, 1.0);
}
)HLSL";
void dispatch(ID3D12PipelineState* p,UINT index,const UINT constants[20]){
    auto h=gpu->heap->GetGPUDescriptorHandleForHeapStart();h.ptr+=static_cast<UINT64>(index)*gpu->stride;
    gpu->commands->SetPipelineState(p);gpu->commands->SetComputeRoot32BitConstants(0,20,constants,0);
    gpu->commands->SetComputeRootDescriptorTable(1,h);gpu->commands->Dispatch((cfg.ow+7)/8,(cfg.oh+7)/8,1);
}
// Runs the filters: source (any state given by `source_state`) -> gpu->post, ready to be copied to the swap buffer.
void run_filters(UINT frame,ID3D12Resource* source,D3D12_RESOURCE_STATES source_state){
    float values[20]{};values[0]=values[2]=static_cast<float>(cfg.ow);values[1]=values[3]=static_cast<float>(cfg.oh);
    for(int i=0;i<8;++i)values[4+i]=post_values[i];
    UINT constants[20];std::memcpy(constants,values,sizeof constants);
    const UINT slot=12+frame*2;
    descriptors(slot,source,gpu->post.p);
    ID3D12DescriptorHeap* heaps[]={gpu->heap.p};gpu->commands->SetDescriptorHeaps(1,heaps);gpu->commands->SetComputeRootSignature(gpu->root.p);
    transition(source,source_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(gpu->post.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch(gpu->filters.p,slot,constants);
    transition(source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,source_state);
    transition(gpu->post.p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
}
}

namespace {
// Final step in HDR: source (8-bit, state `source_state`) -> HDR pass -> FP16 buffer -> copied into the swap buffer (already COPY_DEST).
void hdr_pass(UINT frame,ID3D12Resource* source,D3D12_RESOURCE_STATES source_state,ID3D12Resource* output){
    static const float strength[4]={0.f,0.5f,0.75f,1.0f};
    int lv=hdr_level.load();lv=lv<0?0:lv>3?3:lv;
    const float level_peak=(hdr_peak_nits<1500.0f?hdr_peak_nits:1500.0f)*strength[lv];
    const float paper_nits=hdr_paper_scale*80.0f;
    const float boost=(hdr_display&&level_peak>paper_nits)?level_peak/paper_nits-1.0f:0.0f;
    float values[20]{};values[0]=static_cast<float>(cfg.ow);values[1]=static_cast<float>(cfg.oh);values[2]=hdr_display?hdr_paper_scale:1.0f;values[3]=boost;
    UINT constants[20];std::memcpy(constants,values,sizeof constants);
    const UINT slot=16+frame*2;
    descriptors(slot,source,gpu->hdrbuf.p,DXGI_FORMAT_R16G16B16A16_FLOAT);
    ID3D12DescriptorHeap* heaps[]={gpu->heap.p};gpu->commands->SetDescriptorHeaps(1,heaps);gpu->commands->SetComputeRootSignature(gpu->root.p);
    transition(source,source_state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(gpu->hdrbuf.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch(gpu->hdr.p,slot,constants);
    transition(source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,source_state);
    transition(gpu->hdrbuf.p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu->commands->CopyResource(output,gpu->hdrbuf.p);
    transition(gpu->hdrbuf.p,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
}
}

namespace {
struct DisplayState { wchar_t name[CCHDEVICENAME]{}; DEVMODEW original{},applied{};bool changed=false; } display_state;
bool flag(const char* name){const char* s=std::getenv(name);return s && std::strcmp(s,"1")==0;}
void restore_display(){
    if(!display_state.changed)return;
    DEVMODEW now{};now.dmSize=sizeof(now);
    if(EnumDisplaySettingsExW(display_state.name,ENUM_CURRENT_SETTINGS,&now,0) &&
       now.dmPelsWidth==display_state.applied.dmPelsWidth && now.dmPelsHeight==display_state.applied.dmPelsHeight &&
       now.dmBitsPerPel==display_state.applied.dmBitsPerPel && now.dmDisplayOrientation==display_state.applied.dmDisplayOrientation &&
       now.dmDisplayFlags==display_state.applied.dmDisplayFlags && now.dmDisplayFrequency==display_state.applied.dmDisplayFrequency){
        auto original=display_state.original;original.dmFields=DM_DISPLAYFREQUENCY;
        LONG result=ChangeDisplaySettingsExW(display_state.name,&original,nullptr,0,nullptr);
        std::fprintf(stderr,"[D2DISPLAY] restore=%ld refresh=%luHz\n",result,original.dmDisplayFrequency);
    }
    display_state.changed=false;
}
}
extern "C" void d2_display_start(void* handle){
    static bool registered=false;if(!registered){std::atexit(restore_display);registered=true;}
    if(display_state.changed)return;
    MONITORINFOEXW info{};info.cbSize=sizeof(info);
    if(!GetMonitorInfoW(MonitorFromWindow(static_cast<HWND>(handle),MONITOR_DEFAULTTONEAREST),reinterpret_cast<MONITORINFO*>(&info)))return;
    DEVMODEW current{};current.dmSize=sizeof(current);
    if(!EnumDisplaySettingsExW(info.szDevice,ENUM_CURRENT_SETTINGS,&current,0))return;
    DWORD maximum=current.dmDisplayFrequency;
    const char* request=std::getenv("D2_REFRESH_RATE");
    DWORD desired=0;bool found=false;
    if(request && std::strcmp(request,"native")){
        for(const char* rate:{"60","75","120","144","165","180"})if(!std::strcmp(request,rate))desired=static_cast<DWORD>(std::strtoul(rate,nullptr,10));
        if(!desired){std::fprintf(stderr,"[D2DISPLAY] invalid requested Hz; keeping current\n");return;}
    }
    found=desired==current.dmDisplayFrequency;
    for(DWORD index=0;;index++){
        DEVMODEW mode{};mode.dmSize=sizeof(mode);
        if(!EnumDisplaySettingsExW(info.szDevice,index,&mode,0))break;
        if(mode.dmPelsWidth==current.dmPelsWidth && mode.dmPelsHeight==current.dmPelsHeight &&
           mode.dmBitsPerPel==current.dmBitsPerPel && mode.dmDisplayOrientation==current.dmDisplayOrientation &&
           mode.dmDisplayFlags==current.dmDisplayFlags){
            if(mode.dmDisplayFrequency>maximum)maximum=mode.dmDisplayFrequency;
            if(mode.dmDisplayFrequency==desired)found=true;
        }
    }
    std::fprintf(stderr,"[D2DISPLAY] desktop=%lux%lu active=%luHz maximum-same-mode=%luHz request=%s\n",
        current.dmPelsWidth,current.dmPelsHeight,current.dmDisplayFrequency,maximum,request?request:"native");
    if(desired && !found){std::fprintf(stderr,"[D2DISPLAY] requested=%luHz unsupported; keeping %luHz\n",desired,current.dmDisplayFrequency);return;}
    if(desired && desired!=current.dmDisplayFrequency){
        auto mode=current;mode.dmDisplayFrequency=desired;mode.dmFields=DM_DISPLAYFREQUENCY;
        LONG result=ChangeDisplaySettingsExW(info.szDevice,&mode,nullptr,CDS_TEST,nullptr);
        if(result==DISP_CHANGE_SUCCESSFUL)result=ChangeDisplaySettingsExW(info.szDevice,&mode,nullptr,CDS_FULLSCREEN,nullptr);
        if(result==DISP_CHANGE_SUCCESSFUL){
            std::wcscpy(display_state.name,info.szDevice);display_state.original=current;display_state.applied=mode;display_state.changed=true;
        }
        std::fprintf(stderr,"[D2DISPLAY] apply-requested=%ld refresh=%luHz\n",result,desired);
    }
}
extern "C" UINT d2_display_swap_flags(void* object){
    if(!flag("D2_VRR")||!object)return 0;
    auto* factory=static_cast<IDXGIFactory4*>(object);Ref<IDXGIFactory5> newer;BOOL supported=FALSE;
    if(SUCCEEDED(factory->QueryInterface(IID_IDXGIFactory5,reinterpret_cast<void**>(newer.put()))))
        newer->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&supported,sizeof(supported));
    std::fprintf(stderr,"[D2DISPLAY] tearing-supported=%d\n",supported);
    return supported?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0;
}
// ---- On-disk cache for shaders (DXBC) and pipelines (driver blobs) built while playing ----
// Safe by construction: any miss, damaged file or driver mismatch falls back to compiling exactly as before.
namespace shader_cache{
    std::atomic<int> dxbc_hit{0},dxbc_miss{0},pso_hit{0},pso_miss{0};std::atomic<long long> compile_us{0};
    const wchar_t* dir(){static const std::wstring d=[]{const wchar_t* v=_wgetenv(L"D2_SHADER_CACHE_DIR");return std::wstring(v?v:L"");}();return d.empty()?nullptr:d.c_str();}
    unsigned long long fnv(const void* p,size_t n,unsigned long long h=1469598103934665603ULL){
        const unsigned char* b=static_cast<const unsigned char*>(p);for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ULL;}return h;}
    void make_dirs(const std::wstring& path){
        for(size_t i=1;i<=path.size();i++)
            if(i==path.size()||path[i]==L'\\'||path[i]==L'/')CreateDirectoryW(path.substr(0,i).c_str(),nullptr);   // existing levels just fail harmlessly
    }
    std::wstring file_for(const wchar_t* kind,unsigned long long a,unsigned long long b){
        auto hex=[](unsigned long long v){std::wstring t(16,L'0');for(int i=15;i>=0;i--,v>>=4)t[i]=L"0123456789abcdef"[v&15];return t;};
        make_dirs(dir());   // creates every missing level: CreateDirectoryW alone only makes the last one
        return std::wstring(dir())+L"\\"+kind+L"-"+hex(a)+hex(b)+L".bin";}
    bool read_all(const std::wstring& path,std::vector<unsigned char>& out){
        HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER size{};bool ok=GetFileSizeEx(f,&size)&&size.QuadPart>0&&size.QuadPart<(64<<20);
        if(ok){out.resize(static_cast<size_t>(size.QuadPart));DWORD got=0;ok=ReadFile(f,out.data(),static_cast<DWORD>(out.size()),&got,nullptr)&&got==out.size();}
        CloseHandle(f);return ok;}
    void write_all(const std::wstring& path,const void* data,size_t size){
        const std::wstring tmp=path+L".tmp";
        HANDLE f=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE)return;
        DWORD put=0;const bool ok=WriteFile(f,data,static_cast<DWORD>(size),&put,nullptr)&&put==size;CloseHandle(f);
        if(!ok||!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING))DeleteFileW(tmp.c_str());}
    double now_us(){LARGE_INTEGER c,f;QueryPerformanceCounter(&c);QueryPerformanceFrequency(&f);return static_cast<double>(c.QuadPart)*1e6/static_cast<double>(f.QuadPart);}
    // Header = magic + payload size + payload hash, so a truncated or corrupted file is never trusted.
    struct Header{unsigned magic;unsigned size;unsigned long long check;};
    constexpr unsigned kMagic=0x32534344u;  // "DCS2"
    bool load(const std::wstring& path,std::vector<unsigned char>& payload){
        std::vector<unsigned char> raw;if(!read_all(path,raw)||raw.size()<sizeof(Header))return false;
        Header h;std::memcpy(&h,raw.data(),sizeof h);
        if(h.magic!=kMagic||h.size!=raw.size()-sizeof h||h.check!=fnv(raw.data()+sizeof h,h.size))return false;
        payload.assign(raw.begin()+sizeof h,raw.end());return true;}
    void store(const std::wstring& path,const void* data,size_t size){
        std::vector<unsigned char> raw(sizeof(Header)+size);
        Header h{kMagic,static_cast<unsigned>(size),fnv(data,size)};
        std::memcpy(raw.data(),&h,sizeof h);std::memcpy(raw.data()+sizeof h,data,size);write_all(path,raw.data(),raw.size());}
}
extern "C" long d2_cached_compile(const char* hlsl,size_t length,const char* name,const char* target,void** blob,void** errors){
    ID3DBlob** out=reinterpret_cast<ID3DBlob**>(blob);ID3DBlob** err=reinterpret_cast<ID3DBlob**>(errors);
    const bool cached=shader_cache::dir()!=nullptr;
    unsigned long long a=0,b=0;std::wstring path;
    if(cached){
        a=shader_cache::fnv(hlsl,length);b=shader_cache::fnv(target,std::strlen(target),a^0x9e3779b97f4a7c15ULL);
        path=shader_cache::file_for(target[0]=='v'?L"vs":L"ps",a,b);
        std::vector<unsigned char> bytes;
        if(shader_cache::load(path,bytes)){
            ID3DBlob* b2=nullptr;
            if(SUCCEEDED(D3DCreateBlob(bytes.size(),&b2))){std::memcpy(b2->GetBufferPointer(),bytes.data(),bytes.size());*out=b2;shader_cache::dxbc_hit++;return S_OK;}
        }
    }
    const double t0=shader_cache::now_us();
    const HRESULT hr=D3DCompile(hlsl,length,name,nullptr,nullptr,"main",target,0,0,out,err);
    shader_cache::compile_us+=static_cast<long long>(shader_cache::now_us()-t0);shader_cache::dxbc_miss++;
    if(cached&&SUCCEEDED(hr)&&*out)shader_cache::store(path,(*out)->GetBufferPointer(),(*out)->GetBufferSize());
    return hr;
}
extern "C" long d2_create_pso_cached(void* device_ptr,const void* desc_ptr,void** pso_out){
    auto* device=static_cast<ID3D12Device*>(device_ptr);
    const auto* desc=static_cast<const D3D12_GRAPHICS_PIPELINE_STATE_DESC*>(desc_ptr);
    ID3D12PipelineState** out=reinterpret_cast<ID3D12PipelineState**>(pso_out);
    if(!shader_cache::dir()){return device->CreateGraphicsPipelineState(desc,IID_ID3D12PipelineState,pso_out);}
    // Key = everything that shapes the pipeline except the root signature pointer (the signature is fixed by the game).
    using shader_cache::fnv;
    unsigned long long a=fnv(desc->VS.pShaderBytecode,desc->VS.BytecodeLength),b=fnv(desc->PS.pShaderBytecode,desc->PS.BytecodeLength,0x51ed270b0b0b0b0bULL);
    auto mix=[&](const void* p,size_t n){a=fnv(p,n,a);b=fnv(p,n,b^0x9e3779b97f4a7c15ULL);};
    mix(&desc->BlendState,sizeof desc->BlendState);mix(&desc->RasterizerState,sizeof desc->RasterizerState);
    mix(&desc->DepthStencilState,sizeof desc->DepthStencilState);mix(&desc->SampleMask,sizeof desc->SampleMask);
    mix(&desc->PrimitiveTopologyType,sizeof desc->PrimitiveTopologyType);mix(&desc->NumRenderTargets,sizeof desc->NumRenderTargets);
    mix(desc->RTVFormats,sizeof desc->RTVFormats);mix(&desc->DSVFormat,sizeof desc->DSVFormat);mix(&desc->SampleDesc,sizeof desc->SampleDesc);
    mix(&desc->Flags,sizeof desc->Flags);mix(&desc->IBStripCutValue,sizeof desc->IBStripCutValue);
    for(UINT i=0;i<desc->InputLayout.NumElements;i++){
        const D3D12_INPUT_ELEMENT_DESC& e=desc->InputLayout.pInputElementDescs[i];
        mix(e.SemanticName,std::strlen(e.SemanticName));mix(&e.SemanticIndex,sizeof e.SemanticIndex);mix(&e.Format,sizeof e.Format);
        mix(&e.InputSlot,sizeof e.InputSlot);mix(&e.AlignedByteOffset,sizeof e.AlignedByteOffset);
        mix(&e.InputSlotClass,sizeof e.InputSlotClass);mix(&e.InstanceDataStepRate,sizeof e.InstanceDataStepRate);
    }
    const std::wstring path=shader_cache::file_for(L"pso",a,b);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC copy=*desc;
    std::vector<unsigned char> blob;
    if(shader_cache::load(path,blob)){
        copy.CachedPSO.pCachedBlob=blob.data();copy.CachedPSO.CachedBlobSizeInBytes=blob.size();
        if(SUCCEEDED(device->CreateGraphicsPipelineState(&copy,IID_ID3D12PipelineState,pso_out))){shader_cache::pso_hit++;return S_OK;}
        DeleteFileW(path.c_str());   // stale (new driver/GPU): rebuild below and replace it
        copy.CachedPSO={};
    }
    const double t0=shader_cache::now_us();
    const HRESULT hr=device->CreateGraphicsPipelineState(&copy,IID_ID3D12PipelineState,pso_out);
    shader_cache::compile_us+=static_cast<long long>(shader_cache::now_us()-t0);shader_cache::pso_miss++;
    if(SUCCEEDED(hr)&&*out){
        ID3DBlob* cached=nullptr;
        if(SUCCEEDED((*out)->GetCachedBlob(&cached))&&cached){shader_cache::store(path,cached->GetBufferPointer(),cached->GetBufferSize());cached->Release();}
    }
    return hr;
}

// Frame-time diagnostics (D2_FRAME_LOG=<file>): one line per hitch plus a summary every 5 s. It only measures; it never changes pacing.
// "game" = time between two presents (the game's own work: recompiled code, uploads, shader/pipeline compiles);
// "present" = time inside Present (VSync / compositor / GPU wait). The larger one tells which side the hitch came from.
namespace flog{
    FILE* file=nullptr;bool ready=false;LARGE_INTEGER freq{},start{},last_exit{};bool have_last=false;
    ULONGLONG last_thread=0,last_proc=0;   // CPU time (100 ns units) at the previous present, to tell "computing" from "waiting"
    double ema=16.7;std::vector<float> window;double sum_game=0,sum_present=0,worst=0;int hitches=0;double next_report=5.0;
    double ms(LARGE_INTEGER a,LARGE_INTEGER b){return static_cast<double>(b.QuadPart-a.QuadPart)*1000.0/static_cast<double>(freq.QuadPart);}
    void open(UINT interval){
        ready=true;const char* path=std::getenv("D2_FRAME_LOG");
        if(!path||!*path)return;
        file=std::fopen(path,"a");if(!file)return;
        QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&start);
        SYSTEMTIME t;GetLocalTime(&t);
        std::fprintf(file,"=== sessao %04u-%02u-%02u %02u:%02u:%02u  render=%ux%u saida=%ux%u upscaler=%s intervalo_present=%u ===\n",
            t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,cfg.rw,cfg.rh,cfg.ow,cfg.oh,cfg.fsr?"FSR1":"nenhum",interval);
        if(shader_cache::dir())std::fprintf(file,"    cache de shaders em: %ls\n",shader_cache::dir());
        else std::fprintf(file,"    cache de shaders: desligado (sem pasta)\n");
        std::fflush(file);
    }
    void report(double now_s){
        if(window.empty())return;
        std::sort(window.begin(),window.end());
        const double n=static_cast<double>(window.size());
        std::fprintf(file,"[%6.1fs] fps=%.1f medio=%.1fms p99=%.1fms max=%.1fms engasgos=%d jogo=%.1fms present=%.1fms\n",
            now_s,n/5.0,(sum_game+sum_present)/n,static_cast<double>(window[static_cast<size_t>(n*0.99)>=window.size()?window.size()-1:static_cast<size_t>(n*0.99)]),worst,hitches,sum_game/n,sum_present/n);
        static int pdh=0,pdm=0,pph=0,ppm=0;static long long pus=0;
        const int dh=shader_cache::dxbc_hit,dm=shader_cache::dxbc_miss,ph=shader_cache::pso_hit,pm=shader_cache::pso_miss;const long long us=shader_cache::compile_us;
        if(dh!=pdh||dm!=pdm||ph!=pph||pm!=ppm)
            std::fprintf(file,"          cache de shaders (ultimos 5s): shader lido do disco=%d compilado=%d | pipeline lido do disco=%d criado=%d | tempo gasto compilando=%.0fms\n",dh-pdh,dm-pdm,ph-pph,pm-ppm,static_cast<double>(us-pus)/1000.0);
        pdh=dh;pdm=dm;pph=ph;ppm=pm;pus=us;
        std::fflush(file);
        window.clear();sum_game=sum_present=worst=0;hitches=0;
    }
    ULONGLONG cpu_of(const FILETIME& a,const FILETIME& b){
        return ((static_cast<ULONGLONG>(a.dwHighDateTime)<<32)|a.dwLowDateTime)+((static_cast<ULONGLONG>(b.dwHighDateTime)<<32)|b.dwLowDateTime);
    }
    void frame(double game_ms,double present_ms,LARGE_INTEGER now){
        const double total=game_ms+present_ms;
        FILETIME c,e,k,u,k2,u2;ULONGLONG thread_now=0,proc_now=0;
        if(GetThreadTimes(GetCurrentThread(),&c,&e,&k,&u))thread_now=cpu_of(k,u);
        if(GetProcessTimes(GetCurrentProcess(),&c,&e,&k2,&u2))proc_now=cpu_of(k2,u2);
        const double cpu_thread=last_thread?static_cast<double>(thread_now-last_thread)/10000.0:0;
        const double cpu_proc=last_proc?static_cast<double>(proc_now-last_proc)/10000.0:0;
        last_thread=thread_now;last_proc=proc_now;
        if(!have_last)return;
        window.push_back(static_cast<float>(total));sum_game+=game_ms;sum_present+=present_ms;if(total>worst)worst=total;
        const double at=ms(start,now)/1000.0;
        if(total>20.0&&total>1.6*ema){
            ++hitches;
            // cpu_thread ~ total: this thread was computing. Both far below total: it was waiting (GPU fence, disk, lock, sleep).
            const char* kind=game_ms<=present_ms?"apresentacao/VSync/GPU":(cpu_thread>0.8*total?"CPU (thread do jogo calculando)":(cpu_proc>0.8*total?"CPU (outra thread trabalhando)":"espera (GPU/disco/trava)"));
            std::fprintf(file,"[%6.1fs] ENGASGO %.1fms (jogo=%.1f present=%.1f cpu_thread=%.1f cpu_proc=%.1f) origem=%s\n",at,total,game_ms,present_ms,cpu_thread,cpu_proc,kind);
            std::fflush(file);
        }else ema=ema*0.95+total*0.05;
        if(at>=next_report){report(at);next_report=at+5.0;}
    }
}
extern "C" int d2_display_present(void* object,UINT interval){
    if(!object)return -1;auto* swap=static_cast<IDXGISwapChain3*>(object);
    if(!flog::ready)flog::open(interval);
    LARGE_INTEGER t_enter{};double game_ms=0;
    if(flog::file){QueryPerformanceCounter(&t_enter);game_ms=flog::have_last?flog::ms(flog::last_exit,t_enter):0;}
    // F1 menu open: hold the frame so the game stands still behind the panel (messages keep being pumped for the panel and the pad).
    if(menu_pause.load()){
        HWND window=nullptr;
        if(SUCCEEDED(swap->GetHwnd(&window))&&window){
            std::fprintf(stderr,"[D2MENU] game paused (F1 menu open)\n");
            while(menu_pause.load()&&IsWindow(window)){
                MSG msg;
                while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
                    if(msg.message==WM_QUIT){PostQuitMessage(static_cast<int>(msg.wParam));return 0;}
                    TranslateMessage(&msg);DispatchMessageW(&msg);
                }
                Sleep(16);
            }
            std::fprintf(stderr,"[D2MENU] game resumed\n");
        }
    }
    // Pause while the game window is not in front (alt-tab, minimised). Messages keep being pumped so the window and the F1 panel stay alive.
    static const bool pause_env_off=[]{const char* v=std::getenv("D2_PAUSE_UNFOCUSED");return v&&v[0]=='0';}();
    if(pause_unfocused.load()&&!pause_env_off){
        HWND window=nullptr;
        if(SUCCEEDED(swap->GetHwnd(&window))&&window){
            auto in_front=[&]{
                if(IsIconic(window))return false;
                HWND fg=GetForegroundWindow();DWORD pid=0;
                if(fg)GetWindowThreadProcessId(fg,&pid);
                return fg&&pid==GetCurrentProcessId();
            };
            if(!in_front()){
                std::fprintf(stderr,"[D2FOCUS] paused (window not in front)\n");
                while(pause_unfocused.load()&&IsWindow(window)&&!in_front()){
                    MSG msg;
                    while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
                        if(msg.message==WM_QUIT){PostQuitMessage(static_cast<int>(msg.wParam));return 0;}
                        TranslateMessage(&msg);DispatchMessageW(&msg);
                    }
                    Sleep(40);
                }
                std::fprintf(stderr,"[D2FOCUS] resumed\n");
            }
        }
    }
    UINT flags=0;DXGI_SWAP_CHAIN_DESC1 desc{};BOOL fullscreen=TRUE;
    if(flag("D2_VRR") && interval==0 && SUCCEEDED(swap->GetDesc1(&desc)) &&
       (desc.Flags&DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) && SUCCEEDED(swap->GetFullscreenState(&fullscreen,nullptr)) && !fullscreen)
        flags=DXGI_PRESENT_ALLOW_TEARING;
    LARGE_INTEGER t_present{};if(flog::file)QueryPerformanceCounter(&t_present);
    HRESULT result=swap->Present(interval,flags);
    if(flog::file){LARGE_INTEGER t_done;QueryPerformanceCounter(&t_done);flog::frame(game_ms,flog::ms(t_present,t_done),t_done);flog::last_exit=t_done;flog::have_last=true;}
    if(FAILED(result))std::fprintf(stderr,"[D2DISPLAY] present failed HRESULT=%lu\n",static_cast<unsigned long>(result));
    return FAILED(result)?-1:0;
}
extern "C" UINT d2_graphics_render_width(){return cfg.rw;}
extern "C" UINT d2_graphics_render_height(){return cfg.rh;}
extern "C" UINT d2_graphics_output_width(){return cfg.ow;}
extern "C" UINT d2_graphics_output_height(){return cfg.oh;}
extern "C" UINT d2_graphics_guest_width(){return cfg.gw;}
extern "C" UINT d2_graphics_guest_height(){return cfg.gh;}
extern "C" int d2_graphics_configure(UINT w,UINT h){
    try{
        cfg={};cfg.gw=w;cfg.gh=h;cfg.rw=dimension("D2_RENDER_WIDTH",w);cfg.rh=dimension("D2_RENDER_HEIGHT",h);
        cfg.ow=dimension("D2_OUTPUT_WIDTH",w);cfg.oh=dimension("D2_OUTPUT_HEIGHT",h);
        const char* mode=std::getenv("D2_UPSCALER");
        if(mode&&std::strcmp(mode,"native")&&std::strcmp(mode,"fsr1"))throw std::runtime_error("Temporal upscaler unavailable: motion vectors/HUD separation missing");
        cfg.fsr=mode&&std::strcmp(mode,"fsr1")==0;
        if(cfg.fsr&&(cfg.rw>cfg.ow||cfg.rh>cfg.oh))throw std::runtime_error("FSR 1 requires render size <= output size");
        const char* sharp=std::getenv("D2_FSR_SHARPNESS");
        if(sharp){char* end=nullptr;cfg.sharpness=std::strtof(sharp,&end);if(*end||!std::isfinite(cfg.sharpness)||cfg.sharpness<0||cfg.sharpness>1)throw std::runtime_error("Invalid FSR sharpness");}
        const char* hud=std::getenv("D2_HUD_AFTER_UPSCALE");
        cfg.post=flag("D2_POST_FILTERS");
        cfg.hdr=flag("D2_HDR");
        cfg.active=cfg.hdr||cfg.fsr||cfg.post||cfg.rw!=w||cfg.rh!=h||cfg.ow!=w||cfg.oh!=h||(hud && !std::strcmp(hud,"1"));
        if(cfg.active&&(std::getenv("GCM_GUEST_FB")||std::getenv("SUBVP_RTT")||std::getenv("GCM_COMPOSITE_RT")))throw std::runtime_error("Experimental framebuffer override incompatible with graphics scaling");
        std::fprintf(stderr,"[D2GRAPHICS] render=%ux%u output=%ux%u mode=%s filters=%s DLSS=unavailable FG=unavailable\n",cfg.rw,cfg.rh,cfg.ow,cfg.oh,cfg.fsr?"FSR1-EASU-RCAS":"native",cfg.post?"available":"off");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"[D2GRAPHICS] %s\n",e.what());return -1;}
}
namespace {
// SDR white level the user chose in Windows (Settings > Display > HDR > SDR content brightness) for the monitor holding the window.
float windows_sdr_white_nits(const wchar_t* gdi_name){
    UINT32 np=0,nm=0;
    if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&np,&nm)!=ERROR_SUCCESS||!np)return 0.0f;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&np,paths.data(),&nm,modes.data(),nullptr)!=ERROR_SUCCESS)return 0.0f;
    for(UINT32 i=0;i<np;++i){
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};src.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;src.header.size=sizeof src;
        src.header.adapterId=paths[i].sourceInfo.adapterId;src.header.id=paths[i].sourceInfo.id;
        if(DisplayConfigGetDeviceInfo(&src.header)!=ERROR_SUCCESS||_wcsicmp(src.viewGdiDeviceName,gdi_name))continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL w{};w.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;w.header.size=sizeof w;
        w.header.adapterId=paths[i].targetInfo.adapterId;w.header.id=paths[i].targetInfo.id;
        if(DisplayConfigGetDeviceInfo(&w.header)==ERROR_SUCCESS&&w.SDRWhiteLevel)return static_cast<float>(w.SDRWhiteLevel)/1000.0f*80.0f;
    }
    return 0.0f;
}
// Puts the swap chain in scRGB (linear BT.709, 1.0 = 80 nits) and reads the display's HDR state, peak and SDR white.
void hdr_setup(IDXGISwapChain3* swap){
    UINT support=0;
    if(FAILED(swap->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,&support))||!(support&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
        throw std::runtime_error("scRGB color space not supported");
    checked(swap->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709),"HDR color space");
    hdr_display=false;hdr_paper_scale=1.0f;hdr_peak_nits=80.0f;
    Ref<IDXGIOutput> out;
    if(SUCCEEDED(swap->GetContainingOutput(out.put()))){
        Ref<IDXGIOutput6> out6;DXGI_OUTPUT_DESC1 d{};
        if(SUCCEEDED(out->QueryInterface(IID_IDXGIOutput6,reinterpret_cast<void**>(out6.put())))&&SUCCEEDED(out6->GetDesc1(&d))&&d.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020){
            hdr_display=true;hdr_peak_nits=d.MaxLuminance>200.0f?d.MaxLuminance:1000.0f;
            float white=0.0f;DXGI_OUTPUT_DESC od{};
            if(SUCCEEDED(out->GetDesc(&od)))white=windows_sdr_white_nits(od.DeviceName);
            if(const char* e=std::getenv("D2_HDR_PAPER_NITS")){float v=static_cast<float>(std::atof(e));if(v>=80.0f&&v<=480.0f)white=v;}
            if(white<80.0f)white=203.0f;
            hdr_paper_scale=white/80.0f;
        }
    }
    std::fprintf(stderr,"[D2HDR] scRGB swap chain; display HDR=%d peak=%.0f nits SDR white=%.0f nits\n",hdr_display?1:0,hdr_peak_nits,hdr_paper_scale*80.0f);
}
}
extern "C" int d2_graphics_start(void* device,void* queue,void* swapchain){
    if(!cfg.active)return 0;
    try{
        gpu=new State;gpu->device=static_cast<ID3D12Device*>(device);gpu->queue=static_cast<ID3D12CommandQueue*>(queue);
        auto* swap=static_cast<IDXGISwapChain3*>(swapchain);
        for(UINT i=0;i<2;++i){
            checked(swap->GetBuffer(i,IID_ID3D12Resource,reinterpret_cast<void**>(gpu->output[i].put())),"Graphics swap buffer");
            checked(gpu->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,reinterpret_cast<void**>(gpu->allocator[i].put())),"Graphics allocator");
        }
        checked(gpu->device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,gpu->allocator[0].p,nullptr,IID_ID3D12GraphicsCommandList,reinterpret_cast<void**>(gpu->commands.put())),"Graphics command list");
        checked(gpu->commands->Close(),"Close graphics command list");
        const bool scaling=cfg.fsr||cfg.rw!=cfg.ow||cfg.rh!=cfg.oh;
        if(cfg.hdr){try{hdr_setup(swap);}catch(const std::exception& e){std::fprintf(stderr,"[D2HDR] %s; showing as SDR\n",e.what());hdr_display=false;hdr_paper_scale=1.0f;}}
        if(!scaling&&!cfg.post&&!cfg.hdr)return 0;
        if(scaling)texture(gpu->final.put(),cfg.ow,cfg.oh,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if(cfg.fsr)texture(gpu->middle.put(),cfg.ow,cfg.oh,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if(cfg.post)texture(gpu->post.put(),cfg.ow,cfg.oh,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if(cfg.hdr)texture(gpu->hdrbuf.put(),cfg.ow,cfg.oh,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,DXGI_FORMAT_R16G16B16A16_FLOAT);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=24;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        checked(gpu->device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(gpu->heap.put())),"Graphics descriptor heap");
        gpu->stride=gpu->device->GetDescriptorHandleIncrementSize(hd.Type);
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=1;
        ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=1;ranges[1].OffsetInDescriptorsFromTableStart=1;
        D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants.Num32BitValues=20;
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={2,ranges};
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;rd.NumStaticSamplers=1;rd.pStaticSamplers=&sampler;
        Ref<ID3DBlob> blob,error;checked(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,blob.put(),error.put()),"Graphics root signature");
        checked(gpu->device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,reinterpret_cast<void**>(gpu->root.put())),"Graphics root signature object");
        if(cfg.fsr){pipeline(d2_easu,gpu->easu);pipeline(d2_rcas,gpu->rcas);}else if(scaling)pipeline(d2_bilinear,gpu->bilinear);
        if(cfg.post)pipeline(d2_post_cs,gpu->filters);
        if(cfg.hdr)pipeline(d2_hdr_cs,gpu->hdr);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"[D2GRAPHICS] start: %s\n",e.what());delete gpu;gpu=nullptr;return -1;}
}
extern "C" int d2_graphics_scene(UINT frame,void** resource){
    if(!cfg.active)return 0;
    try{if(!gpu||frame>=2||!resource)throw std::runtime_error("Invalid graphics scene");
        auto** out=reinterpret_cast<ID3D12Resource**>(resource);texture(out,cfg.rw,cfg.rh,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        if(gpu->heap.p){if(cfg.fsr){descriptors(frame*6,*out,gpu->middle.p);descriptors(frame*6+2,gpu->middle.p,gpu->final.p);}else if(gpu->final.p)descriptors(frame*6+4,*out,gpu->final.p);}
        return 1;
    }catch(const std::exception& e){std::fprintf(stderr,"[D2GRAPHICS] scene: %s\n",e.what());return -1;}
}
extern "C" int d2_graphics_present(UINT frame,void* scene){
    if(!cfg.active)return 0;
    try{if(!gpu||frame>=2)throw std::runtime_error("Invalid graphics present");
        auto* input=static_cast<ID3D12Resource*>(scene);auto* output=gpu->output[frame].p;
        if(gpu->heap.p){if(cfg.fsr)descriptors(frame*6,input,gpu->middle.p);else if(gpu->final.p)descriptors(frame*6+4,input,gpu->final.p);}
        const bool scaling=cfg.fsr||cfg.rw!=cfg.ow||cfg.rh!=cfg.oh;
        const bool filters=cfg.post&&gpu->filters.p&&post_enabled.load()!=0;
        checked(gpu->allocator[frame]->Reset(),"Graphics allocator reset");
        checked(gpu->commands->Reset(gpu->allocator[frame].p,nullptr),"Graphics list reset");
        const bool hdr=cfg.hdr&&gpu->hdr.p;
        const D3D12_RESOURCE_STATES out_state=D3D12_RESOURCE_STATE_COPY_DEST;
        transition(output,D3D12_RESOURCE_STATE_PRESENT,out_state);
        if(!scaling&&!filters){
            if(hdr)hdr_pass(frame,input,D3D12_RESOURCE_STATE_COMMON,output);
            else{transition(input,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);gpu->commands->CopyResource(output,input);
            transition(input,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);}
        }else if(!scaling){
            // Native size with image filters: scene -> filters -> swap buffer.
            run_filters(frame,input,D3D12_RESOURCE_STATE_COMMON);
            if(hdr)hdr_pass(frame,gpu->post.p,D3D12_RESOURCE_STATE_COPY_SOURCE,output);else gpu->commands->CopyResource(output,gpu->post.p);
            transition(gpu->post.p,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
        }else{
            UINT constants[20]{};constants[16]=cfg.ow;constants[17]=cfg.oh;
            ID3D12DescriptorHeap* heaps[]={gpu->heap.p};gpu->commands->SetDescriptorHeaps(1,heaps);gpu->commands->SetComputeRootSignature(gpu->root.p);
            transition(input,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            transition(gpu->final.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if(cfg.fsr){
                FsrEasuCon(constants,constants+4,constants+8,constants+12,(float)cfg.rw,(float)cfg.rh,(float)cfg.rw,(float)cfg.rh,(float)cfg.ow,(float)cfg.oh);
                transition(gpu->middle.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                dispatch(gpu->easu.p,frame*6,constants);
                transition(gpu->middle.p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                FsrRcasCon(constants,2.0f*(1.0f-cfg.sharpness));dispatch(gpu->rcas.p,frame*6+2,constants);
                transition(gpu->middle.p,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
            }else dispatch(gpu->bilinear.p,frame*6+4,constants);
            transition(input,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
            if(filters){
                // Upscaled image -> filters -> swap buffer.
                run_filters(frame,gpu->final.p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                transition(gpu->final.p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
                if(hdr)hdr_pass(frame,gpu->post.p,D3D12_RESOURCE_STATE_COPY_SOURCE,output);else gpu->commands->CopyResource(output,gpu->post.p);
                transition(gpu->post.p,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
            }else{
            transition(gpu->final.p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
            if(hdr)hdr_pass(frame,gpu->final.p,D3D12_RESOURCE_STATE_COPY_SOURCE,output);else gpu->commands->CopyResource(output,gpu->final.p);
            transition(gpu->final.p,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
            }
        }
        const bool grab=shot_request.exchange(0)!=0;
        Ref<ID3D12Resource> readback;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT shot_rows=0;UINT64 shot_row_bytes=0;bool shot_bgra=false,shot_f16=false;UINT shot_w=0,shot_h=0;
        if(grab){
            try{
                const auto desc=output->GetDesc();shot_f16=desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;UINT64 total=0;shot_w=static_cast<UINT>(desc.Width);shot_h=desc.Height;shot_bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS;
                gpu->device->GetCopyableFootprints(&desc,0,1,0,&footprint,&shot_rows,&shot_row_bytes,&total);
                D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                checked(gpu->device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(readback.put())),"Screenshot buffer");
                transition(output,out_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=output;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.pResource=readback.p;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
                gpu->commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
                transition(output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);
            }catch(const std::exception& e){std::fprintf(stderr,"[D2SHOT] %s\n",e.what());if(readback.p){readback.p->Release();readback.p=nullptr;}transition(output,out_state,D3D12_RESOURCE_STATE_PRESENT);}
        }else transition(output,out_state,D3D12_RESOURCE_STATE_PRESENT);
        checked(gpu->commands->Close(),"Graphics close");ID3D12CommandList* lists[]={gpu->commands.p};gpu->queue->ExecuteCommandLists(1,lists);
        if(readback.p){
            try{
                if(!shot_fence.p)checked(gpu->device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(shot_fence.put())),"Screenshot fence");
                if(!shot_event)shot_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
                checked(gpu->queue->Signal(shot_fence.p,++shot_value),"Screenshot signal");
                if(shot_fence->GetCompletedValue()<shot_value){checked(shot_fence->SetEventOnCompletion(shot_value,shot_event),"Screenshot wait");WaitForSingleObject(shot_event,5000);}
                void* mapped=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(footprint.Footprint.RowPitch)*shot_rows};
                checked(readback->Map(0,&range,&mapped),"Screenshot map");
                std::vector<uint8_t> pixels(static_cast<size_t>(shot_w)*shot_h*4);
                if(shot_f16){
                    const float scale=hdr_display?hdr_paper_scale:1.0f;
                    for(UINT y=0;y<shot_h;++y){
                        const auto* row=reinterpret_cast<const uint16_t*>(static_cast<uint8_t*>(mapped)+static_cast<size_t>(footprint.Footprint.RowPitch)*y);
                        uint8_t* dst=&pixels[static_cast<size_t>(y)*shot_w*4];
                        for(UINT x=0;x<shot_w;++x){dst[x*4]=scrgb_to_srgb8(row[x*4],scale);dst[x*4+1]=scrgb_to_srgb8(row[x*4+1],scale);dst[x*4+2]=scrgb_to_srgb8(row[x*4+2],scale);dst[x*4+3]=255;}
                    }
                }else
                for(UINT y=0;y<shot_h;++y)std::memcpy(&pixels[static_cast<size_t>(y)*shot_w*4],static_cast<uint8_t*>(mapped)+static_cast<size_t>(footprint.Footprint.RowPitch)*y,static_cast<size_t>(shot_w)*4);
                D3D12_RANGE none{0,0};readback->Unmap(0,&none);
                std::thread(shot_save,std::move(pixels),shot_w,shot_h,shot_bgra).detach();
            }catch(const std::exception& e){std::fprintf(stderr,"[D2SHOT] %s\n",e.what());}
        }
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"[D2GRAPHICS] present: %s\n",e.what());return -1;}
}
extern "C" void d2_graphics_set_post(const float* values,int enabled){
    if(values)for(int i=0;i<8;++i)post_values[i]=values[i];
    post_enabled.store(enabled?1:0);
}
extern "C" void* d2_graphics_output(UINT frame){return gpu && frame<2?gpu->output[frame].p:nullptr;}
extern "C" void d2_graphics_shutdown(){delete gpu;gpu=nullptr;restore_display();}

extern "C" void d2_texture_dump(const uint8_t* source,uint32_t source_bytes,uint32_t w,uint32_t h,uint32_t format,int kind,const void* decoded,uint32_t pitch)
{
    d2tex::dump(source,source_bytes,w,h,format,kind,decoded,pitch);
}
extern "C" void d2_texture_set_dump(int on) { d2tex::set_dump(on!=0); }
extern "C" int d2_texture_dump_enabled(void) { return d2tex::dump_enabled()?1:0; }
extern "C" int d2_texture_pack_enabled(void) { return d2tex::pack_enabled()?1:0; }
extern "C" uint8_t* d2_texture_replacement(const uint8_t* source,uint32_t source_bytes,uint32_t w,uint32_t h,uint32_t format,uint32_t* out_w,uint32_t* out_h)
{
    return d2tex::replacement(source,source_bytes,w,h,format,out_w,out_h);
}
extern "C" void d2_texture_free(void* p) { std::free(p); }
extern "C" void d2_screenshot_request(void){shot_request.store(1);}
extern "C" unsigned d2_screenshot_last(char* path,unsigned size){
    if(path&&size){std::lock_guard<std::mutex> g(shot_lock);std::snprintf(path,size,"%s",shot_path.c_str());}
    return shot_count.load();
}
extern "C" void d2_screenshot_save(const void* pixels,uint32_t w,uint32_t h,int bgra){
    if(!pixels||!w||!h)return;
    const auto* p=static_cast<const uint8_t*>(pixels);
    std::vector<uint8_t> copy(p,p+static_cast<size_t>(w)*h*4);
    std::thread(shot_save,std::move(copy),w,h,bgra!=0).detach();
}
extern "C" void d2_graphics_set_outline(int level){outline_level.store(level<0?0:level>3?3:level);}
extern "C" int d2_graphics_outline_level(void){return outline_level.load();}
extern "C" int d2_graphics_hdr_enabled(void){return cfg.hdr?1:0;}
extern "C" void d2_graphics_set_hdr(int level){hdr_level.store(level<0?0:level>3?3:level);}
extern "C" int d2_graphics_hdr_display(void){return hdr_display?1:0;}
extern "C" void d2_graphics_hdr_disable(void){cfg.hdr=false;}
extern "C" void d2_graphics_set_pause_unfocused(int on){pause_unfocused.store(on?1:0);}
extern "C" void d2_graphics_set_menu_pause(int on){menu_pause.store(on?1:0);}
