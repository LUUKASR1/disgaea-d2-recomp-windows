#include "hud_capture.h"
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <string>

template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
void ok(HRESULT h){if(FAILED(h))throw std::runtime_error("Test HRESULT="+std::to_string((unsigned long)h));}
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    D3D12_RESOURCE_BARRIER transition{};transition.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&transition);
}

int main(){try{
    float vertices[4][64]{};for(auto& p:vertices){p[2]=.95f;p[3]=1;}
    require(d2_hud_vertices(vertices,4,256),"all screen vertices accepted");
    vertices[3][2]=.999f;require(!d2_hud_vertices(vertices,4,256),"fourth world vertex rejected");
    vertices[3][2]=.95f;vertices[3][3]=2;require(!d2_hud_vertices(vertices,4,256),"non-unit W rejected");
    require(!d2_hud_vertices(nullptr,4,256),"missing geometry rejected");
    Ref<ID3D12Debug> debug;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug,reinterpret_cast<void**>(debug.put()))))debug->EnableDebugLayer();
    Ref<IDXGIFactory4> factory;ok(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(factory.put())));
    Ref<IDXGIAdapter> warp;ok(factory->EnumWarpAdapter(IID_IDXGIAdapter,reinterpret_cast<void**>(warp.put())));
    Ref<ID3D12Device> device;ok(D3D12CreateDevice(warp.p,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(device.put())));
    Ref<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queue_desc{};
    ok(device->CreateCommandQueue(&queue_desc,IID_ID3D12CommandQueue,reinterpret_cast<void**>(queue.put())));
    Ref<ID3D12CommandAllocator> allocator;ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,reinterpret_cast<void**>(allocator.put())));
    Ref<ID3D12GraphicsCommandList> list;ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.p,nullptr,IID_ID3D12GraphicsCommandList,reinterpret_cast<void**>(list.put())));
    ok(list->Close());Ref<ID3D12Fence> fence;ok(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(fence.put())));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"test fence event");UINT64 tick=0;
    auto submit=[&](){ok(list->Close());ID3D12CommandList* commands[]={list.p};queue->ExecuteCommandLists(1,commands);
        ok(queue->Signal(fence.p,++tick));ok(fence->SetEventOnCompletion(tick,event));require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"test fence timeout");};
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=64;desc.Height=64;
    desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Ref<ID3D12Resource> color;ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(color.put())));
    desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    Ref<ID3D12Resource> depth;ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(depth.put())));
    Ref<ID3D12DescriptorHeap> rtv,dsv;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ok(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(rtv.put())));
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;ok(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(dsv.put())));
    auto rh=rtv->GetCPUDescriptorHandleForHeapStart(),dh=dsv->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(color.p,nullptr,rh);device->CreateDepthStencilView(depth.p,nullptr,dh);
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameter.Constants.Num32BitValues=4;
    D3D12_ROOT_SIGNATURE_DESC root_desc{};root_desc.NumParameters=1;root_desc.pParameters=&parameter;
    Ref<ID3DBlob> signature,error;ok(D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,signature.put(),error.put()));
    Ref<ID3D12RootSignature> root;ok(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_ID3D12RootSignature,reinterpret_cast<void**>(root.put())));
    const char* vs="cbuffer C:register(b0){float4 tint;} float4 main(uint id:SV_VertexID):SV_POSITION{float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],tint.w,1);}";
    const char* ps="cbuffer C:register(b0){float4 tint;} float4 main():SV_TARGET{return float4(tint.rgb,0.5);}";
    Ref<ID3DBlob> v,p;ok(D3DCompile(vs,std::strlen(vs),"HUD test VS",nullptr,nullptr,"main","vs_5_0",0,0,v.put(),error.put()));
    ok(D3DCompile(ps,std::strlen(ps),"HUD test PS",nullptr,nullptr,"main","ps_5_0",0,0,p.put(),error.put()));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.p;pd.VS={v->GetBufferPointer(),v->GetBufferSize()};pd.PS={p->GetBufferPointer(),p->GetBufferSize()};
    pd.SampleMask=UINT_MAX;pd.SampleDesc.Count=1;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;pd.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;
    pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=pd.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp=D3D12_BLEND_OP_ADD;blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;pd.DepthStencilState.StencilEnable=TRUE;
    pd.DepthStencilState.StencilReadMask=255;pd.DepthStencilState.StencilWriteMask=255;
    auto stencil=D3D12_DEPTH_STENCILOP_DESC{D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_EQUAL};
    pd.DepthStencilState.FrontFace=stencil;pd.DepthStencilState.BackFace=stencil;
    Ref<ID3D12PipelineState> pso;ok(device->CreateGraphicsPipelineState(&pd,IID_ID3D12PipelineState,reinterpret_cast<void**>(pso.put())));
    auto draws=[&](ID3D12GraphicsCommandList* command){
        command->SetGraphicsRootSignature(root.p);command->SetPipelineState(pso.p);command->OMSetStencilRef(3);
        command->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        float green[4]={0,1,0,.5f};D3D12_RECT left{0,0,32,64};command->RSSetScissorRects(1,&left);
        command->SetGraphicsRoot32BitConstants(0,4,green,0);command->DrawInstanced(3,1,0,0);
        float red[4]={1,0,0,.95f};D3D12_RECT right{32,0,64,64};command->RSSetScissorRects(1,&right);
        command->SetGraphicsRoot32BitConstants(0,4,red,0);command->DrawInstanced(3,1,0,0);
    };
    for(unsigned frame=0;frame<3;frame++){
        require(d2_hud_prepare(device.p,queue.p,color.p,depth.p)==0,"prepare replay GPU resources");
        ok(allocator->Reset());ok(list->Reset(allocator.p,nullptr));
        barrier(list.p,color.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
        float background[4]={.2f+frame*.1f,.3f,.4f,1};list->ClearRenderTargetView(rh,background,0,nullptr);
        list->ClearDepthStencilView(dh,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.9f,3,0,nullptr);
        require(d2_hud_snapshot(list.p,color.p,depth.p)==0,"snapshot scene and D24S8 planes");
        list->OMSetRenderTargets(1,&rh,FALSE,&dh);D3D12_VIEWPORT viewport{0,0,64,64,0,1};list->RSSetViewports(1,&viewport);
        draws(list.p);barrier(list.p,color.p,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);submit();
        for(unsigned pass=0;pass<2;pass++){
            auto* command=static_cast<ID3D12GraphicsCommandList*>(d2_hud_begin(pass));require(command!=nullptr,"begin isolated/composed replay");
            draws(command);require(d2_hud_end()==0,"end replay");
        }
        uint64_t rgb=99,alpha=99;uint32_t maximum=99;
        require(d2_hud_compare(color.p,&rgb,&alpha,&maximum)==0,"compare GPU pixels");
        require(rgb==0 && alpha==0 && maximum==0,"Recomposition must exactly match native blend/depth/stencil");
        require(d2_hud_compare(d2_hud_resource(0),&rgb,&alpha,&maximum)==0 && rgb==2048 && alpha==0 && maximum>0,
            "HUD must draw on the left; restored depth/stencil must reject the right draw");
        require(d2_hud_compare(d2_hud_resource(1),&rgb,&alpha,&maximum)==0 && alpha==4096,
            "Isolated HUD must retain transparent background instead of copying the scene");
        std::printf("PASS HUD GPU replay frame %u: RGB=0 alpha=0 max=0\n",frame);
        // Corrupt the reference: ensure the comparison detects an actual difference.
        ok(allocator->Reset());ok(list->Reset(allocator.p,nullptr));
        barrier(list.p,color.p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float changed[4]={1,0,1,1};list->ClearRenderTargetView(rh,changed,0,nullptr);
        barrier(list.p,color.p,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);submit();
        require(d2_hud_compare(color.p,&rgb,&alpha,&maximum)==0 && rgb>0 && maximum>0,"Changed pixels must not pass comparison");
        d2_hud_shutdown();
    }
    Ref<ID3D12InfoQueue> info;
    if(SUCCEEDED(device->QueryInterface(IID_ID3D12InfoQueue,reinterpret_cast<void**>(info.put())))){
        for(UINT64 i=0;i<info->GetNumStoredMessages();i++){
            SIZE_T size=0;info->GetMessage(i,nullptr,&size);auto* message=static_cast<D3D12_MESSAGE*>(std::malloc(size));
            ok(info->GetMessage(i,message,&size));bool bad=message->Severity==D3D12_MESSAGE_SEVERITY_ERROR||message->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION;
            if(bad)std::fprintf(stderr,"%s\n",message->pDescription);std::free(message);require(!bad,"D3D12 validation error");
        }
    }
    CloseHandle(event);std::puts("Full geometry, scene snapshot, isolated HUD, exact replay and mismatch detection passed");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());d2_hud_shutdown();return 1;}}
