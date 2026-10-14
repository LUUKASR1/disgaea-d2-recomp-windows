#include "projection.h"
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <array>
template<class T> struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("Projection GPU HRESULT="+std::to_string((unsigned long)h));}
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    float rows[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    float scale[4]={1,1,.5f,1},offset[4]={0,0,.5f,0},point[4]={.3f,-.4f,.5f,1},base[4],biased[4];
    require(d2_project_world(rows,scale,offset,0,point,base),"Reference position valid");
    require(d2_project_world(rows,scale,offset,.25f,point,biased),"Bias position valid");
    require(std::abs(base[0]/base[3]-biased[0]/biased[3])<1e-6f &&
        std::abs(base[1]/base[3]-biased[1]/biased[3])<1e-6f,"Shader bias preserves normalized xy");
    float shifted[16];std::memcpy(shifted,rows,sizeof(rows));shifted[3]=.2f;shifted[7]=-.4f;
    float current[4],delta[2];require(d2_project_world(shifted,scale,offset,0,point,current),"Translation valid");
    require(d2_same_point_delta(base,current,1280,720,delta) && std::abs(delta[0]+128)<.01f &&
        std::abs(delta[1]+144)<.01f,"Pixel direction/scale must match previous-minus-current UV");
    require(d2_same_point_delta(base,base,1280,720,delta) && delta[0]==0 && delta[1]==0,"Stationary point is zero");
    float invalid[16];std::memcpy(invalid,rows,sizeof(rows));invalid[15]=0;
    require(!d2_project_world(invalid,scale,offset,0,point,current),"Singular w is rejected");
    invalid[15]=-1;require(!d2_project_world(invalid,scale,offset,0,point,current),"Behind-camera sample rejected");

    Ref<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_IDXGIFactory4,(void**)factory.put()));
    Ref<IDXGIAdapter> warp;check(factory->EnumWarpAdapter(IID_IDXGIAdapter,(void**)warp.put()));
    Ref<ID3D12Device> device;check(D3D12CreateDevice(warp.p,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)device.put()));
    Ref<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};check(device->CreateCommandQueue(&q,IID_ID3D12CommandQueue,(void**)queue.put()));
    Ref<ID3D12CommandAllocator> allocator;check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,(void**)allocator.put()));
    Ref<ID3D12GraphicsCommandList> list;check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.p,nullptr,IID_ID3D12GraphicsCommandList,(void**)list.put()));
    check(list->Close());
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;
    Ref<ID3DBlob> signature,error;check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,signature.put(),error.put()));
    Ref<ID3D12RootSignature> root;check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_ID3D12RootSignature,(void**)root.put()));
    const char* shader=R"(
StructuredBuffer<float4> input:register(t0);RWStructuredBuffer<float4> output:register(u0);
[numthreads(1,1,1)]void main(){
    float4 p=input[0];
    float4 q=float4(dot(p,input[1]),dot(p,input[2]),dot(p,input[3]),dot(p,input[4]));
    float bias=input[7].x;
    float factor=bias==0?0:bias/q.w;
    float4 adjusted=float4(q.xy+q.xy*factor,q.z+bias,q.w+bias);
    output[0]=float4(adjusted.xyz*input[5].xyz+adjusted.w*input[6].xyz,adjusted.w);
})";
    Ref<ID3DBlob> code;check(D3DCompile(shader,std::strlen(shader),"World position reference",nullptr,nullptr,"main","cs_5_0",0,0,code.put(),error.put()));
    D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root.p;ps.CS={code->GetBufferPointer(),code->GetBufferSize()};
    Ref<ID3D12PipelineState> pipeline;check(device->CreateComputePipelineState(&ps,IID_ID3D12PipelineState,(void**)pipeline.put()));
    auto buffer=[&](Ref<ID3D12Resource>& target,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES state){
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC desc{};
        desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=1;desc.DepthOrArraySize=1;
        desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=flags;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_ID3D12Resource,(void**)target.put()));
    };
    Ref<ID3D12Resource> upload,output,readback;
    buffer(upload,128,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
    buffer(output,16,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    buffer(readback,16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    Ref<ID3D12Fence> fence;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,(void**)fence.put()));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"Projection fence event");
    for(unsigned scenario=0;scenario<4;scenario++){
        float bank[8][4]{};std::memcpy(bank[0],point,16);std::memcpy(bank[1],rows,64);
        std::memcpy(bank[5],scale,16);std::memcpy(bank[6],offset,16);bank[7][0]=scenario==0?0:.25f;
        if(scenario==2){bank[1][3]=2;bank[2][3]=-1;bank[4][3]=3;}
        if(scenario==3){bank[1][0]=0;bank[1][2]=1;bank[3][0]=-1;bank[3][2]=0;bank[4][3]=2;}
        float expected[4];require(d2_project_world(bank[1],bank[5],bank[6],bank[7][0],bank[0],expected),"Synthetic projected position");
        void* mapped=nullptr;D3D12_RANGE none{0,0};check(upload->Map(0,&none,&mapped));std::memcpy(mapped,bank,128);upload->Unmap(0,nullptr);
        check(allocator->Reset());check(list->Reset(allocator.p,pipeline.p));
        if(scenario){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={output.p,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};list->ResourceBarrier(1,&b);}
        list->SetComputeRootSignature(root.p);list->SetComputeRootShaderResourceView(0,upload->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress());list->Dispatch(1,1,1);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={output.p,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
        list->CopyBufferRegion(readback.p,0,output.p,0,16);check(list->Close());
        ID3D12CommandList* commands[]={list.p};queue->ExecuteCommandLists(1,commands);
        check(queue->Signal(fence.p,scenario+1));check(fence->SetEventOnCompletion(scenario+1,event));
        require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"Projection GPU timeout");
        D3D12_RANGE range{0,16};check(readback->Map(0,&range,&mapped));auto actual=static_cast<const float*>(mapped);
        for(unsigned axis=0;axis<4;axis++)require(std::abs(actual[axis]-expected[axis])<1e-5f,"CPU projection matches GPU reference");
        readback->Unmap(0,&none);
    }
    CloseHandle(event);puts("PASS world projection: GPU reference, bias, translation, rotation, UV sign and singular rejection");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
