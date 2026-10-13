#include "graphics.h"
#include "hud_capture.h"
#include "hud_present.h"
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
void ok(HRESULT h){if(FAILED(h))throw std::runtime_error("Output test HRESULT="+std::to_string((unsigned long)h));}
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    D3D12_RESOURCE_BARRIER item{};item.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    item.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&item);
}
int main(){try{
    _putenv_s("D2_HUD_AFTER_UPSCALE","1");
    Ref<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug,reinterpret_cast<void**>(debug.put()))))debug->EnableDebugLayer();
    Ref<IDXGIFactory4> factory;ok(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(factory.put())));
    Ref<IDXGIAdapter> warp;ok(factory->EnumWarpAdapter(IID_IDXGIAdapter,reinterpret_cast<void**>(warp.put())));
    Ref<ID3D12Device> device;ok(D3D12CreateDevice(warp.p,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(device.put())));
    Ref<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};ok(device->CreateCommandQueue(&q,IID_ID3D12CommandQueue,reinterpret_cast<void**>(queue.put())));
    Ref<ID3D12CommandAllocator> allocator;ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,reinterpret_cast<void**>(allocator.put())));
    Ref<ID3D12GraphicsCommandList> list;ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.p,nullptr,IID_ID3D12GraphicsCommandList,reinterpret_cast<void**>(list.put())));ok(list->Close());
    Ref<ID3D12Fence> fence;ok(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(fence.put())));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"Output fence event");UINT64 tick=0;
    auto wait=[&](){ok(queue->Signal(fence.p,++tick));ok(fence->SetEventOnCompletion(tick,event));require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"Output test fence timeout");};
    auto submit=[&](){ok(list->Close());ID3D12CommandList* commands[]={list.p};queue->ExecuteCommandLists(1,commands);wait();};
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameter.Constants.Num32BitValues=4;
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&parameter;
    Ref<ID3DBlob> blob,error;ok(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,blob.put(),error.put()));
    Ref<ID3D12RootSignature> root;ok(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,reinterpret_cast<void**>(root.put())));
    const char* vs="cbuffer C:register(b0){float4 tint;} float4 main(uint id:SV_VertexID):SV_POSITION{float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],tint.w,1);}";
    const char* ps="cbuffer C:register(b0){float4 tint;} float4 main():SV_TARGET{return float4(tint.rgb,0.5);}";
    Ref<ID3DBlob> v,p;ok(D3DCompile(vs,std::strlen(vs),"output VS",nullptr,nullptr,"main","vs_5_0",0,0,v.put(),error.put()));
    ok(D3DCompile(ps,std::strlen(ps),"output PS",nullptr,nullptr,"main","ps_5_0",0,0,p.put(),error.put()));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.p;pd.VS={v->GetBufferPointer(),v->GetBufferSize()};pd.PS={p->GetBufferPointer(),p->GetBufferSize()};
    pd.SampleMask=UINT_MAX;pd.SampleDesc.Count=1;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;pd.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;
    pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pd.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};pd.DepthStencilState.BackFace=pd.DepthStencilState.FrontFace;
    auto& blend=pd.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp=D3D12_BLEND_OP_ADD;blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.RenderTargetWriteMask=15;
    Ref<ID3D12PipelineState> hud,depth_only;ok(device->CreateGraphicsPipelineState(&pd,IID_ID3D12PipelineState,reinterpret_cast<void**>(hud.put())));
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask=0;pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    ok(device->CreateGraphicsPipelineState(&pd,IID_ID3D12PipelineState,reinterpret_cast<void**>(depth_only.put())));
    auto draw=[&](ID3D12GraphicsCommandList* command,ID3D12PipelineState* pso,D3D12_RECT rect,const float* tint){
        command->SetGraphicsRootSignature(root.p);command->SetPipelineState(pso);command->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command->RSSetScissorRects(1,&rect);command->SetGraphicsRoot32BitConstants(0,4,tint,0);command->DrawInstanced(3,1,0,0);
    };
    const struct {UINT rw,rh,ow,oh;const char* mode;} cases[]={{1280,720,1280,720,"native"},{640,360,1280,720,"native"},{640,360,1280,720,"fsr1"}};
    for(auto c:cases){
        _putenv_s("D2_RENDER_WIDTH",std::to_string(c.rw).c_str());_putenv_s("D2_RENDER_HEIGHT",std::to_string(c.rh).c_str());
        _putenv_s("D2_OUTPUT_WIDTH",std::to_string(c.ow).c_str());_putenv_s("D2_OUTPUT_HEIGHT",std::to_string(c.oh).c_str());_putenv_s("D2_UPSCALER",c.mode);
        require(d2_graphics_configure(1280,720)==0,"Configure HUD output");
        HWND window=CreateWindowExW(0,L"STATIC",L"HUD output test",WS_OVERLAPPEDWINDOW,0,0,c.ow,c.oh,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"Output test window");
        DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=c.ow;sd.Height=c.oh;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        Ref<IDXGISwapChain1> swap1;ok(factory->CreateSwapChainForHwnd(queue.p,window,&sd,nullptr,nullptr,swap1.put()));
        Ref<IDXGISwapChain3> swap;ok(swap1->QueryInterface(IID_IDXGISwapChain3,reinterpret_cast<void**>(swap.put())));
        require(d2_graphics_start(device.p,queue.p,swap.p)==0,"Start HUD graphics");
        std::array<Ref<ID3D12Resource>,2> scene;
        for(UINT frame=0;frame<2;frame++)require(d2_graphics_scene(frame,reinterpret_cast<void**>(scene[frame].put()))==1,"HUD mode needs an owned scene even at native size");
        D3D12_RESOURCE_DESC z{};z.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;z.Width=c.rw;z.Height=c.rh;z.DepthOrArraySize=1;z.MipLevels=1;z.Format=DXGI_FORMAT_R24G8_TYPELESS;z.SampleDesc.Count=1;z.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;Ref<ID3D12Resource> depth;
        ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&z,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(depth.put())));
        Ref<ID3D12DescriptorHeap> rtv,dsv;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=2;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ok(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(rtv.put())));auto base=rtv->GetCPUDescriptorHandleForHeapStart();UINT stride=device->GetDescriptorHandleIncrementSize(hd.Type);
        for(UINT frame=0;frame<2;frame++){auto handle=base;handle.ptr+=frame*stride;device->CreateRenderTargetView(scene[frame].p,nullptr,handle);}
        hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;ok(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(dsv.put())));
        auto dh=dsv->GetCPUDescriptorHandleForHeapStart();D3D12_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;device->CreateDepthStencilView(depth.p,&dv,dh);
        require(d2_posthud_prepare(device.p,queue.p,scene[0].p,depth.p,d2_graphics_output(0))==0,"Initialize post-HUD depth resampling");
        for(UINT iteration=0;iteration<3;iteration++){
            UINT frame=swap->GetCurrentBackBufferIndex();wait();d2_posthud_frame(frame);
            ok(allocator->Reset());ok(list->Reset(allocator.p,nullptr));auto rh=base;rh.ptr+=frame*stride;
            transition(list.p,scene[frame].p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
            float background[4]={.2f+iteration*.1f,.4f,.6f,1};list->ClearRenderTargetView(rh,background,0,nullptr);
            list->ClearDepthStencilView(dh,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.9f,0,0,nullptr);
            list->OMSetRenderTargets(1,&rh,FALSE,&dh);D3D12_VIEWPORT viewport{0,0,(float)c.rw,(float)c.rh,0,1};list->RSSetViewports(1,&viewport);
            const float near_depth[4]={0,0,0,.25f};draw(list.p,depth_only.p,{(LONG)c.rw/2,0,(LONG)c.rw,(LONG)c.rh},near_depth);
            require(d2_posthud_snapshot(frame,list.p,scene[frame].p,depth.p)==0,"Snapshot prior to HUD");
            const float green[4]={0,1,0,.5f},red[4]={1,0,0,.5f};UINT old_x=17*c.rw/c.ow,old_right=(c.ow-18)*c.rw/c.ow;
            draw(list.p,hud.p,{(LONG)old_x,0,(LONG)old_x+1,(LONG)c.rh},green);
            draw(list.p,hud.p,{(LONG)old_right,0,(LONG)old_right+1,(LONG)c.rh},red);
            transition(list.p,scene[frame].p,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);submit();
            d2_posthud_keep(frame,0,hud.p);require(d2_posthud_pipeline(frame,0)==hud.p,"Retained original pipeline");
            require(d2_graphics_present(frame,d2_posthud_scene(frame))==0,"Upscale scene without internal HUD");
            auto* command=static_cast<ID3D12GraphicsCommandList*>(d2_posthud_begin(frame,d2_graphics_output(frame)));require(command!=nullptr,"Begin output HUD");
            draw(command,static_cast<ID3D12PipelineState*>(d2_posthud_pipeline(frame,0)),{17,0,18,(LONG)c.oh},green);
            draw(command,hud.p,{(LONG)c.ow-18,0,(LONG)c.ow-17,(LONG)c.oh},red);
            require(d2_posthud_end(frame)==0,"Submit output HUD");wait();
            if(c.rw==c.ow){
                require(d2_hud_prepare(device.p,queue.p,scene[frame].p,depth.p)==0,"Prepare independent native comparison");
                uint64_t rgb=9,alpha=9;UINT maximum=9;
                require(d2_hud_compare_pair(scene[frame].p,d2_graphics_output(frame),&rgb,&alpha,&maximum)==0 && rgb==0 && alpha==0,
                    "Native output must equal original HUD/depth composition");
                d2_hud_shutdown();
            }
            auto* output=static_cast<ID3D12Resource*>(d2_graphics_output(frame));auto desc=output->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
            device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
            D3D12_RESOURCE_DESC rb{};rb.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rb.Width=bytes;rb.Height=1;rb.DepthOrArraySize=1;rb.MipLevels=1;rb.SampleDesc.Count=1;rb.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES readheap{};readheap.Type=D3D12_HEAP_TYPE_READBACK;Ref<ID3D12Resource> readback;
            ok(device->CreateCommittedResource(&readheap,D3D12_HEAP_FLAG_NONE,&rb,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(readback.put())));
            ok(allocator->Reset());ok(list->Reset(allocator.p,nullptr));transition(list.p,output,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=readback.p;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;src.pResource=output;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);transition(list.p,output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);submit();
            unsigned char* pixels=nullptr;D3D12_RANGE range{0,(SIZE_T)bytes};ok(readback->Map(0,&range,reinterpret_cast<void**>(&pixels)));
            for(UINT y:{0u,c.oh/2,c.oh-1})for(UINT x:{16u,17u,18u,c.ow-18})for(UINT channel=0;channel<4;channel++){
                float expected=background[channel];if(x==17 && channel<3)expected=.5f*background[channel]+(channel==1?.5f:0);
                int actual=pixels[y*footprint.Footprint.RowPitch+x*4+channel];require(std::abs(actual-(int)std::round(expected*255))<=3,"Output-pixel HUD width / depth / stale internal HUD leak");
            }
            readback->Unmap(0,nullptr);std::printf("PASS output HUD %ux%u -> %ux%u mode=%s frame=%u: 1px HUD and depth preserved\n",c.rw,c.rh,c.ow,c.oh,c.mode,iteration);
            ok(swap->Present(0,0));wait();
        }
        wait();d2_posthud_shutdown();d2_graphics_shutdown();DestroyWindow(window);
    }
    Ref<ID3D12InfoQueue> info;if(SUCCEEDED(device->QueryInterface(IID_ID3D12InfoQueue,reinterpret_cast<void**>(info.put())))){
        for(UINT64 i=0;i<info->GetNumStoredMessages();i++){
            SIZE_T size=0;info->GetMessage(i,nullptr,&size);auto* message=static_cast<D3D12_MESSAGE*>(std::malloc(size));ok(info->GetMessage(i,message,&size));
            bool bad=message->Severity==D3D12_MESSAGE_SEVERITY_ERROR||message->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION;
            if(bad)std::fprintf(stderr,"%s\n",message->pDescription);std::free(message);require(!bad,"Post-HUD D3D12 validation error");
        }
    }
    CloseHandle(event);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());d2_posthud_shutdown();d2_hud_shutdown();d2_graphics_shutdown();return 1;}}
