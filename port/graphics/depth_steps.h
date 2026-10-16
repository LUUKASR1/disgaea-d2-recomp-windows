#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Three GPU-local snapshots of plane 0 of a borrowed D24/R24G8 depth buffer.
   record() runs on the caller's render command list and restores source state.
   reset/destroy require the caller's GPU idle fence; read() follows submission
   on the same queue and waits through the existing readback fence. */
void* d2_depth_steps_create(void* device,void* source);
void d2_depth_steps_reset_after_gpu_idle(void* steps);
void d2_depth_steps_destroy_after_gpu_idle(void* steps);
int d2_depth_steps_record(void* steps,void* command_list,uint32_t slot);
int d2_depth_steps_read(void* steps,void* queue,uint32_t slot,uint32_t width,uint32_t height,float* output);
#ifdef __cplusplus
}
#endif
