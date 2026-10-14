#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {float current_clip[4],previous_clip[4];} D2MotionVertex;
typedef struct {const D2MotionVertex* vertices;uint32_t count,cull;} D2MotionDraw;
/* RGBA32_FLOAT: xy previous-minus-current pixels, z owning draw index + 1 (zero uncovered), w depth.
   The owner tag is scoped to this call, never persistent object identity.
   Diagnostic visibility is ONLY between supplied candidates, never full scene. */
typedef struct {float delta_x,delta_y,coverage,depth;} D2MotionPixel;
int d2_motion_raster_gpu(void* device,void* queue,const D2MotionDraw* draws,uint32_t count,
    uint32_t width,uint32_t height,D2MotionPixel* pixels);
#ifdef __cplusplus
}
#endif
