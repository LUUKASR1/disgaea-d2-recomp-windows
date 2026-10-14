#include "motion_raster.h"
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cstdio>
#include <memory>
#include <vector>
namespace {
template<class T>struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
struct Failure{HRESULT hr;};void check(HRESULT h){if(FAILED(h))throw Failure{h};}
struct Context{
    Ref<ID3D12CommandAllocator> allocator;Ref<ID3D12GraphicsCommandList> list;
    Ref<ID3D12RootSignature> root;Ref<ID3D12PipelineState> pipelines[8];
    Ref<ID3D12Resource> upload,color,depth,readback;
    Ref<ID3D12DescriptorHeap> rtv,dsv;Ref<ID3D12Fence> fence;
    HANDLE event=nullptr;~Context(){if(event)CloseHandle(event);}
};
const char shader[]=R"(
cbuffer Size:register(b0){float2 dimensions;}
struct Vertex {float4 current:POSITION;float4 previous:TEXCOORD0;};
struct Varying {float4 position:SV_Position;float4 previous:TEXCOORD0;};
Varying vs(Vertex v){Varying o;o.position=v.current;o.previous=v.previous;return o;}
float4 ps(Varying v):SV_Target{
    if(!all(isfinite(v.previous)) || v.previous.w<=1e-8)discard;
    float2 previous_uv=float2(v.previous.x/v.previous.w*.5+.5,.5-v.previous.y/v.previous.w*.5);
    float2 delta=(previous_uv-v.position.xy/dimensions)*dimensions;
    if(!all(isfinite(delta)))discard;
    return float4(delta,1,v.position.z);
})";
}
extern "C" int d2_motion_raster_gpu(void* dp,void* qp,const D2MotionDraw* draws,uint32_t count,
    uint32_t width,uint32_t height,D2MotionPixel* pixels){
    if(!dp || !qp || !draws || !pixels || !count || count>64 || !width || !height ||
       width>4096 || height>4096 || (uint64_t)width*height>8388608)return (int)E_INVALIDARG;
    uint64_t vertices=0;
    for(unsigned i=0;i<count;i++){
        if(!draws[i].vertices || !draws[i].count || draws[i].count%3 || draws[i].count>4096 || draws[i].cull>7)return (int)E_INVALIDARG;
        vertices+=draws[i].count;
    }
    std::memset(pixels,0,(size_t)width*height*sizeof(*pixels));
    auto device=static_cast<ID3D12Device*>(dp);auto queue=static_cast<ID3D12CommandQueue*>(qp);
    auto c=std::make_unique<Context>();bool submitted=false,completed=false;
    try{
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)c->allocator.put()));
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,c->allocator.p,nullptr,IID_ID3D12GraphicsCommandList,(void**)c->list.put()));
        D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameter.Constants.Num32BitValues=2;
        D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&parameter;rd.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Ref<ID3DBlob> sig,error,vs,ps;
        check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,sig.put(),error.put()));
        check(device->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_ID3D12RootSignature,(void**)c->root.put()));
        auto compile=[&](const char* entry,const char* target,Ref<ID3DBlob>& output){
            Ref<ID3DBlob> errors;HRESULT hr=D3DCompile(shader,sizeof(shader)-1,"D2 candidate motion raster",nullptr,nullptr,entry,target,
                D3DCOMPILE_ENABLE_STRICTNESS,0,output.put(),errors.put());
            if(FAILED(hr) && errors.p)std::fprintf(stderr,"[D2MOTION] raster shader: %.*s\n",(int)errors->GetBufferSize(),(const char*)errors->GetBufferPointer());
            check(hr);
        };
        compile("vs","vs_5_0",vs);compile("ps","ps_5_0",ps);
        D3D12_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        for(unsigned i=0;i<count;i++){
            unsigned key=draws[i].cull;if(c->pipelines[key].p)continue;
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=c->root.p;
            pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
            pd.InputLayout={elements,2};pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pd.SampleMask=UINT_MAX;pd.SampleDesc.Count=1;pd.NumRenderTargets=1;
            pd.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;pd.DSVFormat=DXGI_FORMAT_D32_FLOAT;
            pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
            pd.RasterizerState.CullMode=!(key&1)?D3D12_CULL_MODE_NONE:key&2?D3D12_CULL_MODE_FRONT:D3D12_CULL_MODE_BACK;
            pd.RasterizerState.FrontCounterClockwise=(key&4)?TRUE:FALSE;
            pd.RasterizerState.DepthClipEnable=TRUE;
            auto& blend=pd.BlendState.RenderTarget[0];
            blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
            blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
            blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;
            blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
            pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
            pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
            pd.DepthStencilState.StencilReadMask=pd.DepthStencilState.StencilWriteMask=255;
            pd.DepthStencilState.FrontFace.StencilFailOp=pd.DepthStencilState.FrontFace.StencilDepthFailOp=
                pd.DepthStencilState.FrontFace.StencilPassOp=D3D12_STENCIL_OP_KEEP;
            pd.DepthStencilState.FrontFace.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;
            pd.DepthStencilState.BackFace=pd.DepthStencilState.FrontFace;
            check(device->CreateGraphicsPipelineState(&pd,IID_ID3D12PipelineState,(void**)c->pipelines[key].put()));
        }
        auto resource=[&](Ref<ID3D12Resource>& out,D3D12_RESOURCE_DESC& desc,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear){
            D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
            check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,clear,IID_ID3D12Resource,(void**)out.put()));
        };
        D3D12_RESOURCE_DESC tex{};tex.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex.Width=width;tex.Height=height;tex.DepthOrArraySize=1;tex.MipLevels=1;tex.SampleDesc.Count=1;
        tex.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};clear.Format=tex.Format;
        resource(c->color,tex,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear);
        tex.Format=DXGI_FORMAT_D32_FLOAT;tex.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        clear.Format=tex.Format;clear.DepthStencil.Depth=1;
        resource(c->depth,tex,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        check(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,(void**)c->rtv.put()));
        hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        check(device->CreateDescriptorHeap(&hd,IID_ID3D12DescriptorHeap,(void**)c->dsv.put()));
        auto rtv=c->rtv->GetCPUDescriptorHandleForHeapStart(),dsv=c->dsv->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(c->color.p,nullptr,rtv);device->CreateDepthStencilView(c->depth.p,nullptr,dsv);
        auto buffer=[&](Ref<ID3D12Resource>& out,uint64_t bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
            desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resource(out,desc,type,state,nullptr);
        };
        buffer(c->upload,vertices*sizeof(D2MotionVertex),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;auto color_desc=c->color->GetDesc();
        device->GetCopyableFootprints(&color_desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
        buffer(c->readback,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        void* mapped=nullptr;D3D12_RANGE none{0,0};check(c->upload->Map(0,&none,&mapped));
        uint64_t start=0;for(unsigned i=0;i<count;i++){std::memcpy((D2MotionVertex*)mapped+start,draws[i].vertices,draws[i].count*sizeof(D2MotionVertex));start+=draws[i].count;}
        c->upload->Unmap(0,nullptr);
        float zero[4]={0};c->list->ClearRenderTargetView(rtv,zero,0,nullptr);
        c->list->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
        c->list->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
        D3D12_VIEWPORT viewport{0,0,(float)width,(float)height,0,1};D3D12_RECT scissor{0,0,(LONG)width,(LONG)height};
        c->list->RSSetViewports(1,&viewport);c->list->RSSetScissorRects(1,&scissor);
        c->list->SetGraphicsRootSignature(c->root.p);float dimensions[2]={(float)width,(float)height};
        c->list->SetGraphicsRoot32BitConstants(0,2,dimensions,0);
        c->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VERTEX_BUFFER_VIEW vb{c->upload->GetGPUVirtualAddress(),(UINT)(vertices*sizeof(D2MotionVertex)),sizeof(D2MotionVertex)};
        c->list->IASetVertexBuffers(0,1,&vb);
        unsigned offset=0;for(unsigned i=0;i<count;i++){
            c->list->SetPipelineState(c->pipelines[draws[i].cull].p);c->list->DrawInstanced(draws[i].count,1,offset,0);offset+=draws[i].count;
        }
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={c->color.p,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
        c->list->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=c->readback.p;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        src.pResource=c->color.p;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        c->list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);check(c->list->Close());
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)c->fence.put()));
        c->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!c->event)throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        ID3D12CommandList* lists[]={c->list.p};queue->ExecuteCommandLists(1,lists);submitted=true;
        check(queue->Signal(c->fence.p,1));check(c->fence->SetEventOnCompletion(1,c->event));
        if(WaitForSingleObject(c->event,30000)!=WAIT_OBJECT_0)throw Failure{HRESULT_FROM_WIN32(WAIT_TIMEOUT)};
        completed=true;check(device->GetDeviceRemovedReason());
        D3D12_RANGE read{0,(SIZE_T)bytes};check(c->readback->Map(0,&read,&mapped));
        for(unsigned y=0;y<height;y++)std::memcpy(pixels+(size_t)y*width,(const char*)mapped+footprint.Offset+(size_t)y*footprint.Footprint.RowPitch,width*sizeof(*pixels));
        c->readback->Unmap(0,&none);return 0;
    }catch(const Failure& f){if(submitted && !completed)c.release();return (int)f.hr;}
    catch(...){if(submitted && !completed)c.release();return (int)E_FAIL;}
}
