#include "scene_depth.h"
#include "depth_steps.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cmath>
#include <limits>
#include <vector>
#include <stdexcept>
template<class T>struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D12 depth setup failed");}
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    Ref<IDXGIFactory4> factory;Ref<IDXGIAdapter> warp;Ref<ID3D12Device> device;Ref<ID3D12CommandQueue> queue;
    check(CreateDXGIFactory1(IID_IDXGIFactory4,(void**)factory.put()));
    check(factory->EnumWarpAdapter(IID_IDXGIAdapter,(void**)warp.put()));
    check(D3D12CreateDevice(warp.p,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)device.put()));
    D3D12_COMMAND_QUEUE_DESC q{};check(device->CreateCommandQueue(&q,IID_ID3D12CommandQueue,(void**)queue.put()));
    Ref<ID3D12DescriptorHeap> dsv;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;
    check(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,(void**)dsv.put()));
    auto handle=dsv->GetCPUDescriptorHandleForHeapStart();
    for(unsigned format=0;format<2;format++){
        Ref<ID3D12Resource> depth;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=65;desc.Height=32;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        desc.Format=format?DXGI_FORMAT_R24G8_TYPELESS:DXGI_FORMAT_D24_UNORM_S8_UINT;
        D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;clear.DepthStencil.Depth=1;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_ID3D12Resource,(void**)depth.put()));
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(depth.p,&view,handle);
        for(float value:{.25f,.5f,1.0f}){
            Ref<ID3D12CommandAllocator> allocator;Ref<ID3D12GraphicsCommandList> list;
            check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)allocator.put()));
            check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.p,nullptr,IID_ID3D12GraphicsCommandList,(void**)list.put()));
            list->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,value,0xA5,0,nullptr);
            check(list->Close());ID3D12CommandList* commands[]={list.p};queue->ExecuteCommandLists(1,commands);
            std::vector<float> pixels(17*9);
            require(d2_scene_depth_read(device.p,queue.p,depth.p,17,9,pixels.data())==0,"D24 full-plane copy and cropped readback succeed");
            for(float p:pixels)require(std::abs(p-value)<2.0f/16777215.0f,"D24 low bits decode independently of stencil and row padding");
            // The readback fence also covers the clear and restores source DEPTH_WRITE for the next clear.
        }
        void* steps=d2_depth_steps_create(device.p,depth.p);require(steps!=nullptr,"Timeline resources allocated");
        for(unsigned iteration=0;iteration<2;iteration++){
            d2_depth_steps_reset_after_gpu_idle(steps);
            Ref<ID3D12CommandAllocator> allocator;Ref<ID3D12GraphicsCommandList> list;
            check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)allocator.put()));
            check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.p,nullptr,IID_ID3D12GraphicsCommandList,(void**)list.put()));
            const float stages[3]={.8f,.4f,.2f};
            for(unsigned slot=0;slot<3;slot++){
                list->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,stages[slot],0,0,nullptr);
                require(d2_depth_steps_record(steps,list.p,slot)==0,"Depth snapshot recorded inside original command list");
            }
            require(d2_depth_steps_record(steps,list.p,0)<0,"Duplicate slot cannot replace an earlier moment");
            list->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,.05f,0,0,nullptr);
            check(list->Close());ID3D12CommandList* commands[]={list.p};queue->ExecuteCommandLists(1,commands);
            std::vector<float> pixels(17*9);
            for(unsigned slot=0;slot<3;slot++){
                require(d2_depth_steps_read(steps,queue.p,slot,17,9,pixels.data())==0,"Timeline crop readback succeeds");
                for(float v:pixels)require(std::abs(v-stages[slot])<2.f/16777215.f,"Snapshot preserves its moment despite subsequent clears");
            }
            require(d2_scene_depth_read(device.p,queue.p,depth.p,17,9,pixels.data())==0,"Source still usable after snapshots");
            for(float v:pixels)require(std::abs(v-.05f)<2.f/16777215.f,"Source retains final clear; recording restores its state");
        }
        d2_depth_steps_destroy_after_gpu_idle(steps);
        float out=0;require(d2_scene_depth_read(device.p,queue.p,depth.p,66,1,&out)<0,"Invalid crop rejected");
    }
    const float epsilon=8.0f/16777215.0f;
    require(d2_depth_relation(.5f,.5f,epsilon)==1,"Equal depth");
    require(d2_depth_relation(.5f+epsilon*.5f,.5f,epsilon)==1,"D24 tolerance");
    require(d2_depth_relation(.8f,.2f,epsilon)==2,"Deeper candidate");
    require(d2_depth_relation(.2f,.8f,epsilon)==3,"Nearer candidate");
    require(d2_depth_relation(std::numeric_limits<float>::quiet_NaN(),.5f,epsilon)==0,"Invalid depth rejected");
    require(d2_depth_relation(-.1f,.5f,epsilon)==0,"Out-of-range rejected");
    D2MotionPixel candidate{12,-7,1,.5f},filtered{};
    require(d2_depth_filter_pixel(&candidate,.5f,epsilon,&filtered) && filtered.coverage==1 &&
        filtered.delta_x==12 && filtered.delta_y==-7,"Equal depth retains actual vector");
    require(!d2_depth_filter_pixel(&candidate,.2f,epsilon,&filtered) && filtered.coverage==0 &&
        filtered.delta_x==0 && filtered.delta_y==0,"Behind depth rejects coverage, not a valid zero vector");
    require(!d2_depth_filter_pixel(&candidate,.8f,epsilon,&filtered),"Nearer depth stays unvalidated");
    candidate.coverage=0;require(!d2_depth_filter_pixel(&candidate,.5f,epsilon,&filtered),"Uncovered pixel stays unknown");
    candidate.coverage=1;candidate.delta_x=std::numeric_limits<float>::quiet_NaN();
    require(!d2_depth_filter_pixel(&candidate,.5f,epsilon,&filtered),"Invalid motion rejected");
    candidate.delta_x=0;candidate.delta_y=0;
    require(d2_depth_filter_pixel(&candidate,.5f,epsilon,&candidate) && candidate.coverage==1,
        "Valid stationary zero vector and in-place operation preserved");
    puts("PASS scene D24 depth: typed/typeless, padded full-plane copy, cropped output, stencil independence, restoration and relations");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
