/* Included after the audited backend's types and renderer state. All state is
 * renderer-thread-owned. Existing waits, uploads and transitions stay in place. */
#include "texture_reuse.h"
static unsigned char d2_texture_retained[VP_TEX_SLOTS];

static void d2_texture_reserve_frame(void)
{
    if (!d2_texture_reuse_enabled()) return;
    int heads[256], next[VP_TEX_SLOTS];
    memset(heads, -1, sizeof(heads));
    memset(d2_texture_retained, 0, sizeof(d2_texture_retained));
    for (int i = 0; i < VP_TEX_SLOTS; ++i) {
        const VPTexSlot* c = &s_d3d.vp_tex[i];
        next[i] = -1;
        if (!c->res) continue;
        unsigned bucket = (c->key ^ (c->key >> 8) ^ (c->key >> 16)) & 255u;
        next[i] = heads[bucket]; heads[bucket] = i;
    }
    for (u32 d = 0; d < s_d3d.draw_count && d < MAX_DRAWS; ++d) {
        const D3D12DrawRecord* dr = &s_d3d.draws[d];
        if (!dr->is_vp || dr->is_clear) continue;
        for (unsigned unit = 0; unit < 4; ++unit) {
            u32 key = dr->tex[unit].off;
            if (!dr->tex[unit].set || !key) continue;
            unsigned bucket = (key ^ (key >> 8) ^ (key >> 16)) & 255u;
            for (int i = heads[bucket]; i >= 0; i = next[i])
                if (s_d3d.vp_tex[i].key == key) d2_texture_retained[i] = 1;
        }
    }
}

static int d2_texture_reuse_slot(int fallback, u32 w, u32 h, u32 fmt, int cube)
{
    if (!d2_texture_reuse_enabled()) return fallback;
    D2TextureAllocation slots[VP_TEX_SLOTS];
    for (int i = 0; i < VP_TEX_SLOTS; ++i) {
        const VPTexSlot* c = &s_d3d.vp_tex[i];
        slots[i] = (D2TextureAllocation){c->res, c->up, c->w, c->h, c->fmt,
                                        c->cube, c->used, d2_texture_retained[i]};
    }
    return d2_texture_choose_allocation(slots, VP_TEX_SLOTS, fallback, w, h, fmt, cube);
}
