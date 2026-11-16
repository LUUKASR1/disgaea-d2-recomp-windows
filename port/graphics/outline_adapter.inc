/* Character outline for the DirectX 12 renderer (included into the adapted rsx_d3d12_backend.c, after hud_adapter.inc).
 *
 * Characters, props and effects of the world are small quads drawn with vertex program 2003CB89 before the HUD block
 * (see docs/TEMPORAL_GRAPHICS.md). For those draws the frame loop first draws the same quad eight times, shifted by
 * 1..3 pixels in every direction, with a blend state that paints the sprite's own alpha silhouette black, and then
 * draws the sprite normally on top. The result is a dark outline that follows the (alpha tested) sprite shape and is
 * hidden where the world is in front. D2_OUTLINE_DEBUG=1 logs how many draws were taken for outlining. */
#define D2_OUTLINE_VS 0x2003CB89u
#define D2_OUTLINE_BLEND (1u | ((u32)D3D12_BLEND_ZERO << 1) | ((u32)D3D12_BLEND_INV_SRC_ALPHA << 6) | \
                          ((u32)D3D12_BLEND_ZERO << 11) | ((u32)D3D12_BLEND_ONE << 16) | \
                          ((u32)D3D12_BLEND_OP_ADD << 21) | ((u32)D3D12_BLEND_OP_ADD << 24))

static u32 d2_outline_hud_start(void)
{
    static u64 frame = ~0ull; static u32 start = 0, count = ~0u;
    if (frame != (u64)s_d3d.frame_count || count != s_d3d.draw_count) {
        frame = (u64)s_d3d.frame_count; count = s_d3d.draw_count; start = d2_hud_find_boundary(s_d3d.draw_count);
    }
    return start;
}

static u32 d2_outline_vs_hash(const D3D12DrawRecord* dr)
{
    return (dr->vs_idx >= 0 && dr->vs_idx < s_d3d.vp_vs_n) ? s_d3d.vp_vs[dr->vs_idx].hash : 0u;
}

static int d2_outline_is_character(const D3D12DrawRecord* dr, u32 d)
{
    static int dbg = -1; static u32 seen = 0, picked = 0, frames = 0;
    if (dbg < 0) { const char* e = getenv("D2_OUTLINE_DEBUG"); dbg = e && *e && *e != '0'; }
    static u32 evs = 0, etex = 0, einit = 0;
    if (!einit) { einit = 1; const char* a = getenv("D2_OUTLINE_VS"); const char* b = getenv("D2_OUTLINE_TEX0");
                  if (a && *a) evs = (u32)strtoul(a, NULL, 16); if (b && *b) etex = (u32)strtoul(b, NULL, 16); }
    const u32 hud = d2_outline_hud_start();
    int ok = dr->is_vp && !dr->is_clear && !dr->rt_off && !dr->rt_mrt[0] && dr->fp_addr &&
             (evs ? (d2_outline_vs_hash(dr) == evs && (!etex || dr->tex[0].raw == etex)) : d2_outline_vs_hash(dr) == D2_OUTLINE_VS) && dr->vertex_count >= 3 && dr->vertex_count <= 96 &&
             hud < s_d3d.draw_count && d < hud;
    if (dbg) {
        /* Per-texture table: which atlas/texture each draw samples, its format and size, and the draw sizes. */
        static struct { u32 vs, raw, w, h, fmt, w1, h1, fmt1, n, vmin, vmax; } sig[16]; static int nsig = 0;
        if (dr->is_vp && !dr->is_clear) {
            int k = 0;
            for (; k < nsig; k++) if (sig[k].vs == d2_outline_vs_hash(dr) && sig[k].raw == dr->tex[0].raw && sig[k].fmt1 == dr->tex[1].fmt && sig[k].w1 == dr->tex[1].w) break;
            if (k == nsig && nsig < 16) {
                sig[nsig].vs = d2_outline_vs_hash(dr); sig[nsig].raw = dr->tex[0].raw; sig[nsig].w = dr->tex[0].w; sig[nsig].h = dr->tex[0].h; sig[nsig].fmt = dr->tex[0].fmt;
                sig[nsig].w1 = dr->tex[1].w; sig[nsig].h1 = dr->tex[1].h; sig[nsig].fmt1 = dr->tex[1].fmt; sig[nsig].n = 0; sig[nsig].vmin = ~0u; sig[nsig].vmax = 0; nsig++;
            }
            if (k < nsig) { sig[k].n++; if (dr->vertex_count < sig[k].vmin) sig[k].vmin = dr->vertex_count; if (dr->vertex_count > sig[k].vmax) sig[k].vmax = dr->vertex_count; }
        }
        if (d == 0 && frames % 300 == 299) {
            for (int k = 0; k < nsig; k++)
                fprintf(stderr, "[outline] tex vs=%08X t0=0x%08X %ux%u fmt=0x%X | t1 %ux%u fmt=0x%X | n=%u verts=%u..%u\n", sig[k].vs, sig[k].raw, sig[k].w, sig[k].h, sig[k].fmt,
                        sig[k].w1, sig[k].h1, sig[k].fmt1, sig[k].n, sig[k].vmin, sig[k].vmax);
            nsig = 0;
        }
        seen++; if (ok) picked++;
        if (d == 0 && ++frames % 300 == 0) {
            fprintf(stderr, "[outline] last 300 frames: draws=%u outlined=%u hud-start=%u of %u (%s)\n", seen, picked, hud,
                    s_d3d.draw_count, d2_hud_boundary_reason);
            seen = picked = 0;
        }
    }
    return ok;
}

static ID3D12PipelineState* d2_outline_pso(const D3D12DrawRecord* dr)
{
    D2DepthStencilKey zs = dr->d2_zs;
    zs.depth &= ~2u; /* depth test as the sprite has it, but never write: the sprite itself must still pass */
    return vp_get_fp_pso(dr->vs_idx, dr->fp_addr, D2_OUTLINE_BLEND, dr_num_rts(dr),
                         dr->rt_off ? rsx_surface_dxgi(dr->rt_fmt) : DXGI_FORMAT_R8G8B8A8_UNORM,
                         dr->fp_exp32, dr->cmask, dr->cull, dr_cube_mask(dr), zs);
}

