#pragma once
#include <stdint.h>
#include <math.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Copies plane 0 of borrowed D24/R24G8 depth, restoring DEPTH_WRITE.
   Full source subresource is copied; output is the requested top-left crop. */
int d2_scene_depth_read(void* device,void* queue,void* depth,uint32_t width,uint32_t height,float* output);
/* Diagnostic categories, NOT proof of visibility: 0 invalid, 1 equal,
   2 candidate deeper than final depth, 3 candidate nearer. */
static inline int d2_depth_relation(float candidate,float scene,float epsilon){
    if(!isfinite(candidate)||!isfinite(scene)||!isfinite(epsilon)||epsilon<0 ||
       candidate<0||candidate>1||scene<0||scene>1)return 0;
    float difference=candidate-scene;
    if(fabsf(difference)<=epsilon)return 1;
    return difference>0?2:3;
}
#ifdef __cplusplus
}
#endif
