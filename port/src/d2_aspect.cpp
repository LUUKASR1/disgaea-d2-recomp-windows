/* BLUS31313 1.40 Vert+ aspect scaling for 3D perspective projection (func_002D9F84).
 * Keeps horizontal scale (m00) unchanged and multiplies vertical scale (m11 at +0x14)
 * by d2_graphics_vertplus_y_scale() (e.g. 720/800 = 0.9 for 1280x800), revealing
 * extra 3D field vertically with 1:1 square pixels and aligned 3D billboard sprites. */
#include "ppu_recomp.h"
#include <cstring>

#ifdef _WIN32
extern "C" float d2_graphics_vertplus_y_scale(void);
#else
static float d2_graphics_vertplus_y_scale(void) { return 1.0f; }
#endif

#if D2_GAME_VERSION == 140
void d2_original_002D9F84(ppu_context*);

void func_002D9F84(ppu_context* ctx)
{
    const uint32_t out_ea = (uint32_t)ctx->gpr[3];
    d2_original_002D9F84(ctx);
    const float sy = d2_graphics_vertplus_y_scale();
    if (sy != 1.0f && out_ea) {
        uint32_t raw = vm_read32(out_ea + 0x14u);
        float m11 = 0.0f;
        std::memcpy(&m11, &raw, sizeof m11);
        m11 *= sy;
        std::memcpy(&raw, &m11, sizeof raw);
        vm_write32(out_ea + 0x14u, raw);
    }
}
#endif
