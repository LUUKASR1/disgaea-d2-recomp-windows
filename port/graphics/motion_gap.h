#pragma once
#include "motion_mask.h"
/* Reason for the conservative selected-candidate field, not scene semantics.
   In particular, no candidate does not identify background or object type. */
enum {D2_GAP_RETAINED=0,D2_GAP_NO_CANDIDATE,D2_GAP_DEPTH_UNAVAILABLE,D2_GAP_DEPTH_REJECTED,
    D2_GAP_MASK_UNAVAILABLE,D2_GAP_SHADER_REJECTED,D2_GAP_DEPTH_MISMATCH,D2_GAP_INVALID,D2_GAP_COUNT};
static inline int d2_motion_gap_class(const D2MotionPixel* source,const D2MotionPixel* filtered,
    const D2MotionMask* masks,uint32_t count,size_t pixel,float epsilon){
    if(!source)return D2_GAP_NO_CANDIDATE;
    if(!isfinite(source->coverage)||source->coverage<0)return D2_GAP_INVALID;
    if(source->coverage==0)return D2_GAP_NO_CANDIDATE;
    if(!count || count>64 || source->coverage<1 || source->coverage>count || floorf(source->coverage)!=source->coverage ||
       !isfinite(source->delta_x)||!isfinite(source->delta_y)||!isfinite(source->depth)||source->depth<0||source->depth>1)return D2_GAP_INVALID;
    if(!filtered)return D2_GAP_DEPTH_UNAVAILABLE;
    if(!isfinite(filtered->coverage)||filtered->coverage<0)return D2_GAP_INVALID;
    if(filtered->coverage<=.5f)return D2_GAP_DEPTH_REJECTED;
    D2MotionPixel result={0,0,0,0};
    int code=d2_motion_mask_filter_pixel(source,masks,count,pixel,epsilon,&result);
    switch(code){
    case D2_MASK_KEEP:return D2_GAP_RETAINED;
    case D2_MASK_UNAVAILABLE:return D2_GAP_MASK_UNAVAILABLE;
    case D2_MASK_SHADER_REJECTED:return D2_GAP_SHADER_REJECTED;
    case D2_MASK_DEPTH_MISMATCH:return D2_GAP_DEPTH_MISMATCH;
    default:return D2_GAP_INVALID;
    }
}
