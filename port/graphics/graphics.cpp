#include "graphics.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <string>
#define A_CPU 1
#include "vendor/fsr1/ffx_a.h"
#include "vendor/fsr1/ffx_fsr1.h"
#include "fsr_shaders.h"

namespace {
template<class T> struct Ref {
    T* p=nullptr;
    Ref()=default; Ref(const Ref&)=delete; Ref& operator=(const Ref&)=delete;
    ~Ref(){if(p)p->Release();}
    T** put(){if(p)p->Release();p=nullptr;return &p;}
    T* operator->()const{return p;}
};
struct Config {UINT gw=1280,gh=720,rw=1280,rh=720,ow=1280,oh=720;bool active=false,fsr=false;float sharpness=0.2f;} cfg;
struct State {
    ID3D12Device* device=nullptr; ID3D12CommandQueue* queue=nullptr;
    std::array<Ref<ID3D12Resource>,2> output;
    std::array<Ref<ID3D12CommandAllocator>,2> allocator;
    Ref<ID3D12GraphicsCommandList> commands;
    Ref<ID3D12Resource> middle,final;
    Ref<ID3D12DescriptorHeap> heap;
    Ref<ID3D12RootSignature> root;
    Ref<ID3D12PipelineState> easu,rcas,bilinear;
    UINT stride=0;
};
State* gpu=nullptr;
void checked(HRESULT hr,const char* what){if(FAILED(hr))throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(static_cast<unsigned long>(hr)));}
UINT dimension(const char* key,UINT fallback){const char* s=std::getenv(key);if(!s)return fallback;char* end=nullptr;auto n=std::strtoul(s,&end,10);if(!*s||*end||n<320||n>4096)throw std::runtime_error("Invalid graphics dimension");return static_cast<UINT>(n);}
void texture(ID3D12Resource** out,UINT w,UINT h,D3D12_RESOURCE_FLAGS flags){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
    d.DepthOrArraySize=1;d.MipLevels=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Flags=flags;
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
void descriptors(UINT index,ID3D12Resource* input,ID3D12Resource* output){
    auto cpu=gpu->heap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=index*gpu->stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    gpu->device->CreateShaderResourceView(input,&srv,cpu);cpu.ptr+=gpu->stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R8G8B8A8_UNORM;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    gpu->device->CreateUnorderedAccessView(output,nullptr,&uav,cpu);
}
void dispatch(ID3D12PipelineState* p,UINT index,const UINT constants[20]){
    auto h=gpu->heap->GetGPUDescriptorHandleForHeapStart();h.ptr+=static_cast<UINT64>(index)*gpu->stride;
    gpu->commands->SetPipelineState(p);gpu->commands->SetComputeRoot32BitConstants(0,20,constants,0);
    gpu->commands->SetComputeRootDescriptorTable(1,h);gpu->commands->Dispatch((cfg.ow+7)/8,(cfg.oh+7)/8,1);
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
extern "C" int d2_display_present(void* object,UINT interval){
    if(!object)return -1;auto* swap=static_cast<IDXGISwapChain3*>(object);
    UINT flags=0;DXGI_SWAP_CHAIN_DESC1 desc{};BOOL fullscreen=TRUE;
    if(flag("D2_VRR") && interval==0 && SUCCEEDED(swap->GetDesc1(&desc)) &&
       (desc.Flags&DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) && SUCCEEDED(swap->GetFullscreenState(&fullscreen,nullptr)) && !fullscreen)
        flags=DXGI_PRESENT_ALLOW_TEARING;
    HRESULT result=swap->Present(interval,flags);
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
        cfg.active=cfg.fsr||cfg.rw!=w||cfg.rh!=h||cfg.ow!=w||cfg.oh!=h||(hud && !std::strcmp(hud,"1"));
        if(cfg.active&&(std::getenv("GCM_GUEST_FB")||std::getenv("SUBVP_RTT")||std::getenv("GCM_COMPOSITE_RT")))throw std::runtime_error("Experimental framebuffer override incompatible with graphics scaling");
        std::fprintf(stderr,"[D2GRAPHICS] render=%ux%u output=%ux%u mode=%s DLSS=unavailable FG=unavailable\n",cfg.rw,cfg.rh,cfg.ow,cfg.oh,cfg.fsr?"FSR1-EASU-RCAS":"native");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"[D2GRAPHICS] %s\n",e.what());return -1;}
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
        if(!cfg.fsr&&cfg.rw==cfg.ow&&cfg.rh==cfg.oh)return 0;
        texture(gpu->final.put(),cfg.ow,cfg.oh,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if(cfg.fsr)texture(gpu->middle.put(),cfg.ow,cfg.oh,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=12;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
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
        if(cfg.fsr){pipeline(d2_easu,gpu->easu);pipeline(d2_rcas,gpu->rcas);}else pipeline(d2_bilinear,gpu->bilinear);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"[D2GRAPHICS] start: %s\n",e.what());delete gpu;gpu=nullptr;return -1;}
}
extern "C" int d2_graphics_scene(UINT frame,void** resource){
    if(!cfg.active)return 0;
    try{if(!gpu||frame>=2||!resource)throw std::runtime_error("Invalid graphics scene");
        auto** out=reinterpret_cast<ID3D12Resource**>(resource);texture(out,cfg.rw,cfg.rh,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        if(gpu->heap.p){if(cfg.fsr){descriptors(frame*6,*out,gpu->middle.p);descriptors(frame*6+2,gpu->middle.p,gpu->final.p);}else descriptors(frame*6+4,*out,gpu->final.p);}
        return 1;
    }catch(const std::exception& e){std::fprintf(stderr,"[D2GRAPHICS] scene: %s\n",e.what());return -1;}
}
extern "C" int d2_graphics_present(UINT frame,void* scene){
    if(!cfg.active)return 0;
    try{if(!gpu||frame>=2)throw std::runtime_error("Invalid graphics present");
        auto* input=static_cast<ID3D12Resource*>(scene);auto* output=gpu->output[frame].p;
        if(gpu->heap.p){if(cfg.fsr)descriptors(frame*6,input,gpu->middle.p);else descriptors(frame*6+4,input,gpu->final.p);}
        checked(gpu->allocator[frame]->Reset(),"Graphics allocator reset");
        checked(gpu->commands->Reset(gpu->allocator[frame].p,nullptr),"Graphics list reset");
        transition(output,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST);
        if(!cfg.fsr&&cfg.rw==cfg.ow&&cfg.rh==cfg.oh){
            transition(input,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);gpu->commands->CopyResource(output,input);
            transition(input,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
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
            transition(gpu->final.p,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
            gpu->commands->CopyResource(output,gpu->final.p);
            transition(gpu->final.p,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
        }
        transition(output,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);
        checked(gpu->commands->Close(),"Graphics close");ID3D12CommandList* lists[]={gpu->commands.p};gpu->queue->ExecuteCommandLists(1,lists);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"[D2GRAPHICS] present: %s\n",e.what());return -1;}
}
extern "C" void* d2_graphics_output(UINT frame){return gpu && frame<2?gpu->output[frame].p:nullptr;}
extern "C" void d2_graphics_shutdown(){delete gpu;gpu=nullptr;restore_display();}
