#include "motion_mask.h"
#include <cstdio>
#include <cmath>
#include <limits>
#include <stdexcept>
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    const unsigned char near_mask[3]={255,0,0},far_mask[3]={255,255,0};
    const float near_depth[3]={.2f,1,1},far_depth[3]={.8f,.8f,1};
    D2MotionMask masks[2]={{near_mask,near_depth,3},{far_mask,far_depth,3}};
    D2MotionPixel pixel{12,-7,1,.2f},out{99,99,99,99};const float epsilon=8.f/16777215.f;
    require(d2_motion_mask_filter_pixel(&pixel,masks,2,0,epsilon,&out)==D2_MASK_KEEP &&
        out.delta_x==12 && out.delta_y==-7 && out.coverage==1,"Owning mask and depth retain original vector");
    require(d2_motion_mask_filter_pixel(&pixel,masks,2,1,epsilon,&out)==D2_MASK_SHADER_REJECTED && out.coverage==0 &&
        out.delta_x==0 && out.delta_y==0,"Discarded near owner cannot borrow overlapping far draw's surviving mask");
    pixel.coverage=2;pixel.depth=.8f;
    require(d2_motion_mask_filter_pixel(&pixel,masks,2,1,epsilon,&out)==D2_MASK_KEEP,"Second owner selects its own mask");
    pixel.depth=.2f;require(d2_motion_mask_filter_pixel(&pixel,masks,2,1,epsilon,&out)==D2_MASK_DEPTH_MISMATCH && out.coverage==0,"Different original depth cannot validate another geometry sample");
    pixel.depth=.8f;require(d2_motion_mask_filter_pixel(&pixel,masks,2,2,epsilon,&out)==D2_MASK_SHADER_REJECTED,"Both discarded remains uncovered");
    masks[1].coverage=nullptr;require(d2_motion_mask_filter_pixel(&pixel,masks,2,0,epsilon,&out)==D2_MASK_UNAVAILABLE,"Missing original shader mask fails closed");
    masks[1].coverage=far_mask;require(d2_motion_mask_filter_pixel(&pixel,masks,2,3,epsilon,&out)==D2_MASK_UNAVAILABLE,"Pixel bounds checked before mask access");
    for(float owner:{0.f,-1.f,1.5f,3.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        pixel.coverage=owner;require(d2_motion_mask_filter_pixel(&pixel,masks,2,0,epsilon,&out)==D2_MASK_INVALID_OWNER,"Invalid owner tag rejected");
    }
    pixel={0,0,2,.8f};require(d2_motion_mask_filter_pixel(&pixel,masks,2,0,epsilon,&out)==D2_MASK_KEEP && out.coverage==1,"Stationary zero vector is valid coverage");
    pixel.delta_x=std::numeric_limits<float>::quiet_NaN();require(d2_motion_mask_filter_pixel(&pixel,masks,2,0,epsilon,&out)==D2_MASK_INVALID_PIXEL && out.coverage==0,"Nonfinite vector cannot survive mask");
    puts("PASS owning draw alpha/depth filter: overlap isolation, original depth, missing masks, bounds, owner validity and stationary motion");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
