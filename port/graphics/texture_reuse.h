#pragma once
#include <stddef.h>

typedef struct D2TextureAllocation {
    const void* resource;
    const void* upload;
    unsigned width, height, format;
    int cube, used, retained;
} D2TextureAllocation;

/* Scratch texture SRVs must not overwrite per-draw tables already populated
 * earlier in the preparation loop. Keep the original address only for A/B off. */
static size_t d2_texture_descriptor_index(size_t draw_base, size_t max_draws,
    size_t slot, int enabled)
{
    return (enabled ? draw_base + max_draws * 4 : 1) + slot;
}

/* Call only after the original exact-key lookup has missed. No new resource
 * pool: choose an idle allocation in the existing bounded texture cache.
 * Retained entries may be requested later in this command list. */
static int d2_texture_choose_allocation(const D2TextureAllocation* slots,
    size_t count, int fallback, unsigned width, unsigned height,
    unsigned format, int cube)
{
    if (!slots || fallback < 0 || (size_t)fallback >= count) return fallback;
    for (size_t i = 0; i < count; ++i) {
        const D2TextureAllocation* s = slots + i;
        if (!s->used && !s->retained && s->resource && s->upload &&
            s->width == width && s->height == height &&
            s->format == format && s->cube == cube) return (int)i;
    }
    return fallback;
}
