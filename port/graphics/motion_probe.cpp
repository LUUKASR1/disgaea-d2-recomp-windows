#include "motion_probe.h"
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace {
template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
struct Failure {HRESULT hr;};
void check(HRESULT h){if(FAILED(h))throw Failure{h};}
struct Context {
    Ref<ID3D12CommandAllocator> allocator;Ref<ID3D12GraphicsCommandList> list;
    Ref<ID3D12RootSignature> root;Ref<ID3D12PipelineState> pipeline;
    Ref<ID3D12Resource> upload,result,readback;Ref<ID3D12Fence> fence;
    HANDLE event=nullptr;~Context(){if(event)CloseHandle(event);}
};
const char shader[]=R"(
struct Input {float4 previous_rows[4];float4 previous_scale;float4 previous_offset;float4 previous_bias;
float4 current_rows[4];float4 current_scale;float4 current_offset;float4 current_bias;float4 point;float4 dimensions;};
struct Output {float4 previous_clip;float4 current_clip;float4 delta_valid;};
StructuredBuffer<Input> inputs:register(t0);RWStructuredBuffer<Output> outputs:register(u0);
cbuffer Count:register(b0){uint count;}
bool project(float4 rows[4],float4 scale,float4 offset,float bias,float4 point,out float4 clip){
    float4 q;
    [unroll]for(uint i=0;i<4;i++){
        precise float sum=rows[i].x*point.x+rows[i].y*point.y;
        sum+=rows[i].z*point.z;sum+=rows[i].w*point.w;q[i]=sum;
    }
    clip=0;
    if(!all(isfinite(q)) || !isfinite(bias) || q.w<=1e-8 || q.w+bias<=1e-8)return false;
    precise float factor=bias/q.w;precise float w=q.w+bias;
    q.xy+=q.xy*factor;q.z+=bias;
    clip=float4(q.xyz*scale.xyz+w*offset.xyz,w);
    return all(isfinite(clip));
}
[numthreads(64,1,1)]void main(uint3 tid:SV_DispatchThreadID){
    if(tid.x>=count)return;
    Input i=inputs[tid.x];Output o;o.previous_clip=0;o.current_clip=0;o.delta_valid=0;
    bool before=project(i.previous_rows,i.previous_scale,i.previous_offset,i.previous_bias.z,i.point,o.previous_clip);
    bool now=project(i.current_rows,i.current_scale,i.current_offset,i.current_bias.z,i.point,o.current_clip);
    if(before && now && all(isfinite(i.dimensions.xy)) && all(i.dimensions.xy>0)){
        float2 delta=float2(o.previous_clip.x/o.previous_clip.w-o.current_clip.x/o.current_clip.w,
            o.current_clip.y/o.current_clip.w-o.previous_clip.y/o.previous_clip.w)*.5*i.dimensions.xy;
        if(all(isfinite(delta)))o.delta_valid=float4(delta,1,0);
    }
    outputs[tid.x]=o;
})";
static_assert(sizeof(D2MotionProbeInput)==256 && sizeof(D2MotionProbeOutput)==48,"GPU structured buffer layout");
}
extern "C" int d2_motion_probe_gpu(void* device_ptr,void* queue_ptr,const D2MotionProbeInput* inputs,
    uint32_t count,D2MotionProbeOutput* outputs){
    if(!device_ptr || !queue_ptr || !inputs || !outputs || !count || count>4096)return (int)E_INVALIDARG;
    std::memset(outputs,0,sizeof(*outputs)*count);
    auto device=static_cast<ID3D12Device*>(device_ptr);auto queue=static_cast<ID3D12CommandQueue*>(queue_ptr);
    auto c=std::make_unique<Context>();bool submitted=false,completed=false;
    try {
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)c->allocator.put()));
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,c->allocator.p,nullptr,IID_ID3D12GraphicsCommandList,(void**)c->list.put()));
        D3D12_ROOT_PARAMETER params[3]{};
        params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[2].Constants.Num32BitValues=1;
        D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=3;rd.pParameters=params;
        Ref<ID3DBlob> sig,errors,code;
        check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,sig.put(),errors.put()));
        check(device->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_ID3D12RootSignature,(void**)c->root.put()));
        check(D3DCompile(shader,sizeof(shader)-1,"D2 motion sample diagnostic",nullptr,nullptr,"main","cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS,0,code.put(),errors.put()));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=c->root.p;pd.CS={code->GetBufferPointer(),code->GetBufferSize()};
        check(device->CreateComputePipelineState(&pd,IID_ID3D12PipelineState,(void**)c->pipeline.put()));
        auto buffer=[&](Ref<ID3D12Resource>& out,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES state){
            D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC desc{};
            desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=1;
            desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;
            desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=flags;
            check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_ID3D12Resource,(void**)out.put()));
        };
        const UINT64 bytes=sizeof(*outputs)*count;
        buffer(c->upload,sizeof(*inputs)*count,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
        buffer(c->result,bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        buffer(c->readback,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
        void* mapped=nullptr;D3D12_RANGE none{0,0};check(c->upload->Map(0,&none,&mapped));
        std::memcpy(mapped,inputs,sizeof(*inputs)*count);c->upload->Unmap(0,nullptr);
        c->list->SetPipelineState(c->pipeline.p);c->list->SetComputeRootSignature(c->root.p);
        c->list->SetComputeRootShaderResourceView(0,c->upload->GetGPUVirtualAddress());
        c->list->SetComputeRootUnorderedAccessView(1,c->result->GetGPUVirtualAddress());
        c->list->SetComputeRoot32BitConstant(2,count,0);c->list->Dispatch((count+63)/64,1,1);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={c->result.p,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
        c->list->ResourceBarrier(1,&barrier);c->list->CopyBufferRegion(c->readback.p,0,c->result.p,0,bytes);
        check(c->list->Close());check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)c->fence.put()));
        c->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!c->event)throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        ID3D12CommandList* lists[]={c->list.p};queue->ExecuteCommandLists(1,lists);submitted=true;
        check(queue->Signal(c->fence.p,1));check(c->fence->SetEventOnCompletion(1,c->event));
        if(WaitForSingleObject(c->event,30000)!=WAIT_OBJECT_0)throw Failure{HRESULT_FROM_WIN32(WAIT_TIMEOUT)};
        completed=true;
        check(device->GetDeviceRemovedReason());
        D3D12_RANGE read{0,(SIZE_T)bytes};check(c->readback->Map(0,&read,&mapped));
        std::memcpy(outputs,mapped,(size_t)bytes);c->readback->Unmap(0,&none);return 0;
    }catch(const Failure& e){
        /* A failed signal/timeout may leave commands in flight. Retain ownership
           until process exit rather than freeing memory still accessed by GPU.
           At most eight capture requests per session enter this path. */
        if(submitted && !completed)c.release();
        return (int)e.hr;
    }catch(...){if(submitted && !completed)c.release();return (int)E_FAIL;}
}
