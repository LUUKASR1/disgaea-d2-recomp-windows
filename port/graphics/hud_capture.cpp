#include "hud_capture.h"
#include <windows.h>
#include <d3d12.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template<class T> struct Ref {
    T* p=nullptr; ~Ref(){if(p)p->Release();}
    Ref()=default; Ref(const Ref&)=delete; Ref& operator=(const Ref&)=delete;
    T** put(){if(p)p->Release();p=nullptr;return &p;}
    T* operator->()const{return p;}
};
void check(HRESULT hr,const char* message){if(FAILED(hr))throw std::runtime_error(std::string(message)+" HRESULT="+std::to_string((unsigned long)hr));}
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    if(before==after)return;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
struct State {
    ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
    Ref<ID3D12Resource> scene,depth_snapshot,replay_depth;
    std::array<Ref<ID3D12Resource>,2> color;
    Ref<ID3D12DescriptorHeap> rtv,dsv;
    Ref<ID3D12CommandAllocator> allocator;
    Ref<ID3D12GraphicsCommandList> list;
    Ref<ID3D12Fence> fence;
    HANDLE event=nullptr;
    UINT64 tick=0;UINT width=0,height=0,stride=0,pass=0;
    bool snapshot=false,recording=false;
    ~State(){if(event)CloseHandle(event);}
    void reset(){check(allocator->Reset(),"HUD allocator reset");check(list->Reset(allocator.p,nullptr),"HUD list reset");}
    void submit(){
        check(list->Close(),"HUD close");ID3D12CommandList* lists[]={list.p};queue->ExecuteCommandLists(1,lists);
        check(queue->Signal(fence.p,++tick),"HUD fence signal");
        check(fence->SetEventOnCompletion(tick,event),"HUD fence event");
        if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("HUD diagnostic GPU timeout");
    }
};
std::unique_ptr<State> gpu;
void texture(State& s,Ref<ID3D12Resource>& target,D3D12_RESOURCE_DESC desc){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    check(s.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,
        IID_ID3D12Resource,reinterpret_cast<void**>(target.put())),"HUD resource creation");
}
std::vector<unsigned char> read(ID3D12Resource* resource){
    auto desc=resource->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes=0;gpu->device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
    D3D12_RESOURCE_DESC rb{};rb.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rb.Width=bytes;rb.Height=1;
    rb.DepthOrArraySize=1;rb.MipLevels=1;rb.SampleDesc.Count=1;rb.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;Ref<ID3D12Resource> buffer;
    check(gpu->device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rb,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
        IID_ID3D12Resource,reinterpret_cast<void**>(buffer.put())),"HUD readback creation");
    gpu->reset();transition(gpu->list.p,resource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=buffer.p;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint=footprint;src.pResource=resource;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu->list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    transition(gpu->list.p,resource,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);gpu->submit();
    void* mapped=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};check(buffer->Map(0,&range,&mapped),"HUD readback map");
    std::vector<unsigned char> pixels(static_cast<size_t>(gpu->width)*gpu->height*4);
    for(UINT y=0;y<gpu->height;y++)std::memcpy(pixels.data()+static_cast<size_t>(y)*gpu->width*4,
        static_cast<unsigned char*>(mapped)+footprint.Offset+static_cast<size_t>(y)*footprint.Footprint.RowPitch,gpu->width*4);
    D3D12_RANGE written{0,0};buffer->Unmap(0,&written);return pixels;
}
int failure(const std::exception& error){std::fprintf(stderr,"[D2HUD] %s\n",error.what());return -1;}
}

extern "C" int d2_hud_vertices(const void* data,uint32_t count,uint32_t stride){
    if(!data||count<3||count>262144||stride<16||stride>4096||stride%4)return 0;
    for(uint32_t i=0;i<count;i++){
        float p[4];std::memcpy(p,static_cast<const unsigned char*>(data)+static_cast<size_t>(i)*stride,16);
        for(float v:p)if(!std::isfinite(v))return 0;
        if(std::abs(p[3]-1)>1e-5f||p[2]<0||p[2]>=0.99f)return 0;
    }
    return 1;
}
extern "C" int d2_hud_prepare(void* device,void* queue,void* color,void* depth){
    try{
        if(!device||!queue||!color||!depth)throw std::runtime_error("Missing diagnostic GPU resources");
        auto c=static_cast<ID3D12Resource*>(color)->GetDesc(),d=static_cast<ID3D12Resource*>(depth)->GetDesc();
        if(c.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||c.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||
           c.SampleDesc.Count!=1||c.MipLevels!=1||c.DepthOrArraySize!=1||c.Width>4096||c.Height>4096||
           (d.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT && d.Format!=DXGI_FORMAT_R24G8_TYPELESS)||d.SampleDesc.Count!=1||d.Width<c.Width||d.Height<c.Height)
            throw std::runtime_error("Unsupported HUD diagnostic surface");
        auto state=std::make_unique<State>();state->device=static_cast<ID3D12Device*>(device);state->queue=static_cast<ID3D12CommandQueue*>(queue);
        state->width=static_cast<UINT>(c.Width);state->height=c.Height;
        c.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        texture(*state,state->scene,c);for(auto& r:state->color)texture(*state,r,c);
        texture(*state,state->depth_snapshot,d);texture(*state,state->replay_depth,d);
        D3D12_DESCRIPTOR_HEAP_DESC h{};h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;h.NumDescriptors=2;
        check(state->device->CreateDescriptorHeap(&h,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(state->rtv.put())),"HUD RTV heap");
        state->stride=state->device->GetDescriptorHandleIncrementSize(h.Type);
        auto handle=state->rtv->GetCPUDescriptorHandleForHeapStart();
        for(auto& r:state->color){state->device->CreateRenderTargetView(r.p,nullptr,handle);handle.ptr+=state->stride;}
        h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;h.NumDescriptors=1;
        check(state->device->CreateDescriptorHeap(&h,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(state->dsv.put())),"HUD DSV heap");
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dsv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        state->device->CreateDepthStencilView(state->replay_depth.p,&dsv,state->dsv->GetCPUDescriptorHandleForHeapStart());
        check(state->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,
            reinterpret_cast<void**>(state->allocator.put())),"HUD allocator");
        check(state->device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,state->allocator.p,nullptr,
            IID_ID3D12GraphicsCommandList,reinterpret_cast<void**>(state->list.put())),"HUD list");check(state->list->Close(),"HUD initial close");
        check(state->device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(state->fence.put())),"HUD fence");
        state->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!state->event)throw std::runtime_error("HUD fence event creation");
        gpu=std::move(state);return 0;
    }catch(const std::exception& error){return failure(error);}
}
extern "C" int d2_hud_snapshot(void* command_list,void* color,void* depth){
    if(!gpu||!command_list||!color||!depth)return -1;
    auto* list=static_cast<ID3D12GraphicsCommandList*>(command_list);
    auto* source=static_cast<ID3D12Resource*>(color);auto* z=static_cast<ID3D12Resource*>(depth);
    transition(list,source,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(list,z,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(list,gpu->scene.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
    transition(list,gpu->depth_snapshot.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(gpu->scene.p,source);list->CopyResource(gpu->depth_snapshot.p,z);
    transition(list,gpu->scene.p,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
    transition(list,gpu->depth_snapshot.p,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
    transition(list,source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition(list,z,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    gpu->snapshot=true;return 0;
}
extern "C" void* d2_hud_begin(uint32_t pass){
    try{
        if(!gpu||!gpu->snapshot||pass>1||gpu->recording)throw std::runtime_error("Invalid HUD replay pass");
        gpu->reset();gpu->pass=pass;auto* list=gpu->list.p;auto* target=gpu->color[pass].p;
        if(pass){
            transition(list,gpu->scene.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition(list,target,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(target,gpu->scene.p);
            transition(list,gpu->scene.p,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
            transition(list,target,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
        }else transition(list,target,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition(list,gpu->depth_snapshot.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(list,gpu->replay_depth.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(gpu->replay_depth.p,gpu->depth_snapshot.p);
        transition(list,gpu->depth_snapshot.p,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
        transition(list,gpu->replay_depth.p,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        auto rtv=gpu->rtv->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=pass*gpu->stride;
        auto dsv=gpu->dsv->GetCPUDescriptorHandleForHeapStart();list->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
        if(!pass){const float clear[4]={0,0,0,0};list->ClearRenderTargetView(rtv,clear,0,nullptr);}
        D3D12_VIEWPORT viewport{0,0,static_cast<float>(gpu->width),static_cast<float>(gpu->height),0,1};
        D3D12_RECT rect{0,0,static_cast<LONG>(gpu->width),static_cast<LONG>(gpu->height)};
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);gpu->recording=true;return list;
    }catch(const std::exception& error){failure(error);return nullptr;}
}
extern "C" int d2_hud_end(void){
    try{
        if(!gpu||!gpu->recording)throw std::runtime_error("HUD replay not recording");
        transition(gpu->list.p,gpu->color[gpu->pass].p,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);
        transition(gpu->list.p,gpu->replay_depth.p,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COMMON);
        gpu->submit();gpu->recording=false;return 0;
    }catch(const std::exception& error){return failure(error);}
}
extern "C" void* d2_hud_resource(uint32_t kind){return !gpu||kind>2?nullptr:kind==0?gpu->scene.p:gpu->color[kind-1].p;}
extern "C" int d2_hud_compare_pair(void* reference,void* actual,uint64_t* rgb,uint64_t* alpha,uint32_t* maximum){
    try{
        if(!gpu||gpu->recording||!reference||!actual||!rgb||!alpha||!maximum)throw std::runtime_error("Invalid HUD comparison");
        auto* original=static_cast<ID3D12Resource*>(reference);auto desc=original->GetDesc();
        if(desc.Width!=gpu->width||desc.Height!=gpu->height||desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)throw std::runtime_error("HUD comparison dimensions differ");
        auto* comparison=static_cast<ID3D12Resource*>(actual);auto other=comparison->GetDesc();
        if(other.Width!=desc.Width||other.Height!=desc.Height||other.Format!=desc.Format)throw std::runtime_error("Comparison target dimensions differ");
        auto a=read(original),b=read(comparison);*rgb=0;*alpha=0;*maximum=0;
        for(size_t i=0;i<a.size();i+=4){bool changed=false;for(size_t c=0;c<3;c++){
            uint32_t delta=static_cast<uint32_t>(std::abs(static_cast<int>(a[i+c])-static_cast<int>(b[i+c])));
            *maximum=std::max(*maximum,delta);changed|=delta!=0;
        }*rgb+=changed;*alpha+=a[i+3]!=b[i+3];}return 0;
    }catch(const std::exception& error){return failure(error);}
}
extern "C" int d2_hud_compare(void* reference,uint64_t* rgb,uint64_t* alpha,uint32_t* maximum){
    return d2_hud_compare_pair(reference,gpu?gpu->color[1].p:nullptr,rgb,alpha,maximum);
}
extern "C" void d2_hud_shutdown(void){gpu.reset();}
