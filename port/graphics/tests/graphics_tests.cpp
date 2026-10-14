#include "graphics.h"
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
static void ok(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D12 test HRESULT="+std::to_string((unsigned long)h));}
static void require(bool value,const char* what){if(!value)throw std::runtime_error(what);}
static void barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
int main(){try{
    Ref<ID3D12Debug> debug;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug,reinterpret_cast<void**>(debug.put()))))debug->EnableDebugLayer();
    Ref<IDXGIFactory4> factory;ok(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(factory.put())));
    Ref<IDXGIAdapter> warp;ok(factory->EnumWarpAdapter(IID_IDXGIAdapter,reinterpret_cast<void**>(warp.put())));
    Ref<ID3D12Device> device;ok(D3D12CreateDevice(warp.p,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(device.put())));
    Ref<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ok(device->CreateCommandQueue(&q,IID_ID3D12CommandQueue,reinterpret_cast<void**>(queue.put())));
    Ref<ID3D12Fence> fence;ok(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(fence.put())));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"Fence event creation");UINT64 tick=0;
    auto wait=[&](){ok(queue->Signal(fence.p,++tick));ok(fence->SetEventOnCompletion(tick,event));require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"GPU fence timed out");};
    HWND window=CreateWindowExW(0,L"STATIC",L"D2 graphics test",WS_OVERLAPPEDWINDOW,0,0,1280,720,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(window!=nullptr,"Hidden test window");
    _putenv_s("D2_MAX_REFRESH","0");d2_display_start(window);
    const struct {const char* rw;const char* rh;const char* ow;const char* oh;const char* mode;} cases[]={
        {"1280","720","1280","720","native"},
        {"1600","900","1600","900","native"},
        {"640","360","1280","720","native"},
        {"640","360","1280","720","fsr1"}};
    for(auto c:cases){
        _putenv_s("D2_RENDER_WIDTH",c.rw);_putenv_s("D2_RENDER_HEIGHT",c.rh);
        _putenv_s("D2_OUTPUT_WIDTH",c.ow);_putenv_s("D2_OUTPUT_HEIGHT",c.oh);
        _putenv_s("D2_UPSCALER",c.mode);require(d2_graphics_configure(1280,720)==0,"Configure graphics");
        require(d2_graphics_render_width()==(UINT)atoi(c.rw)&&d2_graphics_render_height()==(UINT)atoi(c.rh)&&
                d2_graphics_output_width()==(UINT)atoi(c.ow)&&d2_graphics_output_height()==(UINT)atoi(c.oh),"Requested dimensions actually active");
        DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=d2_graphics_output_width();sd.Height=d2_graphics_output_height();sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        _putenv_s("D2_VRR","0");require(d2_display_swap_flags(factory.p)==0,"Disabled VRR must preserve swap flags");
        _putenv_s("D2_VRR","1");sd.Flags=d2_display_swap_flags(factory.p);
        Ref<IDXGISwapChain1> s1;ok(factory->CreateSwapChainForHwnd(queue.p,window,&sd,nullptr,nullptr,s1.put()));
        Ref<IDXGISwapChain3> swap;ok(s1->QueryInterface(IID_IDXGISwapChain3,reinterpret_cast<void**>(swap.put())));
        require(d2_graphics_start(device.p,queue.p,swap.p)==0,"Graphics GPU initialization/shaders");
        Ref<ID3D12Resource> scene;int owned=d2_graphics_scene(0,reinterpret_cast<void**>(scene.put()));require(owned>=0,"Scene texture");
        if(!owned)ok(swap->GetBuffer(0,IID_ID3D12Resource,reinterpret_cast<void**>(scene.put())));
        auto sceneDesc=scene->GetDesc();require(sceneDesc.Width==d2_graphics_render_width()&&sceneDesc.Height==d2_graphics_render_height(),"Actual internal raster dimensions");
        Ref<ID3D12DescriptorHeap> rtv;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
        ok(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(rtv.put())));
        auto handle=rtv->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(scene.p,nullptr,handle);
        Ref<ID3D12CommandAllocator> allocator;ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,reinterpret_cast<void**>(allocator.put())));
        Ref<ID3D12GraphicsCommandList> list;ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.p,nullptr,IID_ID3D12GraphicsCommandList,reinterpret_cast<void**>(list.put())));
        ok(list->Close());
        for(int iteration=0;iteration<3;iteration++){
            ok(allocator->Reset());ok(list->Reset(allocator.p,nullptr));
            float color[]={0.2f+iteration*0.1f,0.4f,0.6f,1.0f};
            barrier(list.p,scene.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
            list->ClearRenderTargetView(handle,color,0,nullptr);
            barrier(list.p,scene.p,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);
            ok(list->Close());ID3D12CommandList* lists[]={list.p};queue->ExecuteCommandLists(1,lists);wait();
            require(d2_graphics_present(0,scene.p)==0,"GPU scaling pass");wait();
            Ref<ID3D12Resource> backbuffer;ok(swap->GetBuffer(0,IID_ID3D12Resource,reinterpret_cast<void**>(backbuffer.put())));
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowSize=0,bytes=0;
            auto desc=backbuffer->GetDesc();device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowSize,&bytes);
            D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;Ref<ID3D12Resource> readback;
            ok(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(readback.put())));
            ok(allocator->Reset());ok(list->Reset(allocator.p,nullptr));barrier(list.p,backbuffer.p,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=readback.p;destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;
            D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=backbuffer.p;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);barrier(list.p,backbuffer.p,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);
            ok(list->Close());queue->ExecuteCommandLists(1,lists);wait();
            unsigned char* data=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};ok(readback->Map(0,&range,reinterpret_cast<void**>(&data)));
            for(UINT y:{0u,sd.Height/2,sd.Height-1})for(UINT x:{0u,sd.Width/2,sd.Width-1})for(UINT component=0;component<4;component++){
                int actual=data[y*footprint.Footprint.RowPitch+x*4+component];int expected=(int)std::round(color[component]*255.0f);
                require(std::abs(actual-expected)<=3,"GPU color/edge preservation or stale frame");
            }
            readback->Unmap(0,nullptr);
        }
        require(d2_display_present(swap.p,0)==0,"VRR-capable presentation");
        require(d2_display_present(swap.p,1)==0,"VSync must suppress tearing flag");
        _putenv_s("D2_VRR","0");require(d2_display_present(swap.p,0)==0,"Disabled VRR presentation");
        wait();d2_graphics_shutdown();std::printf("PASS raster=%sx%s output=%sx%s mode=%s, 3 GPU frames\n",c.rw,c.rh,c.ow,c.oh,c.mode);
        // A flip-model chain must be destroyed before creating another on this HWND.
        DestroyWindow(window);window=CreateWindowExW(0,L"STATIC",L"D2 graphics test",WS_OVERLAPPEDWINDOW,0,0,1280,720,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    }
    Ref<ID3D12InfoQueue> info;
    if(SUCCEEDED(device->QueryInterface(IID_ID3D12InfoQueue,reinterpret_cast<void**>(info.put())))){
        for(UINT64 i=0;i<info->GetNumStoredMessages();i++){
            SIZE_T size=0;info->GetMessage(i,nullptr,&size);auto* message=(D3D12_MESSAGE*)std::malloc(size);ok(info->GetMessage(i,message,&size));
            bool bad=message->Severity==D3D12_MESSAGE_SEVERITY_ERROR||message->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION;
            if(bad)std::fprintf(stderr,"%s\n",message->pDescription);std::free(message);require(!bad,"D3D12 validation error");
        }
    }
    _putenv_s("D2_UPSCALER","native");_putenv_s("GCM_COMPOSITE_RT","1");
    require(d2_graphics_configure(1280,720)!=0,"Debug compositor cannot bypass scaling contracts");
    _putenv_s("GCM_COMPOSITE_RT","");
    _putenv_s("D2_UPSCALER","dlss");require(d2_graphics_configure(1280,720)!=0,"Unsupported DLSS cannot masquerade as active");
    DestroyWindow(window);CloseHandle(event);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());d2_graphics_shutdown();return 1;}}
