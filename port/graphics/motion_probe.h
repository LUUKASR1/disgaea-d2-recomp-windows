#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Capture-only compute validation. This is NOT a raster motion-vector texture. */
typedef struct {
    float previous_rows[16],previous_scale[4],previous_offset[4],previous_bias[4];
    float current_rows[16],current_scale[4],current_offset[4],current_bias[4];
    float point[4],dimensions[4];
} D2MotionProbeInput;
typedef struct {float previous_clip[4],current_clip[4],delta_valid[4];} D2MotionProbeOutput;
/* Borrow device/queue. No state changes to the game's command list/resources.
   Returns zero on success, negative HRESULT on failure; outputs cleared on failure. */
int d2_motion_probe_gpu(void* device,void* queue,const D2MotionProbeInput* input,
    uint32_t count,D2MotionProbeOutput* output);
#ifdef __cplusplus
}
#endif
