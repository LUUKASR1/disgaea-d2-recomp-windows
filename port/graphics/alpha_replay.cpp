#include "alpha_replay.h"
#include "scene_depth.h"
#include <windows.h>
#include <d3d12.h>
#include <memory>
#include <vector>
#include <cstring>
namespace {
template<class T>struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
struct Failure{HRESULT hr;};void check(HRESULT h){if(FAILED(h))throw Failure{h};}
struct Context{
    Ref<ID3D12CommandAllocator> allocator;Ref<ID3D12GraphicsCommandList> list;
    Ref<ID3D12Resource> color,depth;Ref<ID3D12DescriptorHeap> rtv,dsv;Ref<ID3D12Fence> fence;
    std::vector<IUnknown*> borrowed;HANDLE event=nullptr;
    ~Context(){for(auto p:borrowed)p->Release();if(event)CloseHandle(event);}
};
}
extern "C" int d2_alpha_replay(void* dp,void* qp,uint32_t width,uint32_t height,
    D2AlphaRecord record,void* user,void* const* borrowed,uint32_t borrowed_count,unsigned char* mask){
    if(!dp||!qp||!record||!mask||!width||!height||width>4096||height>4096||
       (uint64_t)width*height>8388608||borrowed_count>256||(!borrowed&&borrowed_count))return (int)E_INVALIDARG;
    std::memset(mask,0,(size_t)width*height);
    auto device=static_cast<ID3D12Device*>(dp);auto queue=static_cast<ID3D12CommandQueue*>(qp);
    auto c=std::make_unique<Context>();bool submitted=false,completed=false;
    try{
        c->borrowed.reserve(borrowed_count);
        for(unsigned i=0;i<borrowed_count;i++)if(borrowed[i]){
            auto p=static_cast<IUnknown*>(borrowed[i]);c->borrowed.push_back(p);p->AddRef();
        }
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=width;td.Height=height;
        td.DepthOrArraySize=1;td.MipLevels=1;td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};clear.Format=td.Format;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,
            IID_ID3D12Resource,(void**)c->color.put()));
        td.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        clear.Format=td.Format;clear.DepthStencil.Depth=1;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,
            IID_ID3D12Resource,(void**)c->depth.put()));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        check(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,(void**)c->rtv.put()));
        hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        check(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,(void**)c->dsv.put()));
        auto rt=c->rtv->GetCPUDescriptorHandleForHeapStart(),ds=c->dsv->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(c->color.p,nullptr,rt);device->CreateDepthStencilView(c->depth.p,nullptr,ds);
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)c->allocator.put()));
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,c->allocator.p,nullptr,IID_ID3D12GraphicsCommandList,(void**)c->list.put()));
        float black[4]={0};c->list->ClearRenderTargetView(rt,black,0,nullptr);
        c->list->ClearDepthStencilView(ds,(D3D12_CLEAR_FLAGS)(D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL),1,0,0,nullptr);
        c->list->OMSetRenderTargets(1,&rt,FALSE,&ds);
        D3D12_VIEWPORT vp{0,0,(float)width,(float)height,0,1};D3D12_RECT sc{0,0,(LONG)width,(LONG)height};
        c->list->RSSetViewports(1,&vp);c->list->RSSetScissorRects(1,&sc);
        record(c->list.p,user);check(c->list->Close());
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)c->fence.put()));
        c->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!c->event)throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        ID3D12CommandList* lists[]={c->list.p};queue->ExecuteCommandLists(1,lists);submitted=true;
        check(queue->Signal(c->fence.p,1));check(c->fence->SetEventOnCompletion(1,c->event));
        if(WaitForSingleObject(c->event,30000)!=WAIT_OBJECT_0)throw Failure{HRESULT_FROM_WIN32(WAIT_TIMEOUT)};
        completed=true;check(device->GetDeviceRemovedReason());
        std::vector<float> depth((size_t)width*height);
        check((HRESULT)d2_scene_depth_read(dp,qp,c->depth.p,width,height,depth.data()));
        for(size_t i=0;i<depth.size();i++)mask[i]=isfinite(depth[i])&&depth[i]>=0&&depth[i]<1?255:0;
        return 0;
    }catch(const Failure& f){if(submitted&&!completed)c.release();return (int)f.hr;}
    catch(...){if(submitted&&!completed)c.release();return (int)E_FAIL;}
}
