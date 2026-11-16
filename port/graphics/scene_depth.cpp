#include "scene_depth.h"
#include <windows.h>
#include <d3d12.h>
#include <memory>
#include <cstring>
namespace {
template<class T>struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
struct Failure{HRESULT hr;};void check(HRESULT h){if(FAILED(h))throw Failure{h};}
struct Context{
    Ref<ID3D12CommandAllocator> allocator;Ref<ID3D12GraphicsCommandList> list;
    Ref<ID3D12Resource> readback,source;Ref<ID3D12Fence> fence;
    HANDLE event=nullptr;~Context(){if(event)CloseHandle(event);}
};
}
extern "C" int d2_scene_depth_read(void* dp,void* qp,void* source,uint32_t width,uint32_t height,float* output){
    if(!dp||!qp||!source||!output||!width||!height)return (int)E_INVALIDARG;
    auto device=static_cast<ID3D12Device*>(dp);auto queue=static_cast<ID3D12CommandQueue*>(qp);
    auto depth=static_cast<ID3D12Resource*>(source);auto desc=depth->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count!=1 ||
       desc.DepthOrArraySize!=1 || desc.MipLevels!=1 || desc.Width>4096 || desc.Height>4096 ||
       width>desc.Width || height>desc.Height ||
       (desc.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT && desc.Format!=DXGI_FORMAT_R24G8_TYPELESS))return (int)E_INVALIDARG;
    auto c=std::make_unique<Context>();c->source.p=depth;depth->AddRef();bool submitted=false,completed=false;
    try{
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
        device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
        if(!bytes||bytes==UINT64_MAX||footprint.Footprint.RowPitch<desc.Width*4)throw Failure{E_INVALIDARG};
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
        buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
            IID_ID3D12Resource,(void**)c->readback.put()));
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)c->allocator.put()));
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,c->allocator.p,nullptr,IID_ID3D12GraphicsCommandList,(void**)c->list.put()));
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={depth,0,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE};
        c->list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=c->readback.p;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint=footprint;src.pResource=depth;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=0;
        c->list->CopyTextureRegion(&dst,0,0,0,&src,nullptr); // Entire depth-plane rectangle, as D3D12 requires.
        b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_DEPTH_WRITE;
        c->list->ResourceBarrier(1,&b);check(c->list->Close());
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)c->fence.put()));
        c->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!c->event)throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        ID3D12CommandList* lists[]={c->list.p};queue->ExecuteCommandLists(1,lists);submitted=true;
        check(queue->Signal(c->fence.p,1));check(c->fence->SetEventOnCompletion(1,c->event));
        if(WaitForSingleObject(c->event,30000)!=WAIT_OBJECT_0)throw Failure{HRESULT_FROM_WIN32(WAIT_TIMEOUT)};
        completed=true;check(device->GetDeviceRemovedReason());
        D3D12_RANGE range{0,(SIZE_T)bytes},none{0,0};void* mapped=nullptr;check(c->readback->Map(0,&range,&mapped));
        for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
            uint32_t packed=0;std::memcpy(&packed,(const char*)mapped+footprint.Offset+(size_t)y*footprint.Footprint.RowPitch+x*4,4);
            output[(size_t)y*width+x]=(float)((double)(packed&0xffffffu)/16777215.0);
        }
        c->readback->Unmap(0,&none);return 0;
    }catch(const Failure& f){if(submitted&&!completed)c.release();return (int)f.hr;}
    catch(...){if(submitted&&!completed)c.release();return (int)E_FAIL;}
}
