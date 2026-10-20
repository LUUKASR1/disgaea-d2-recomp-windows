#pragma once
#include <stddef.h>
#include <stdint.h>
#include "scene_depth.h"
/* Masks indexed by the raster call's draw index, NOT persistent identity.
   The caller validates frozen frame/draw/size metadata and pre-HUD depth first. */
typedef struct {const unsigned char* coverage;const float* depth;size_t pixel_count;} D2MotionMask;
enum {D2_MASK_KEEP=0,D2_MASK_INVALID_PIXEL=1,D2_MASK_INVALID_OWNER=2,
    D2_MASK_UNAVAILABLE=3,D2_MASK_SHADER_REJECTED=4,D2_MASK_DEPTH_MISMATCH=5,D2_MASK_RESULT_COUNT=6};
static inline int d2_motion_mask_filter_pixel(const D2MotionPixel* source,const D2MotionMask* masks,
    uint32_t count,size_t pixel,float epsilon,D2MotionPixel* output){
    if(!output)return D2_MASK_INVALID_PIXEL;
    D2MotionPixel value={0,0,0,0};if(source)value=*source;
    output->delta_x=output->delta_y=output->coverage=output->depth=0;
    if(!source || !isfinite(value.delta_x)||!isfinite(value.delta_y)||!isfinite(value.depth)||
       value.depth<0||value.depth>1)return D2_MASK_INVALID_PIXEL;
    if(!count || count>64 || !isfinite(value.coverage)||value.coverage<1||value.coverage>(float)count ||
       floorf(value.coverage)!=value.coverage)return D2_MASK_INVALID_OWNER;
    uint32_t owner=(uint32_t)value.coverage-1;
    if(!masks || !masks[owner].coverage || !masks[owner].depth || pixel>=masks[owner].pixel_count)return D2_MASK_UNAVAILABLE;
    if(!masks[owner].coverage[pixel])return D2_MASK_SHADER_REJECTED;
    if(d2_depth_relation(value.depth,masks[owner].depth[pixel],epsilon)!=1)return D2_MASK_DEPTH_MISMATCH;
    *output=value;output->coverage=1;return D2_MASK_KEEP;
}
