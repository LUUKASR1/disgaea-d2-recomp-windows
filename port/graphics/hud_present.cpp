#include "hud_present.h"
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <array>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <climits>

namespace {
template<class T> struct Ref {
    T* p=nullptr;~Ref(){if(p)p->Release();}
    Ref()=default;Ref(const Ref&)=delete;Ref& operator=(const Ref&)=delete;
    T** put(){if(p)p->Release();p=nullptr;return &p;}
    T* operator->()const{return p;}
};
void check(HRESULT h,const char* message){if(FAILED(h))throw std::runtime_error(std::string(message)+" HRESULT="+std::to_string((unsigned long)h));}
void barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    D3D12_RESOURCE_BARRIER item{};item.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    item.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&item);
}
struct State {
    ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
    UINT rw=0,rh=0,ow=0,oh=0,rtv_stride=0;
    std::array<Ref<ID3D12Resource>,2> scene,depth,output_depth;
    std::array<Ref<ID3D12CommandAllocator>,2> allocator;
    std::array<std::vector<ID3D12PipelineState*>,2> pipelines;
    Ref<ID3D12GraphicsCommandList> list;
    Ref<ID3D12DescriptorHeap> rtv,dsv,srv;
    Ref<ID3D12RootSignature> root;
    Ref<ID3D12PipelineState> depth_pso;
    std::array<bool,2> snapped{false,false};
    ID3D12Resource* recording_output=nullptr;int recording_frame=-1;
    void release(UINT frame){for(auto* p:pipelines[frame])if(p)p->Release();pipelines[frame].clear();snapped[frame]=false;}
    ~State(){release(0);release(1);}
};
std::unique_ptr<State> gpu;
void texture(State& s,Ref<ID3D12Resource>& target,D3D12_RESOURCE_DESC desc){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    check(s.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,
        IID_ID3D12Resource,reinterpret_cast<void**>(target.put())),"Post-HUD texture");
}
const char* depth_vs=R"(
float4 main(uint vertex:SV_VertexID):SV_POSITION {
    float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[vertex],0,1);
})";
const char* depth_ps=R"(
Texture2D<float> sourceDepth:register(t0);
cbuffer Sizes:register(b0){uint2 inputSize;uint2 outputSize;}
float main(float4 position:SV_POSITION):SV_Depth {
    uint2 pixel=min(uint2(position.xy*float2(inputSize)/float2(outputSize)),inputSize-1);
    return sourceDepth.Load(int3(pixel,0));
})";
int fail(const std::exception& e){std::fprintf(stderr,"[D2POSTHUD] %s\n",e.what());return -1;}
}
extern "C" int d2_posthud_enabled(void){
    static int enabled=-1;
    if(enabled<0){const char* value=std::getenv("D2_HUD_AFTER_UPSCALE");enabled=value && !std::strcmp(value,"1");}
    return enabled;
}
extern "C" void d2_posthud_frame(UINT frame){if(gpu && frame<2)gpu->release(frame);}
extern "C" int d2_posthud_prepare(void* device,void* queue,void* color,void* depth,void* output){
    try{
        if(!device||!queue||!color||!depth||!output)throw std::runtime_error("Post-HUD GPU inputs missing");
        auto c=static_cast<ID3D12Resource*>(color)->GetDesc(),z=static_cast<ID3D12Resource*>(depth)->GetDesc();
        auto o=static_cast<ID3D12Resource*>(output)->GetDesc();
        if(c.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||o.Format!=c.Format||z.Format!=DXGI_FORMAT_R24G8_TYPELESS||
            c.SampleDesc.Count!=1||z.SampleDesc.Count!=1||o.SampleDesc.Count!=1||c.Width>4096||c.Height>4096||
            o.Width>4096||o.Height>4096||c.DepthOrArraySize!=1||z.DepthOrArraySize!=1||o.DepthOrArraySize!=1)
            throw std::runtime_error("Unsupported post-HUD GPU format");
        if(gpu){
            if(gpu->device!=device||gpu->rw!=c.Width||gpu->rh!=c.Height||gpu->ow!=o.Width||gpu->oh!=o.Height)
                throw std::runtime_error("Post-HUD dimensions changed during execution");
            return 0;
        }
        auto s=std::make_unique<State>();s->device=static_cast<ID3D12Device*>(device);s->queue=static_cast<ID3D12CommandQueue*>(queue);
        s->rw=static_cast<UINT>(c.Width);s->rh=c.Height;s->ow=static_cast<UINT>(o.Width);s->oh=o.Height;
        auto out_depth=z;out_depth.Width=s->ow;out_depth.Height=s->oh;
        for(UINT frame=0;frame<2;frame++){
            texture(*s,s->scene[frame],c);texture(*s,s->depth[frame],z);texture(*s,s->output_depth[frame],out_depth);
            check(s->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,
                reinterpret_cast<void**>(s->allocator[frame].put())),"Post-HUD allocator");
        }
        D3D12_DESCRIPTOR_HEAP_DESC h{};h.NumDescriptors=2;h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        check(s->device->CreateDescriptorHeap(&h,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(s->rtv.put())),"Post-HUD RTV heap");
        s->rtv_stride=s->device->GetDescriptorHandleIncrementSize(h.Type);
        h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        check(s->device->CreateDescriptorHeap(&h,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(s->dsv.put())),"Post-HUD DSV heap");
        auto dh=s->dsv->GetCPUDescriptorHandleForHeapStart();UINT dstride=s->device->GetDescriptorHandleIncrementSize(h.Type);
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        for(auto& resource:s->output_depth){s->device->CreateDepthStencilView(resource.p,&view,dh);dh.ptr+=dstride;}
        h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(s->device->CreateDescriptorHeap(&h,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(s->srv.put())),"Post-HUD depth SRV heap");
        auto sh=s->srv->GetCPUDescriptorHandleForHeapStart();UINT stride=s->device->GetDescriptorHandleIncrementSize(h.Type);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        for(auto& resource:s->depth){s->device->CreateShaderResourceView(resource.p,&srv,sh);sh.ptr+=stride;}
        D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
        D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants.Num32BitValues=4;
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={1,&range};
        D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;
        Ref<ID3DBlob> blob,error;check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,blob.put(),error.put()),"Post-HUD depth root signature");
        check(s->device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,
            reinterpret_cast<void**>(s->root.put())),"Post-HUD depth root");
        Ref<ID3DBlob> vs,ps;check(D3DCompile(depth_vs,std::strlen(depth_vs),"HUD depth VS",nullptr,nullptr,"main","vs_5_0",0,0,vs.put(),error.put()),"Post-HUD depth VS");
        check(D3DCompile(depth_ps,std::strlen(depth_ps),"HUD depth PS",nullptr,nullptr,"main","ps_5_0",0,0,ps.put(),error.put()),"Post-HUD depth PS");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=s->root.p;p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        p.SampleMask=UINT_MAX;p.SampleDesc.Count=1;p.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;
        p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
        p.DepthStencilState.DepthEnable=TRUE;p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        p.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
        p.DepthStencilState.BackFace=p.DepthStencilState.FrontFace;
        check(s->device->CreateGraphicsPipelineState(&p,IID_ID3D12PipelineState,reinterpret_cast<void**>(s->depth_pso.put())),"Post-HUD depth pipeline");
        check(s->device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s->allocator[0].p,nullptr,
            IID_ID3D12GraphicsCommandList,reinterpret_cast<void**>(s->list.put())),"Post-HUD command list");check(s->list->Close(),"Post-HUD initial close");
        gpu=std::move(s);std::fprintf(stderr,"[D2POSTHUD] initialized render=%ux%u output=%ux%u\n",gpu->rw,gpu->rh,gpu->ow,gpu->oh);return 0;
    }catch(const std::exception& e){return fail(e);}
}
extern "C" int d2_posthud_snapshot(UINT frame,void* commands,void* color,void* depth){
    if(!gpu||frame>=2||!commands||!color||!depth)return -1;
    auto* list=static_cast<ID3D12GraphicsCommandList*>(commands);auto* source=static_cast<ID3D12Resource*>(color);auto* z=static_cast<ID3D12Resource*>(depth);
    barrier(list,source,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(list,z,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(list,gpu->scene[frame].p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
    barrier(list,gpu->depth[frame].p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(gpu->scene[frame].p,source);list->CopyResource(gpu->depth[frame].p,z);
    barrier(list,gpu->scene[frame].p,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
    barrier(list,gpu->depth[frame].p,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
    barrier(list,source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    barrier(list,z,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);gpu->snapped[frame]=true;return 0;
}
extern "C" void* d2_posthud_scene(UINT frame){return gpu && frame<2 && gpu->snapped[frame]?gpu->scene[frame].p:nullptr;}
extern "C" void d2_posthud_keep(UINT frame,UINT index,void* value){
    if(!gpu||frame>=2||!value||index>65535)return;auto* p=static_cast<ID3D12PipelineState*>(value);
    auto& items=gpu->pipelines[frame];if(items.size()<=index)items.resize(index+1,nullptr);
    if(items[index])items[index]->Release();p->AddRef();items[index]=p;
}
extern "C" void* d2_posthud_pipeline(UINT frame,UINT index){return !gpu||frame>=2||index>=gpu->pipelines[frame].size()?nullptr:gpu->pipelines[frame][index];}
extern "C" void* d2_posthud_begin(UINT frame,void* target){
    try{
        if(!gpu||frame>=2||!target||!gpu->snapped[frame]||gpu->recording_frame>=0)throw std::runtime_error("Invalid post-HUD frame");
        auto* output=static_cast<ID3D12Resource*>(target);auto desc=output->GetDesc();
        if(desc.Width!=gpu->ow||desc.Height!=gpu->oh)throw std::runtime_error("Post-HUD output size mismatch");
        check(gpu->allocator[frame]->Reset(),"Post-HUD allocator reset");check(gpu->list->Reset(gpu->allocator[frame].p,nullptr),"Post-HUD reset");
        auto* list=gpu->list.p;
        barrier(list,gpu->depth[frame].p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        barrier(list,gpu->output_depth[frame].p,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        auto dh=gpu->dsv->GetCPUDescriptorHandleForHeapStart();dh.ptr+=frame*gpu->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        list->ClearDepthStencilView(dh,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1,0,0,nullptr);
        list->OMSetRenderTargets(0,nullptr,FALSE,&dh);
        D3D12_VIEWPORT viewport{0,0,static_cast<float>(gpu->ow),static_cast<float>(gpu->oh),0,1};D3D12_RECT rect{0,0,static_cast<LONG>(gpu->ow),static_cast<LONG>(gpu->oh)};
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&rect);
        list->SetGraphicsRootSignature(gpu->root.p);list->SetPipelineState(gpu->depth_pso.p);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap* heaps[]={gpu->srv.p};list->SetDescriptorHeaps(1,heaps);
        auto sh=gpu->srv->GetGPUDescriptorHandleForHeapStart();sh.ptr+=frame*gpu->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT sizes[4]={gpu->rw,gpu->rh,gpu->ow,gpu->oh};list->SetGraphicsRoot32BitConstants(0,4,sizes,0);list->SetGraphicsRootDescriptorTable(1,sh);list->DrawInstanced(3,1,0,0);
        barrier(list,output,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rh=gpu->rtv->GetCPUDescriptorHandleForHeapStart();rh.ptr+=frame*gpu->rtv_stride;
        gpu->device->CreateRenderTargetView(output,nullptr,rh);list->OMSetRenderTargets(1,&rh,FALSE,&dh);
        gpu->recording_output=output;gpu->recording_frame=static_cast<int>(frame);return list;
    }catch(const std::exception& e){fail(e);return nullptr;}
}
extern "C" int d2_posthud_end(UINT frame){
    try{
        if(!gpu||frame>=2||gpu->recording_frame!=static_cast<int>(frame))throw std::runtime_error("Post-HUD not recording");
        barrier(gpu->list.p,gpu->depth[frame].p,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        barrier(gpu->list.p,gpu->output_depth[frame].p,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COMMON);
        barrier(gpu->list.p,gpu->recording_output,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
        check(gpu->list->Close(),"Post-HUD close");ID3D12CommandList* lists[]={gpu->list.p};gpu->queue->ExecuteCommandLists(1,lists);
        gpu->recording_frame=-1;gpu->recording_output=nullptr;return 0;
    }catch(const std::exception& e){return fail(e);}
}
extern "C" void d2_posthud_shutdown(void){gpu.reset();}
