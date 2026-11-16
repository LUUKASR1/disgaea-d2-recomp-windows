/* Map-cursor discovery (DirectX 12 renderer). The game has no pointer, so to make the map cursor follow the mouse the cursor
 * sprite has to be found on screen first. Every frame the small quads of vertex program 2003CB89 (UI/world sprites) are
 * projected to screen space and kept in a ring of the last 180 frames; F9 writes them, with the direction/Cross/Circle key
 * states of each frame, to %LOCALAPPDATA%\DisgaeaD2Recomp\cursor-diag.txt. Moving the cursor with the keys while recording shows
 * which quad moves with the key presses. */
#define D2_CUR_FRAMES 180
#define D2_CUR_QUADS  96
typedef struct { float cx, cy, hx, hy, z; u32 tex, w, h, fmt, w1, nv; } D2CursorQuad;
typedef struct { u32 keys, count; D2CursorQuad quad[D2_CUR_QUADS]; } D2CursorFrame;
static D2CursorFrame d2_cursor_ring[D2_CUR_FRAMES];
static u32 d2_cursor_head = 0, d2_cursor_filled = 0;

static void d2_cursor_dump(void)
{
    const char* local = getenv("LOCALAPPDATA");
    char path[600];
    snprintf(path, sizeof path, "%s\\DisgaeaD2Recomp\\cursor-diag.txt", local ? local : ".");
    FILE* f = fopen(path, "w");
    if (!f) { fprintf(stderr, "[cursor] cannot write %s\n", path); return; }
    fprintf(f, "# frames=%u backbuffer=%ux%u ; per frame: keys mask (1 up,2 down,4 left,8 right,16 cross,32 circle) then quads\n",
            d2_cursor_filled, s_d3d.width, s_d3d.height);
    fprintf(f, "# quad: cx cy (NDC) hx hy (half size NDC) z tex w x h fmt tex1w verts\n");
    for (u32 i = 0; i < d2_cursor_filled; i++) {
        const D2CursorFrame* fr = &d2_cursor_ring[(d2_cursor_head + D2_CUR_FRAMES - d2_cursor_filled + i) % D2_CUR_FRAMES];
        fprintf(f, "F %u keys=%u n=%u\n", i, fr->keys, fr->count);
        for (u32 q = 0; q < fr->count; q++) {
            const D2CursorQuad* c = &fr->quad[q];
            fprintf(f, "  %.4f %.4f %.4f %.4f %.3f tex=%08X %ux%u f%02X t1w=%u v=%u\n", c->cx, c->cy, c->hx, c->hy, c->z, c->tex, c->w, c->h, c->fmt & 0xFF, c->w1, c->nv);
        }
    }
    fclose(f);
    fprintf(stderr, "[cursor] wrote %u frames to %s\n", d2_cursor_filled, path);
}

static void d2_cursor_scan(void)
{
    D2CursorFrame* fr = &d2_cursor_ring[d2_cursor_head];
    fr->count = 0;
    fr->keys = d2_pad_keys_mask();
    if (s_d3d.vp_vb_mapped && s_d3d.vp_cb_mapped) {
        const u32 parity = (u32)s_d3d.vp_parity;
        for (u32 d = 0; d < s_d3d.draw_count && d < MAX_DRAWS && fr->count < D2_CUR_QUADS; d++) {
            const D3D12DrawRecord* r = &s_d3d.draws[d];
            if (!r->is_vp || r->is_clear || r->rt_off || r->rt_mrt[0] || d2_outline_vs_hash(r) != 0x2003CB89u) continue;
            if (r->vertex_count < 4 || r->vertex_count > 96 || r->cb_slot >= MAX_DRAWS || r->vb_byte_offset % 256u ||
                r->vb_byte_offset / 256u >= MAX_VERTICES || r->vertex_count > MAX_VERTICES - r->vb_byte_offset / 256u) continue;
            const char* vb = (const char*)s_d3d.vp_vb_mapped + (u64)parity * MAX_VERTICES * 256u + r->vb_byte_offset;
            const float* bank = (const float*)((const char*)s_d3d.vp_cb_mapped + ((u64)parity * MAX_DRAWS + r->cb_slot) * VP_CB_STRIDE);
            const float* scale = bank + 512 * 4; const float* offset = bank + 513 * 4;
            float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f, zs = 0; int ok = 1;
            for (u32 v = 0; v < r->vertex_count && ok; v++) {
                const float* p = (const float*)(vb + (size_t)v * 256u);
                if (!(p[3] > 1e-8f)) { ok = 0; break; }
                const float x = (p[0] * scale[0] + p[3] * offset[0]) / p[3], y = (p[1] * scale[1] + p[3] * offset[1]) / p[3];
                if (!(x == x) || !(y == y)) { ok = 0; break; }
                if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y;
                zs += (p[2] * scale[2] + p[3] * offset[2]) / p[3] / r->vertex_count;
            }
            if (!ok) continue;
            D2CursorQuad* q = &fr->quad[fr->count++];
            q->cx = (minx + maxx) * 0.5f; q->cy = (miny + maxy) * 0.5f; q->hx = (maxx - minx) * 0.5f; q->hy = (maxy - miny) * 0.5f; q->z = zs;
            q->tex = r->tex[0].raw; q->w = r->tex[0].w; q->h = r->tex[0].h; q->fmt = r->tex[0].fmt; q->w1 = r->tex[1].w; q->nv = r->vertex_count;
        }
    }
    {   /* the map cursor: the only 256x320 sprite with 6 vertices whose on-screen shape is a 2:1 diamond */
        int found = 0; float cx = 0, cy = 0, hx = 0, hy = 0;
        for (u32 q = 0; q < fr->count; q++) {
            const D2CursorQuad* c = &fr->quad[q];
            if (c->w != 256 || c->h != 320 || (c->fmt & 0xFF) != 0xA5 || c->nv != 6 || c->hx <= 0.0f || c->hy <= 0.0f) continue;
            const float aspect = (c->hx * (float)s_d3d.width) / (c->hy * (float)s_d3d.height);
            if (aspect < 1.6f || aspect > 2.4f) continue;
            if (found) { found = 2; break; }
            found = 1; cx = c->cx; cy = c->cy; hx = c->hx; hy = c->hy;
        }
        d2_mouse_follow_frame(found == 1, cx, cy, hx, hy);
    }
    d2_cursor_head = (d2_cursor_head + 1) % D2_CUR_FRAMES;
    if (d2_cursor_filled < D2_CUR_FRAMES) d2_cursor_filled++;
    if (d2_graphics_cursor_dump_requested()) d2_cursor_dump();
}

