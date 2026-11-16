#pragma once
#include "motion_mask.h"
/* Selected static candidates only. Agreement is not persistent object identity. */
typedef struct {D2MotionPixel value;uint32_t draw,contributors,ambiguous;} D2MotionLayer;
static inline int d2_motion_layer_add(D2MotionLayer* layer,const D2MotionPixel* isolated,const D2MotionMask* mask,
    size_t pixel,float scene_depth,float epsilon,uint32_t draw){
    if(!layer)return 0;
    D2MotionPixel validated={0,0,0,0};
    if(d2_motion_mask_filter_pixel(isolated,mask,1,pixel,epsilon,&validated)!=D2_MASK_KEEP ||
       d2_depth_relation(validated.depth,scene_depth,epsilon)!=1)return 0;
    if(!layer->contributors){layer->value=validated;layer->draw=draw;}
    else if(fabsf(layer->value.delta_x-validated.delta_x)>.25f || fabsf(layer->value.delta_y-validated.delta_y)>.25f)
        layer->ambiguous=1;
    /* Retain first vector as a fixed consensus anchor; changing anchors could
       accept a nontransitive chain of slightly different motion vectors. */
    layer->contributors++;return 1;
}
static inline int d2_motion_layer_eligible(const D2MotionPixel* source,const D2MotionMask* masks,
    uint32_t count,size_t pixel){
    if(!source || !count || count>64 || !isfinite(source->coverage)||source->coverage<1 ||
       source->coverage>count || floorf(source->coverage)!=source->coverage ||
       !isfinite(source->delta_x)||!isfinite(source->delta_y)||!isfinite(source->depth)||source->depth<0||source->depth>1)return 0;
    uint32_t owner=(uint32_t)source->coverage-1;
    return masks && masks[owner].coverage && masks[owner].depth && pixel<masks[owner].pixel_count && !masks[owner].coverage[pixel];
}
static inline int d2_motion_layer_recover(const D2MotionLayer* layer,const D2MotionPixel* source,
    const D2MotionPixel* baseline,const D2MotionMask* masks,uint32_t count,size_t pixel,D2MotionPixel* out){
    if(!out)return 0;
    out->delta_x=out->delta_y=out->coverage=out->depth=0;
    if(baseline && baseline->coverage>.5f){*out=*baseline;return 0;}
    if(!layer || !layer->contributors || layer->ambiguous || !d2_motion_layer_eligible(source,masks,count,pixel))return 0;
    *out=layer->value;out->coverage=1;return 1;
}
