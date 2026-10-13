#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Diagnostic-only GPU replay. No runtime draw is suppressed or moved. */
int d2_hud_vertices(const void* vertices, uint32_t count, uint32_t stride);
int d2_hud_prepare(void* device, void* queue, void* color, void* depth);
int d2_hud_snapshot(void* command_list, void* color, void* depth);
void* d2_hud_begin(uint32_t pass);
int d2_hud_end(void);
void* d2_hud_resource(uint32_t kind);
int d2_hud_compare(void* reference, uint64_t* different_rgb, uint64_t* different_alpha, uint32_t* maximum_error);
void d2_hud_shutdown(void);
#ifdef __cplusplus
}
#endif
