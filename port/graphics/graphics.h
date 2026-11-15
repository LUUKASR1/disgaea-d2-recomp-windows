#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void d2_display_start(void* hwnd);
uint32_t d2_display_swap_flags(void* factory);
int d2_display_present(void* swapchain,uint32_t interval);
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
void* d2_graphics_output(uint32_t frame);
void d2_graphics_shutdown(void);
void d2_graphics_set_post(const float* values,int enabled);
/* Texture pack: dump (PNG per distinct texture) and toggle. kind = RSX_TEXFMT_* of the decoded image. */
void d2_texture_dump(const uint8_t* source,uint32_t source_bytes,uint32_t w,uint32_t h,uint32_t format,int kind,const void* decoded,uint32_t pitch);
void d2_texture_set_dump(int on);
int d2_texture_dump_enabled(void);
#ifdef __cplusplus
}
#endif
