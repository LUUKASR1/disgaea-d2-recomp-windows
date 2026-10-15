#pragma once
#include "alpha_replay.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int d2_draw_capture_enabled(void);
void d2_draw_capture_close(void);
void d2_draw_vertex_source(const void *bytecode, size_t bytes,
                           const char *hlsl);
void d2_draw_pipeline(void *pso, const void *description,
                      const char *fragment_hlsl);
int d2_draw_pipeline_ready(void *pso);
void d2_draw_srv(uint32_t slot, void *resource, const void *view);
void d2_draw_frame_begin(void);
int d2_draw_export(void *device, void *queue, void *pso, D2AlphaRecord record,
                   void *user, const char *folder, uint64_t frame,
                   uint32_t draw, uint32_t width, uint32_t height,
                   const void *vertices, uint32_t vertex_count, const void *vc,
                   const void *fc, uint32_t fc_bytes, uint32_t first_srv,
                   uint32_t palette_clamp, void *const *borrowed,
                   uint32_t borrowed_count);
#ifdef __cplusplus
}
#endif
