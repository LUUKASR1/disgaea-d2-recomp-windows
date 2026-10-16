#include "depth_steps.h"
#include "scene_depth.h"
#include <windows.h>
#include <d3d12.h>
#include <memory>
namespace {
template<class T>struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
struct Steps{Ref<ID3D12Device> device;Ref<ID3D12Resource> source,snapshot[3];bool recorded[3]{};};
}
extern "C" void* d2_depth_steps_create(void* dp,void* sp){
    if(!dp||!sp)return nullptr;
    auto device=static_cast<ID3D12Device*>(dp);auto source=static_cast<ID3D12Resource*>(sp);auto desc=source->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.DepthOrArraySize!=1||desc.MipLevels!=1||
       desc.SampleDesc.Count!=1||desc.Width>4096||desc.Height>4096||
       (desc.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT&&desc.Format!=DXGI_FORMAT_R24G8_TYPELESS))return nullptr;
    try{
    auto s=std::make_unique<Steps>();s->device.p=device;device->AddRef();s->source.p=source;source->AddRef();
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    for(auto& snapshot:s->snapshot)if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_ID3D12Resource,(void**)snapshot.put())))return nullptr;
    return s.release();
    }catch(...){return nullptr;}
}
extern "C" void d2_depth_steps_reset_after_gpu_idle(void* p){if(p){auto s=static_cast<Steps*>(p);for(auto& r:s->recorded)r=false;}}
extern "C" void d2_depth_steps_destroy_after_gpu_idle(void* p){delete static_cast<Steps*>(p);}
extern "C" int d2_depth_steps_record(void* p,void* lp,uint32_t slot){
    if(!p||!lp||slot>=3)return (int)E_INVALIDARG;auto s=static_cast<Steps*>(p);
    if(s->recorded[slot])return (int)E_INVALIDARG;auto list=static_cast<ID3D12GraphicsCommandList*>(lp);
    D3D12_RESOURCE_BARRIER b[2]{};for(auto& barrier:b)barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition={s->source.p,0,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE};
    b[1].Transition={s->snapshot[slot].p,0,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_DEST};
    list->ResourceBarrier(2,b);
    D3D12_TEXTURE_COPY_LOCATION source{},target{};source.pResource=s->source.p;target.pResource=s->snapshot[slot].p;
    source.Type=target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;source.SubresourceIndex=target.SubresourceIndex=0;
    list->CopyTextureRegion(&target,0,0,0,&source,nullptr);
    b[0].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    b[1].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b[1].Transition.StateAfter=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    list->ResourceBarrier(2,b);s->recorded[slot]=true;return 0;
}
extern "C" int d2_depth_steps_read(void* p,void* queue,uint32_t slot,uint32_t width,uint32_t height,float* output){
    if(!p||slot>=3)return (int)E_INVALIDARG;auto s=static_cast<Steps*>(p);
    if(!s->recorded[slot])return (int)E_INVALIDARG;
    return d2_scene_depth_read(s->device.p,queue,s->snapshot[slot].p,width,height,output);
}
