#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int d2_posthud_enabled(void);
/* The SDK must drain the previous GPU work before frame() / allocator reuse. */
void d2_posthud_frame(uint32_t frame);
int d2_posthud_prepare(void* device,void* queue,void* color,void* depth,void* output);
int d2_posthud_snapshot(uint32_t frame,void* list,void* color,void* depth);
void* d2_posthud_scene(uint32_t frame);
void d2_posthud_keep(uint32_t frame,uint32_t index,void* pipeline);
void* d2_posthud_pipeline(uint32_t frame,uint32_t index);
void* d2_posthud_begin(uint32_t frame,void* output);
int d2_posthud_end(uint32_t frame);
void d2_posthud_shutdown(void);
#ifdef __cplusplus
}
#endif
