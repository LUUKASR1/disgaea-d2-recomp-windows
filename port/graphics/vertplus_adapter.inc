/* Vert+ 16:10 (e.g. 1280x800) viewport classification for the DirectX 12 renderer.
 * Included in the generated rsx_d3d12_backend.c before hud_adapter.inc.
 *
 * Uses CPU-cacheable shadows (d2_vp_pos_shadow, d2_vp_cb_xform) so HUD/fade
 * classification never reads Write-Combined D3D12_HEAP_TYPE_UPLOAD memory. */
static float d2_vp_pos_shadow[2u * MAX_VERTICES][4];
static int d2_vp_frame_has_3d = 0;
static u32 d2_vp_hud_start = 0;
static unsigned char d2_vp_draw_kind[MAX_DRAWS]; /* 0=16x9, 1=full, 2=mixed-split */
static u32 d2_vp_quad_mask[MAX_DRAWS];           /* bit q=1 -> quad q uses full 16:10 */

static u32 d2_vp_vs_hash(const D3D12DrawRecord* r)
{
    if (r->vs_idx >= 0 && r->vs_idx < s_d3d.vp_vs_n) return s_d3d.vp_vs[r->vs_idx].hash;
    return r->d2_capture_vs_hash;
}

static inline int d2_vp_is_hud_draw(const D3D12DrawRecord* r, u32 parity)
{
    if (!r->is_vp || r->is_clear || r->rt_off || r->rt_mrt[0] || r->rt_mrt[1] || r->rt_mrt[2] ||
        d2_vp_vs_hash(r) != 0x2003CB89u || !r->vertex_count ||
        (r->vb_byte_offset % 256u) || r->vb_byte_offset / 256u > MAX_VERTICES ||
        r->vertex_count > MAX_VERTICES - r->vb_byte_offset / 256u) return 0;
    for (unsigned u = 0; u < 4; u++) if (r->tex_rt[u] >= 0) return 0;
    const u32 vbase = parity * MAX_VERTICES + (r->vb_byte_offset >> 8);
    return d2_hud_vertices(d2_vp_pos_shadow[vbase], r->vertex_count, 16u);
}

static inline int d2_vp_quad_needs_full(const D3D12DrawRecord* r, u32 parity, u32 v_abs)
{
    const float* xf = d2_vp_cb_xform[parity & 1u][r->cb_slot];
    const float sx = xf[0], sy = xf[1], ox = xf[2], oy = xf[3];
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (u32 v = v_abs; v < v_abs + 6u; v++) {
        const float* pos = d2_vp_pos_shadow[v];
        if (!isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[3]) || fabsf(pos[3]) < 1e-5f) return 0;
        const float nx = (pos[0] * sx + pos[3] * ox) / pos[3];
        const float ny = (pos[1] * sy + pos[3] * oy) / pos[3];
        if (!isfinite(nx) || !isfinite(ny)) return 0;
        if (nx < minx) minx = nx; if (nx > maxx) maxx = nx;
        if (ny < miny) miny = ny; if (ny > maxy) maxy = ny;
    }
    if (miny <= -0.98f && maxy >= 0.98f) return 1;
    if (r->blend && (!r->tex[0].set || (r->tex[0].w <= 16u && r->tex[0].h <= 16u)) &&
        minx <= -0.98f && maxx >= 0.98f && (maxy >= 0.98f || miny <= -0.98f) && (maxy - miny) >= 0.65f) return 1;
    return 0;
}

static int d2_vp_classify_quads(const D3D12DrawRecord* r, u32 parity, u32* out_mask)
{
    *out_mask = 0;
    if (!r->is_vp || r->is_clear || r->rt_off || r->rt_mrt[0] || r->rt_mrt[1] || r->rt_mrt[2] ||
        d2_vp_vs_hash(r) != 0x2003CB89u) return 0;
    if (r->vertex_count < 6u || (r->vertex_count % 6u) != 0u || r->vertex_count > 192u ||
        r->cb_slot >= MAX_DRAWS || (r->vb_byte_offset % 256u) ||
        r->vb_byte_offset / 256u > MAX_VERTICES || r->vertex_count > MAX_VERTICES - r->vb_byte_offset / 256u) return 0;
    const u32 vbase = parity * MAX_VERTICES + (r->vb_byte_offset >> 8);
    const u32 nq = r->vertex_count / 6u;
    u32 nfull = 0, mask = 0;
    for (u32 q = 0; q < nq; q++) {
        if (d2_vp_quad_needs_full(r, parity, vbase + q * 6u)) {
            mask |= (1u << q);
            nfull++;
        }
    }
    *out_mask = mask;
    if (nfull == nq) return 1;
    if (nfull > 0u) return 2;
    return 0;
}

static int d2_vp_is_fullscreen_fade(const D3D12DrawRecord* r, u32 parity)
{
    u32 mask = 0;
    return d2_vp_classify_quads(r, parity, &mask) == 1;
}

static void d2_vertplus_prepare(u32 count)
{
    if (!d2_graphics_vertplus_enabled()) {
        d2_vp_frame_has_3d = 0;
        d2_vp_hud_start = count;
        return;
    }
    const u32 limit = count < MAX_DRAWS ? count : MAX_DRAWS;
    const u32 parity = (u32)s_d3d.vp_parity;
    int last_3d = -1;
    for (u32 d = 0; d < limit; d++) {
        const D3D12DrawRecord* r = &s_d3d.draws[d];
        if (!r->is_vp || r->is_clear || r->rt_off || r->rt_mrt[0] || r->rt_mrt[1] || r->rt_mrt[2]) continue;
        if (d2_vp_vs_hash(r) == 0x99362655u) last_3d = (int)d;
    }
    const int has_3d = (last_3d >= 0);
    int last_world = last_3d;
    if (has_3d) {
        for (u32 d = limit; d > (u32)(last_3d + 1); ) {
            d--;
            const D3D12DrawRecord* r = &s_d3d.draws[d];
            if (!r->is_vp || r->is_clear || r->rt_off || r->rt_mrt[0] || r->rt_mrt[1] || r->rt_mrt[2]) continue;
            if (d2_vp_vs_hash(r) == 0x2003CB89u && !d2_vp_is_hud_draw(r, parity)) {
                last_world = (int)d;
                break;
            }
        }
    }
    d2_vp_frame_has_3d = has_3d;
    d2_vp_hud_start = (has_3d && last_world >= 0) ? (u32)(last_world + 1) : 0u;
    if (has_3d) {
        for (u32 d = d2_vp_hud_start; d < limit; d++) {
            d2_vp_draw_kind[d] = (unsigned char)d2_vp_classify_quads(&s_d3d.draws[d], parity, &d2_vp_quad_mask[d]);
        }
    }

    static int last_mode = -1;
    if (last_mode != has_3d) {
        last_mode = has_3d;
        fprintf(stderr, "[D2VERTPLUS] mode=%s draws=%u hud_start=%u y_scale=%.4f\n",
                has_3d ? "3D-Vert+" : "2D-16x9", limit, d2_vp_hud_start, d2_graphics_vertplus_y_scale());
    }
    static int vlog = -1;
    if (vlog < 0) { const char* e = getenv("D2_VERTPLUS_LOG"); vlog = (e && !strcmp(e, "1")) ? 1 : 0; }
    if (__builtin_expect(vlog && has_3d, 0)) {
        static u32 prev_draws = 0, prev_hud = 0, snaps = 0;
        static u64 prev_frame = 0;
        const int draw_jump = (limit > prev_draws + 3u) || (prev_draws > limit + 3u);
        if (snaps < 80 && (draw_jump || d2_vp_hud_start != prev_hud || s_d3d.frame_count >= prev_frame + 90u)) {
            prev_draws = limit;
            prev_hud = d2_vp_hud_start;
            prev_frame = s_d3d.frame_count;
            snaps++;
            fprintf(stderr, "[D2VERTPLUS-3D] frame=%llu draws=%u hud_start=%u\n",
                    (unsigned long long)s_d3d.frame_count, limit, d2_vp_hud_start);
            u32 start_d = (d2_vp_hud_start > 2u) ? (d2_vp_hud_start - 2u) : 0u;
            for (u32 d = start_d; d < limit && d < d2_vp_hud_start + 96u; d++) {
                const D3D12DrawRecord* r = &s_d3d.draws[d];
                if (!r->is_vp || r->is_clear || r->rt_off) continue;
                float minx = 0.0f, maxx = 0.0f, miny = 0.0f, maxy = 0.0f, z0 = 0.0f, w0 = 0.0f;
                if (r->vertex_count > 0u && r->cb_slot < MAX_DRAWS && (r->vb_byte_offset % 256u) == 0u &&
                    r->vb_byte_offset / 256u <= MAX_VERTICES &&
                    r->vertex_count <= MAX_VERTICES - r->vb_byte_offset / 256u) {
                    const u32 vbase = parity * MAX_VERTICES + (r->vb_byte_offset >> 8);
                    const float* xf = d2_vp_cb_xform[parity & 1u][r->cb_slot];
                    minx = 1e9f; maxx = -1e9f; miny = 1e9f; maxy = -1e9f;
                    for (u32 v = 0; v < r->vertex_count; v++) {
                        const float* pos = d2_vp_pos_shadow[vbase + v];
                        if (v == 0u) { z0 = pos[2]; w0 = pos[3]; }
                        if (!isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[3]) || fabsf(pos[3]) < 1e-5f) continue;
                        const float nx = (pos[0] * xf[0] + pos[3] * xf[2]) / pos[3];
                        const float ny = (pos[1] * xf[1] + pos[3] * xf[3]) / pos[3];
                        if (!isfinite(nx) || !isfinite(ny)) continue;
                        if (nx < minx) minx = nx; if (nx > maxx) maxx = nx;
                        if (ny < miny) miny = ny; if (ny > maxy) maxy = ny;
                    }
                }
                fprintf(stderr, "  d=%u %s vs=%08X fp=%08X v=%u blend=%d bk=0x%X t0=%d(%ux%u,rt=%d) box=[%.3f..%.3f,%.3f..%.3f] z=%.4f w=%.3f fade=%d\n",
                        d, (d >= d2_vp_hud_start) ? "HUD" : "WLD", d2_vp_vs_hash(r), r->fp_addr, r->vertex_count,
                        r->blend, r->blend_key, r->tex[0].set, r->tex[0].w, r->tex[0].h, r->tex_rt[0],
                        minx, maxx, miny, maxy, z0, w0, d2_vp_is_fullscreen_fade(r, parity));
            }
            fflush(stderr);
        }
    }
}

static inline int d2_vertplus_use_16x9(u32 d, const D3D12DrawRecord* r, u32 parity)
{
    (void)r; (void)parity;
    if (!d2_graphics_vertplus_enabled()) return 0;
    if (!d2_vp_frame_has_3d) return 1;
    if (d >= d2_vp_hud_start && d < MAX_DRAWS) return d2_vp_draw_kind[d] != 1;
    return 0;
}

static void d2_vertplus_16x9_box(float tw, float th, float gw, float gh, float* ox, float* oy, float* s)
{
    const float sx = tw / gw, sy = th / gh;
    const float sc = sx < sy ? sx : sy;
    *s = sc;
    *ox = (tw - gw * sc) * 0.5f;
    *oy = (th - gh * sc) * 0.5f;
}

static void d2_vertplus_aspect_rect(float tw, float th, float gw, float gh, D3D12_RECT* out)
{
    float ox = 0.0f, oy = 0.0f, s = 1.0f;
    d2_vertplus_16x9_box(tw, th, gw, gh, &ox, &oy, &s);
    out->left = (LONG)ox;
    out->top = (LONG)oy;
    out->right = (LONG)(ox + gw * s);
    out->bottom = (LONG)(oy + gh * s);
}

static void d2_vertplus_set_vp(ID3D12GraphicsCommandList* cmd, const D3D12DrawRecord* dr,
                               float tw, float th, float gw, float gh, int use_16x9)
{
    D3D12_VIEWPORT dvp = {0.0f, 0.0f, tw, th, 0.0f, 1.0f};
    if (use_16x9) {
        float ox = 0.0f, oy = 0.0f, sc = 1.0f;
        d2_vertplus_16x9_box(tw, th, gw, gh, &ox, &oy, &sc);
        if (dr->vp_w >= 2 && dr->vp_h >= 2 &&
            (float)(dr->vp_x + dr->vp_w) <= gw + 0.5f &&
            (float)(dr->vp_y + dr->vp_h) <= gh + 0.5f) {
            dvp.TopLeftX = ox + (float)dr->vp_x * sc;
            dvp.TopLeftY = oy + (float)dr->vp_y * sc;
            dvp.Width    = (float)dr->vp_w * sc;
            dvp.Height   = (float)dr->vp_h * sc;
        } else {
            dvp.TopLeftX = ox;
            dvp.TopLeftY = oy;
            dvp.Width    = gw * sc;
            dvp.Height   = gh * sc;
        }
    } else if (dr->vp_w >= 2 && dr->vp_h >= 2 &&
               (float)(dr->vp_x + dr->vp_w) <= gw + 0.5f &&
               (float)(dr->vp_y + dr->vp_h) <= gh + 0.5f) {
        dvp.TopLeftX = (float)dr->vp_x * tw / gw;
        dvp.TopLeftY = (float)dr->vp_y * th / gh;
        dvp.Width    = (float)dr->vp_w * tw / gw;
        dvp.Height   = (float)dr->vp_h * th / gh;
    }
    D3D12_RECT dsc = {(LONG)dvp.TopLeftX, (LONG)dvp.TopLeftY,
                      (LONG)(dvp.TopLeftX + dvp.Width),
                      (LONG)(dvp.TopLeftY + dvp.Height)};
    cmd->lpVtbl->RSSetViewports(cmd, 1, &dvp);
    cmd->lpVtbl->RSSetScissorRects(cmd, 1, &dsc);
}

static inline int d2_vertplus_draw_hud_split(ID3D12GraphicsCommandList* cmd, u32 d, const D3D12DrawRecord* r, u32 parity,
                                              float tw, float th, float gw, float gh)
{
    (void)parity;
    if (!d2_graphics_vertplus_enabled() || !d2_vp_frame_has_3d || d < d2_vp_hud_start || d >= MAX_DRAWS) return 0;
    if (d2_vp_draw_kind[d] != 2u) return 0;
    const u32 nq = r->vertex_count / 6u;
    const u32 mask = d2_vp_quad_mask[d];
    for (u32 q = 0; q < nq; ) {
        const u32 is_f = (mask >> q) & 1u;
        u32 end = q + 1u;
        while (end < nq && (((mask >> end) & 1u) == is_f)) end++;
        d2_vertplus_set_vp(cmd, r, tw, th, gw, gh, !is_f);
        cmd->lpVtbl->DrawInstanced(cmd, (end - q) * 6u, 1, r->vb_byte_offset / 256u + q * 6u, 0);
        q = end;
    }
    return 1;
}
