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
/* Screenshot of the final image (output resolution, after upscaler and filters), lossless PNG. */
int d2_graphics_hdr_enabled(void); /* swap chain is scRGB FP16 (D2_HDR=1) */
void d2_graphics_set_hdr(int level); /* 0..3: how far highlights are expanded on an HDR display */
int d2_graphics_hdr_display(void);   /* the display is in HDR mode */
void d2_graphics_set_outline(int level); /* 0 off, 1..3: black outline around the world sprites (characters), DirectX 12 */
int d2_graphics_outline_level(void);
void d2_screenshot_request(void);
void d2_screenshot_save(const void* pixels,uint32_t w,uint32_t h,int bgra); /* tight RGBA/BGRA rows; written to a PNG in the background */
unsigned d2_screenshot_last(char* path,unsigned size); /* returns a counter that grows with every saved shot */
int d2_texture_pack_enabled(void);
/* RGBA8 replacement for the texture (malloc'd, free with d2_texture_free) or NULL. */
uint8_t* d2_texture_replacement(const uint8_t* source,uint32_t source_bytes,uint32_t w,uint32_t h,uint32_t format,uint32_t* out_w,uint32_t* out_h);
void d2_texture_free(void* p);
#ifdef __cplusplus
}
#endif
