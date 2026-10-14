#include "motion_raster.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <stdexcept>
template<class T>struct Ref{T* p=nullptr;~Ref(){if(p)p->Release();}T** put(){return &p;}T* operator->(){return p;}};
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D12 setup failed");}
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    Ref<IDXGIFactory4> factory;Ref<IDXGIAdapter> warp;Ref<ID3D12Device> device;Ref<ID3D12CommandQueue> queue;
    check(CreateDXGIFactory1(IID_IDXGIFactory4,(void**)factory.put()));
    check(factory->EnumWarpAdapter(IID_IDXGIAdapter,(void**)warp.put()));
    check(D3D12CreateDevice(warp.p,D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,(void**)device.put()));
    D3D12_COMMAND_QUEUE_DESC q{};check(device->CreateCommandQueue(&q,IID_ID3D12CommandQueue,(void**)queue.put()));
    D2MotionVertex far_vertices[3]{},near_vertices[3]{};float xy[3][2]={{-.8f,-.8f},{0,.8f},{.8f,-.8f}};
    for(unsigned i=0;i<3;i++){
        float w=1+(float)i;
        near_vertices[i].current_clip[0]=xy[i][0]*w;near_vertices[i].current_clip[1]=xy[i][1]*w;
        near_vertices[i].current_clip[2]=.2f*w;near_vertices[i].current_clip[3]=w;
        std::memcpy(near_vertices[i].previous_clip,near_vertices[i].current_clip,16);
        near_vertices[i].previous_clip[0]-=.25f*w;near_vertices[i].previous_clip[1]+=.5f*w;
        std::memcpy(far_vertices[i].current_clip,near_vertices[i].current_clip,16);far_vertices[i].current_clip[2]=.8f*w;
        std::memcpy(far_vertices[i].previous_clip,far_vertices[i].current_clip,16);far_vertices[i].previous_clip[0]+=.5f*w;
    }
    D2MotionDraw draws[2]={{near_vertices,3,0},{far_vertices,3,0}};std::vector<D2MotionPixel> pixels(64*64);
    auto raster=[&](unsigned count){require(d2_motion_raster_gpu(device.p,queue.p,draws,count,64,64,pixels.data())==0,"Raster GPU succeeds");};
    raster(2);auto center=pixels[32*64+32];
    require(center.coverage==1 && std::abs(center.depth-.2f)<1e-5f,"Near triangle survives far_vertices draw depth occlusion");
    require(std::abs(center.delta_x+8)<1e-4f && std::abs(center.delta_y+16)<1e-4f,"Perspective interpolation preserves pixel motion sign and scale");
    require(pixels[0].coverage==0 && pixels[0].delta_x==0 && pixels[0].delta_y==0,"Outside coverage stays invalid zero");
    for(const auto& p:pixels)if(p.coverage>.5f)require(std::isfinite(p.delta_x)&&std::isfinite(p.delta_y),"Covered pixel finite");
    draws[0]={far_vertices,3,0};draws[1]={near_vertices,3,0};raster(2);
    require(std::abs(pixels[32*64+32].delta_x+8)<1e-4f,"Depth result independent of candidate order");
    for(auto& v:near_vertices)std::memcpy(v.previous_clip,v.current_clip,16);
    draws[0]={near_vertices,3,0};raster(1);
    for(const auto& p:pixels)if(p.coverage>.5f)require(std::abs(p.delta_x)<1e-4f && std::abs(p.delta_y)<1e-4f,"Stationary raster zero");
    draws[0].cull=1;raster(1);unsigned back=0;for(const auto& p:pixels)back+=p.coverage>.5f;
    draws[0].cull=3;raster(1);unsigned front=0;for(const auto& p:pixels)front+=p.coverage>.5f;
    require((back==0)!=(front==0),"Front/back culling keeps opposite orientations");
    draws[0].cull=5;raster(1);unsigned flipped=0;for(const auto& p:pixels)flipped+=p.coverage>.5f;
    require(flipped==front,"Winding convention changes culling");
    draws[0].count=2;require(d2_motion_raster_gpu(device.p,queue.p,draws,1,64,64,pixels.data())<0,"Non-triangle count rejected");
    puts("PASS candidate motion raster: coverage, perspective interpolation, UV sign, stationary, depth order and culling");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
