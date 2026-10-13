#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int d2_graphics_configure(uint32_t guest_width, uint32_t guest_height);
uint32_t d2_graphics_render_width(void);
uint32_t d2_graphics_render_height(void);
uint32_t d2_graphics_output_width(void);
uint32_t d2_graphics_output_height(void);
uint32_t d2_graphics_guest_width(void);
uint32_t d2_graphics_guest_height(void);
int d2_graphics_start(void* device, void* queue, void* swapchain);
int d2_graphics_scene(uint32_t frame, void** resource);
int d2_graphics_present(uint32_t frame, void* scene);
void d2_graphics_shutdown(void);
#ifdef __cplusplus
}
#endif
