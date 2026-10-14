#include "motion_probe.h"
#include "projection.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
template<class T>struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("GPU setup failed");}
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try{
    Ref<IDXGIFactory4> f;Ref<IDXGIAdapter> a;Ref<ID3D12Device> d;Ref<ID3D12CommandQueue> q;
    check(CreateDXGIFactory1(IID_IDXGIFactory4,(void**)f.put()));
    check(f->EnumWarpAdapter(IID_IDXGIAdapter,(void**)a.put()));
    check(D3D12CreateDevice(a.p,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)d.put()));
    D3D12_COMMAND_QUEUE_DESC desc{};check(d->CreateCommandQueue(&desc,IID_ID3D12CommandQueue,(void**)q.put()));
    std::array<D2MotionProbeInput,65> input{};
    std::array<D2MotionProbeOutput,65> output{};
    for(unsigned i=0;i<input.size();i++){
        auto& v=input[i];
        for(unsigned j=0;j<4;j++){v.previous_rows[j*4+j]=1;v.current_rows[j*4+j]=1;v.previous_scale[j]=v.current_scale[j]=1;}
        v.previous_scale[2]=v.current_scale[2]=.5f;v.previous_offset[2]=v.current_offset[2]=.5f;
        v.point[0]=.3f;v.point[1]=-.4f;v.point[2]=.5f;v.point[3]=1;
        v.dimensions[0]=1280;v.dimensions[1]=720;
        if(i%4==1){v.current_rows[3]=.2f;v.current_rows[7]=-.4f;}
        if(i%4==2){v.current_rows[0]=0;v.current_rows[2]=1;v.current_rows[8]=-1;v.current_rows[10]=0;v.current_rows[15]=2;}
        if(i%4==3){v.previous_bias[2]=.25f;v.current_bias[2]=.5f;}
    }
    input[60].current_rows[15]=0;input[61].current_rows[15]=-1;
    input[62].previous_rows[0]=std::numeric_limits<float>::quiet_NaN();
    input[63].dimensions[0]=0;
    require(d2_motion_probe_gpu(d.p,q.p,input.data(),(uint32_t)input.size(),output.data())==0,"Compute batch succeeds");
    for(unsigned i=0;i<input.size();i++){
        const auto& v=input[i];const auto& o=output[i];float previous[4],current[4],delta[2];
        bool valid=d2_project_world(v.previous_rows,v.previous_scale,v.previous_offset,v.previous_bias[2],v.point,previous)&&
            d2_project_world(v.current_rows,v.current_scale,v.current_offset,v.current_bias[2],v.point,current)&&
            d2_same_point_delta(previous,current,(unsigned)v.dimensions[0],(unsigned)v.dimensions[1],delta);
        require((o.delta_valid[2]==1)==valid,"GPU validity equals CPU");
        if(valid){for(unsigned j=0;j<4;j++){
            require(std::abs(previous[j]-o.previous_clip[j])<1e-5f,"Previous clip CPU/GPU");
            require(std::abs(current[j]-o.current_clip[j])<1e-5f,"Current clip CPU/GPU");
        }
        require(std::abs(delta[0]-o.delta_valid[0])<.01f && std::abs(delta[1]-o.delta_valid[1])<.01f,"Pixel delta CPU/GPU");
        }else require(o.delta_valid[0]==0 && o.delta_valid[1]==0,"Rejected vectors are marked invalid");
    }
    require(std::abs(output[1].delta_valid[0]+128)<.01f && std::abs(output[1].delta_valid[1]+144)<.01f,"Previous-minus-current sign");
    require(output[0].delta_valid[0]==0 && output[0].delta_valid[1]==0,"Stationary zero");
    require(d2_motion_probe_gpu(d.p,q.p,input.data(),0,output.data())<0,"Empty request rejected");
    puts("PASS GPU motion samples: batch tail, stationary, translation, rotation, bias, validity, UV direction");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
