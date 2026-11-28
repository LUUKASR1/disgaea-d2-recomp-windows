#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps3emu/ps3types.h"
#include "rsx_commands.h"
#include "rsx_vertex_fetch.h"

/* Mock guest memory:
 *  - Local VRAM window placed at vm_base + 0x100000 .. 0x300000
 *  - Main pages 0x001..0x004 mapped to scattered 1 MiB pages in vm_base
 *  - Main page 0x005 intentionally unmapped (cellGcmResolveLocated -> 0) */
#define MOCK_VM_SIZE (8u * 1024u * 1024u)
static uint8_t* g_vm = NULL;
u8* vm_base = NULL;

u32 cellGcmResolveLocated(int location, u32 offset)
{
    if (location == 1) {
        return 0x100000u + (offset & 0x1FFFFFu);
    }
    u32 page = offset >> 20;
    u32 page_off = offset & 0xFFFFFu;
    switch (page) {
    case 1u: return 0x300000u + page_off;
    case 2u: return 0x500000u + page_off; /* non-adjacent to page 1! */
    case 3u: return 0x400000u + page_off;
    case 4u: return 0x600000u + page_off;
    case 5u: return 0u; /* unmapped main page */
    default: return 0x200000u + page_off;
    }
}

typedef struct { float v[4]; } VPSlot;
#include "d2_vertex_context.h"

static uint32_t rng_s = 0xCAFEBABEu;
static uint32_t next_u32(void)
{
    uint32_t x = rng_s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_s = x;
    return x;
}

static void ref_read_vertex(const rsx_state* st, u32 vi, VPSlot out[16])
{
    for (int i = 0; i < 16; i++)
        rsx_fetch_attrib(st, i, vi, out[i].v);
}

static u32 ref_read_index(const rsx_state* st, u32 k)
{
    int local = ((st->index_array_dma & 0xFu) == 0u);
    int is_u16 = (((st->index_array_dma >> 4) & 0xFu) == 1u);
    u32 byte_off = st->index_array_offset + k * (is_u16 ? 2u : 4u);
    const u8* p = vm_base + (local ? cellGcmResolveLocated(1, byte_off)
                                   : cellGcmResolveLocated(0, byte_off));
    if (is_u16) return (u32)(((u32)p[0] << 8) | p[1]);
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static void fill_pattern_vram(void)
{
    /* Fill vm_base including page 0 (for unmapped main fallback) with deterministic bytes */
    for (size_t i = 0; i < MOCK_VM_SIZE; i++)
        g_vm[i] = (uint8_t)((i * 73u + (i >> 8) * 19u + 0x3Du) & 0xFFu);

    /* Put well-formed IEEE float32 / half-float values at strides so half-float
     * subnormal/exp=31/normal paths and float NaN/-0.0f paths are all exercised. */
    for (size_t off = 0; off + 16u <= MOCK_VM_SIZE; off += 16u) {
        if ((off & 0xFFu) == 0u) {
            /* Subnormal & negative zero & NaN half-float bit patterns */
            g_vm[off + 0] = 0x80; g_vm[off + 1] = 0x01; /* neg subnormal -> -0.0f */
            g_vm[off + 2] = 0x03; g_vm[off + 3] = 0xFF; /* pos subnormal -> +0.0f */
            g_vm[off + 4] = 0x7E; g_vm[off + 5] = 0x12; /* NaN */
            g_vm[off + 6] = 0xFC; g_vm[off + 7] = 0x00; /* -Inf */
        } else if ((off & 0xFFu) == 16u) {
            /* Negative zero float32 BE & quiet NaN float32 BE */
            g_vm[off + 0] = 0x80; g_vm[off + 1] = 0x00; g_vm[off + 2] = 0x00; g_vm[off + 3] = 0x00;
            g_vm[off + 4] = 0x7F; g_vm[off + 5] = 0xC0; g_vm[off + 6] = 0x00; g_vm[off + 7] = 0x01;
        }
    }

    /* Populate local & main u16 and u32 index buffers across safe ranges */
    u8* loc_u16 = vm_base + cellGcmResolveLocated(1, 0x10000u);
    u8* loc_u32 = vm_base + cellGcmResolveLocated(1, 0x20000u);
    for (u32 i = 0; i < 4096u; i++) {
        u16 v16 = (u16)((i * 7u + 3u) & 0x3Fu);
        loc_u16[i * 2u + 0u] = (u8)(v16 >> 8);
        loc_u16[i * 2u + 1u] = (u8)(v16 & 0xFFu);
        u32 v32 = (i * 11u + 5u) & 0x3Fu;
        loc_u32[i * 4u + 0u] = (u8)(v32 >> 24);
        loc_u32[i * 4u + 1u] = (u8)(v32 >> 16);
        loc_u32[i * 4u + 2u] = (u8)(v32 >> 8);
        loc_u32[i * 4u + 3u] = (u8)(v32 & 0xFFu);
    }

    /* Main single-page u16 index buffer at page 1 (0x00102000) */
    u8* main_u16 = vm_base + cellGcmResolveLocated(0, 0x00102000u);
    for (u32 i = 0; i < 1024u; i++) {
        u16 v16 = (u16)((i * 5u + 1u) & 0x3Fu);
        main_u16[i * 2u + 0u] = (u8)(v16 >> 8);
        main_u16[i * 2u + 1u] = (u8)(v16 & 0xFFu);
    }

    /* Main cross-page u32 index buffer crossing 0x001FFFFC -> 0x00200000 */
    for (u32 i = 0; i < 64u; i++) {
        u32 ea = 0x001FFFE0u + i * 4u;
        u8* p = vm_base + cellGcmResolveLocated(0, ea);
        u32 v32 = (i * 3u + 2u) & 0x3Fu;
        p[0] = (u8)(v32 >> 24);
        p[1] = (u8)(v32 >> 16);
        p[2] = (u8)(v32 >> 8);
        p[3] = (u8)(v32 & 0xFFu);
    }
}

static void assert_verts_equal(const VPSlot* ref, const VPSlot* got, u32 nverts,
                               const float (*ref_shadow)[4], const float (*got_shadow)[4])
{
    assert(memcmp(got, ref, (size_t)nverts * 16u * sizeof(VPSlot)) == 0);
    if (ref_shadow && got_shadow)
        assert(memcmp(got_shadow, ref_shadow, (size_t)nverts * 16u) == 0);
}

static void verify_all_6_uploads(const rsx_state* st, int with_shadow)
{
    static VPSlot ref_out[256 * 16], got_out[256 * 16];
    static float ref_sh[256][4], got_sh[256][4];
    float (*psh_ref)[4] = with_shadow ? ref_sh : NULL;
    float (*psh_got)[4] = with_shadow ? got_sh : NULL;

    d2_vertex_context_prepare(st);
    assert(d2_vertex_context.valid == 1);

    /* 0-count edge cases must succeed cleanly without touching out */
    memset(got_out, 0x5A, sizeof(got_out));
    assert(d2_vertex_context_upload_tris(st, 0u, 0u, got_out, psh_got) == 1);
    assert(d2_vertex_context_upload_quads(st, 0u, 0u, got_out, psh_got) == 1);
    assert(d2_vertex_context_upload_strip(st, 0u, 0u, 0, got_out, psh_got) == 1);
    assert(d2_vertex_context_upload_strip(st, 0u, 0u, 1, got_out, psh_got) == 1);
    assert(d2_vertex_context_upload_tris_indexed(st, 0u, 0u, got_out, psh_got) == 1);
    assert(d2_vertex_context_upload_quads_indexed(st, 0u, 0u, got_out, psh_got) == 1);
    assert(d2_vertex_context_upload_strip_indexed(st, 0u, 0u, 0, got_out, psh_got) == 1);
    assert(d2_vertex_context_upload_strip_indexed(st, 0u, 0u, 1, got_out, psh_got) == 1);

    /* 1. upload_tris */
    {
        const u32 first = 3u, count = 21u;
        memset(ref_out, 0xCC, sizeof(ref_out));
        memset(got_out, 0xAA, sizeof(got_out));
        memset(ref_sh, 0xCC, sizeof(ref_sh));
        memset(got_sh, 0xAA, sizeof(got_sh));
        for (u32 k = 0u; k < count; k++) {
            ref_read_vertex(st, first + k, &ref_out[k * 16u]);
            if (with_shadow) memcpy(ref_sh[k], ref_out[k * 16u].v, 16u);
        }
        assert(d2_vertex_context_upload_tris(st, first, count, got_out, psh_got) == 1);
        assert_verts_equal(ref_out, got_out, count, psh_ref, psh_got);
    }

    /* 2. upload_quads */
    {
        const u32 first = 2u, quads = 9u;
        static const u8 qidx[6] = {0, 1, 2, 0, 2, 3};
        memset(ref_out, 0xCC, sizeof(ref_out));
        memset(got_out, 0xAA, sizeof(got_out));
        u32 o = 0u;
        for (u32 q = 0u; q < quads; q++) {
            VPSlot c[4][16];
            for (u32 k = 0u; k < 4u; k++) ref_read_vertex(st, first + q * 4u + k, c[k]);
            for (int t = 0; t < 6; t++) {
                memcpy(&ref_out[o * 16u], c[qidx[t]], sizeof(c[0]));
                if (with_shadow) memcpy(ref_sh[o], c[qidx[t]][0].v, 16u);
                o++;
            }
        }
        assert(d2_vertex_context_upload_quads(st, first, quads, got_out, psh_got) == 1);
        assert_verts_equal(ref_out, got_out, o, psh_ref, psh_got);
    }

    /* 3. upload_strip (fan=0 and fan=1, tris=1,2,3,17) */
    static const u32 strip_counts[] = {1u, 2u, 3u, 17u};
    for (size_t ci = 0; ci < sizeof(strip_counts) / sizeof(strip_counts[0]); ci++) {
        u32 tris = strip_counts[ci];
        for (int fan = 0; fan <= 1; fan++) {
            const u32 first = 4u;
            memset(ref_out, 0xCC, sizeof(ref_out));
            memset(got_out, 0xAA, sizeof(got_out));
            u32 o = 0u;
            for (u32 t = 0u; t < tris; t++) {
                u32 i0 = fan ? 0u : t;
                u32 i1 = t + 1u, i2 = t + 2u;
                if (!fan && (t & 1u)) { u32 tmp = i1; i1 = i2; i2 = tmp; }
                ref_read_vertex(st, first + i0, &ref_out[o * 16u]);
                if (with_shadow) memcpy(ref_sh[o], ref_out[o * 16u].v, 16u);
                o++;
                ref_read_vertex(st, first + i1, &ref_out[o * 16u]);
                if (with_shadow) memcpy(ref_sh[o], ref_out[o * 16u].v, 16u);
                o++;
                ref_read_vertex(st, first + i2, &ref_out[o * 16u]);
                if (with_shadow) memcpy(ref_sh[o], ref_out[o * 16u].v, 16u);
                o++;
            }
            assert(d2_vertex_context_upload_strip(st, first, tris, fan, got_out, psh_got) == 1);
            assert_verts_equal(ref_out, got_out, o, psh_ref, psh_got);
        }
    }

    /* 4. Indexed uploads (tris, quads, strip fan=0/1) */
    {
        const u32 first = 1u, count = 24u;
        memset(ref_out, 0xCC, sizeof(ref_out));
        memset(got_out, 0xAA, sizeof(got_out));
        for (u32 k = 0u; k < count; k++) {
            u32 vi = ref_read_index(st, first + k);
            ref_read_vertex(st, vi, &ref_out[k * 16u]);
            if (with_shadow) memcpy(ref_sh[k], ref_out[k * 16u].v, 16u);
        }
        assert(d2_vertex_context_upload_tris_indexed(st, first, count, got_out, psh_got) == 1);
        assert_verts_equal(ref_out, got_out, count, psh_ref, psh_got);
    }
    {
        const u32 first = 2u, quads = 8u;
        static const u8 qidx[6] = {0, 1, 2, 0, 2, 3};
        memset(ref_out, 0xCC, sizeof(ref_out));
        memset(got_out, 0xAA, sizeof(got_out));
        u32 o = 0u;
        for (u32 q = 0u; q < quads; q++) {
            VPSlot c[4][16];
            for (u32 k = 0u; k < 4u; k++)
                ref_read_vertex(st, ref_read_index(st, first + q * 4u + k), c[k]);
            for (int t = 0; t < 6; t++) {
                memcpy(&ref_out[o * 16u], c[qidx[t]], sizeof(c[0]));
                if (with_shadow) memcpy(ref_sh[o], c[qidx[t]][0].v, 16u);
                o++;
            }
        }
        assert(d2_vertex_context_upload_quads_indexed(st, first, quads, got_out, psh_got) == 1);
        assert_verts_equal(ref_out, got_out, o, psh_ref, psh_got);
    }
    for (size_t ci = 0; ci < sizeof(strip_counts) / sizeof(strip_counts[0]); ci++) {
        u32 tris = strip_counts[ci];
        for (int fan = 0; fan <= 1; fan++) {
            const u32 first = 1u;
            memset(ref_out, 0xCC, sizeof(ref_out));
            memset(got_out, 0xAA, sizeof(got_out));
            u32 o = 0u;
            for (u32 t = 0u; t < tris; t++) {
                u32 i0 = fan ? 0u : t;
                ref_read_vertex(st, ref_read_index(st, first + i0), &ref_out[o * 16u]);
                if (with_shadow) memcpy(ref_sh[o], ref_out[o * 16u].v, 16u);
                o++;
                ref_read_vertex(st, ref_read_index(st, first + t + 1u), &ref_out[o * 16u]);
                if (with_shadow) memcpy(ref_sh[o], ref_out[o * 16u].v, 16u);
                o++;
                ref_read_vertex(st, ref_read_index(st, first + t + 2u), &ref_out[o * 16u]);
                if (with_shadow) memcpy(ref_sh[o], ref_out[o * 16u].v, 16u);
                o++;
            }
            assert(d2_vertex_context_upload_strip_indexed(st, first, tris, fan, got_out, psh_got) == 1);
            assert_verts_equal(ref_out, got_out, o, psh_ref, psh_got);
        }
    }
}

static void init_base_rsx_state(rsx_state* st)
{
    memset(st, 0, sizeof(*st));
    for (int i = 0; i < 16; i++) {
        st->vertex_data4f[i][0] = (float)(i * 10 + 1) * 0.25f;
        st->vertex_data4f[i][1] = (float)(i * 10 + 2) * 0.5f;
        st->vertex_data4f[i][2] = (float)(i * 10 + 3) * 0.75f;
        st->vertex_data4f[i][3] = 1.0f + (float)i * 0.125f;
    }
}

static void test_u8_norm_bit_exact(void)
{
    printf("[TEST] type=4 UB normalized bit-exact SSE4.1 vs (float)i/255.0f...\n");
    rsx_state st;
    init_base_rsx_state(&st);
    u8* src = vm_base + cellGcmResolveLocated(1, 0x30000u);
    for (int i = 0; i < 256; i++) src[i] = (u8)i;

    st.vertex_attribs[0].enabled = 1;
    st.vertex_attribs[0].offset = 0x30000u;
    st.vertex_attribs[0].stride = 4u;
    st.vertex_attribs[0].size = 4u;
    st.vertex_attribs[0].type = 4u;

    VPSlot got[64 * 16], ref[64 * 16];
    d2_vertex_context_prepare(&st);
    assert(d2_vertex_context_upload_tris(&st, 0u, 64u, got, NULL) == 1);
    for (u32 k = 0u; k < 64u; k++) {
        ref_read_vertex(&st, k, &ref[k * 16u]);
    }
    assert_verts_equal(ref, got, 64u, NULL, NULL);
    printf("  PASS\n");
}

static void test_vertex_modes_and_layouts(void)
{
    printf("[TEST] d2_vertex_context parity across all types, sizes, locations, dividers...\n");
    static const u32 idx_offsets[4] = {0x10000u, 0x20000u, 0x00102000u, 0x001FFFE0u};
    static const u32 idx_dmas[4] = {(1u << 4) | 0u, (0u << 4) | 0u, (1u << 4) | 1u, (0u << 4) | 1u};

    /* Scenario A: Local-only (all_local_nofreq=1), hot type 2 & 4 with sizes 1..4 */
    {
        rsx_state st;
        init_base_rsx_state(&st);
        st.vertex_attribs[0] = (rsx_vertex_attrib){0, 0x00000000u, 16u, 3u, 2u, 0u, 1}; /* pos float3 local */
        st.vertex_attribs[2] = (rsx_vertex_attrib){0, 0x00001000u, 16u, 4u, 2u, 0u, 1}; /* float4 local */
        st.vertex_attribs[3] = (rsx_vertex_attrib){0, 0x00002000u,  8u, 4u, 4u, 0u, 1}; /* u8x4 norm local */
        st.vertex_attribs[5] = (rsx_vertex_attrib){0, 0x00003000u,  8u, 2u, 2u, 0u, 1}; /* float2 local */
        st.vertex_attribs[7] = (rsx_vertex_attrib){0, 0x00004000u,  4u, 1u, 4u, 0u, 1}; /* u8x1 norm local */
        st.vertex_attribs[8] = (rsx_vertex_attrib){0, 0x00005000u,  4u, 3u, 4u, 0u, 1}; /* u8x3 norm local */
        st.vertex_attribs[9] = (rsx_vertex_attrib){0, 0x00006000u,  4u, 1u, 2u, 0u, 1}; /* float1 local */
        st.vertex_attribs[11] = (rsx_vertex_attrib){0, 0x00007000u, 4u, 2u, 4u, 0u, 1}; /* u8x2 norm local */

        for (int icase = 0; icase < 4; icase++) {
            st.index_array_offset = idx_offsets[icase];
            st.index_array_dma = idx_dmas[icase];
            verify_all_6_uploads(&st, icase & 1);
        }
    }

    /* Scenario B: Cold types (1=S1, 3=SF, 5=S32K, 7=UB256, 6=default), sizes 1..4 */
    {
        rsx_state st;
        init_base_rsx_state(&st);
        st.vertex_attribs[0]  = (rsx_vertex_attrib){0, 0x00000000u, 16u, 4u, 3u, 0u, 1}; /* half4 (incl subnorm/NaN) */
        st.vertex_attribs[1]  = (rsx_vertex_attrib){0, 0x00001000u,  8u, 3u, 1u, 0u, 1}; /* s16 norm 3 */
        st.vertex_attribs[4]  = (rsx_vertex_attrib){0, 0x00002000u,  8u, 2u, 5u, 0u, 1}; /* s16 int 2 */
        st.vertex_attribs[6]  = (rsx_vertex_attrib){0, 0x00003000u,  8u, 4u, 7u, 0u, 1}; /* ub256 4 */
        st.vertex_attribs[10] = (rsx_vertex_attrib){0, 0x00004000u,  4u, 3u, 7u, 0u, 1}; /* ub256 3 */
        st.vertex_attribs[12] = (rsx_vertex_attrib){0, 0x00005000u, 16u, 2u, 9u, 0u, 1}; /* default -> rsx_fetch_attrib */
        st.index_array_offset = idx_offsets[0];
        st.index_array_dma = idx_dmas[0];
        verify_all_6_uploads(&st, 1);
    }

    /* Scenario C: Main single-page + Main cross-page + Unmapped main page + freq DIVIDE & MODULO */
    {
        rsx_state st;
        init_base_rsx_state(&st);
        /* Main single-page (0x00100000) */
        st.vertex_attribs[0] = (rsx_vertex_attrib){0, 0x80000000u | 0x00100000u, 16u, 4u, 2u, 0u, 1};
        /* Main crossing page boundary 1 -> 2 at 0x001FFF80 */
        st.vertex_attribs[1] = (rsx_vertex_attrib){0, 0x80000000u | 0x001FFF80u, 32u, 3u, 2u, 0u, 1};
        /* Unmapped main page 5 -> reads vm_base */
        st.vertex_attribs[2] = (rsx_vertex_attrib){0, 0x80000000u | 0x00500000u, 16u, 4u, 4u, 0u, 1};
        /* Frequency dividers: DIVIDE (slot 3) and MODULO (slot 4) */
        st.vertex_attribs[3] = (rsx_vertex_attrib){0, 0x00002000u, 16u, 4u, 2u, 3u, 1};
        st.vertex_attribs[4] = (rsx_vertex_attrib){0, 0x80000000u | 0x00300000u, 8u, 4u, 4u, 4u, 1};
        st.frequency_divider_op = (1u << 4);
        st.index_array_offset = idx_offsets[3];
        st.index_array_dma = idx_dmas[3];
        verify_all_6_uploads(&st, 1);
    }

    /* Scenario D: All-contiguous main single-page fast path (contiguous path with is_main=1) */
    {
        rsx_state st;
        init_base_rsx_state(&st);
        st.vertex_attribs[0] = (rsx_vertex_attrib){0, 0x80000000u | 0x00100100u, 16u, 4u, 2u, 0u, 1};
        st.vertex_attribs[1] = (rsx_vertex_attrib){0, 0x80000000u | 0x00300100u, 16u, 4u, 4u, 0u, 1};
        st.index_array_offset = idx_offsets[2];
        st.index_array_dma = idx_dmas[2];
        verify_all_6_uploads(&st, 1);
    }

    /* Scenario E: Randomized fuzz across 200 attribute combinations */
    for (int iter = 0; iter < 200; iter++) {
        rsx_state st;
        init_base_rsx_state(&st);
        static const u8 types[6] = {1u, 2u, 3u, 4u, 5u, 7u};
        for (int a = 0; a < 16; a++) {
            if ((next_u32() & 3u) == 0u) continue;
            int is_main = (int)(next_u32() & 1u);
            u32 raw = is_main
                ? (0x00100000u + (next_u32() & 0xFFFFu))
                : (next_u32() & 0x7FFFu);
            if (is_main && (next_u32() & 7u) == 0u) raw = 0x001FFFA0u; /* cross page */
            st.vertex_attribs[a].enabled = 1;
            st.vertex_attribs[a].type = types[next_u32() % 6u];
            st.vertex_attribs[a].size = (next_u32() % 5u); /* 0 -> 4 */
            st.vertex_attribs[a].stride = 16u + (next_u32() & 15u);
            st.vertex_attribs[a].offset = raw | (is_main ? 0x80000000u : 0u);
            st.vertex_attribs[a].frequency = ((next_u32() & 7u) == 0u) ? (2u + (next_u32() % 5u)) : 0u;
        }
        st.frequency_divider_op = next_u32() & 0xFFFFu;
        int ic = (int)(next_u32() & 3u);
        st.index_array_offset = idx_offsets[ic];
        st.index_array_dma = idx_dmas[ic];
        verify_all_6_uploads(&st, (int)(next_u32() & 1u));
    }

    printf("  PASS\n");
}

/* Reference + vectorized packet copy parity test (matches adapt_pacing.py) */
#ifdef __SSSE3__
#include <tmmintrin.h>
#endif
static inline void test_d2_bswap32_copy(u8* dst, const u8* src, u32 words)
{
    u32 i = 0u;
#ifdef __SSSE3__
    if (words >= 4u) {
        const __m128i mask128 = _mm_setr_epi8(
            3, 2, 1, 0,  7, 6, 5, 4,  11, 10, 9, 8,  15, 14, 13, 12);
        for (; i + 8u <= words; i += 8u) {
            __m128i v0 = _mm_loadu_si128((const __m128i*)(src + i * 4u));
            __m128i v1 = _mm_loadu_si128((const __m128i*)(src + i * 4u + 16u));
            _mm_storeu_si128((__m128i*)(dst + i * 4u),       _mm_shuffle_epi8(v0, mask128));
            _mm_storeu_si128((__m128i*)(dst + i * 4u + 16u), _mm_shuffle_epi8(v1, mask128));
        }
        if (i + 4u <= words) {
            __m128i v = _mm_loadu_si128((const __m128i*)(src + i * 4u));
            _mm_storeu_si128((__m128i*)(dst + i * 4u), _mm_shuffle_epi8(v, mask128));
            i += 4u;
        }
    }
#endif
    for (; i < words; i++) {
        u32 w;
        memcpy(&w, src + i * 4u, 4u);
        w = __builtin_bswap32(w);
        memcpy(dst + i * 4u, &w, 4u);
    }
}

static int ref_const_pkt(rsx_state* state, u32 method, u32 count, u32 type,
                         const u8* payload, u32 payload_bytes)
{
    const u32 first = NV4097_SET_TRANSFORM_CONSTANT;
    if (!state || !payload || !count || count > 2047 || (type != 0 && type != 2) ||
        method < first || method >= first + 128u || (method & 3u) || count > payload_bytes / 4u)
        return 0;
    if (type == 0 && count > (first + 128u - method) / 4u) return 0;
    const u32 reg0 = (method - first) / 4u;
    for (u32 i = 0; i < count; i++) {
        u32 reg = (type == 0) ? (reg0 + i) : reg0;
        u32 slot = state->transform_constant_load + (reg >> 2);
        if (slot < RSX_MAX_VERTEX_CONSTANTS) {
            const u8* src = payload + i * 4u;
            u32 bits = ((u32)src[0] << 24) | ((u32)src[1] << 16) | ((u32)src[2] << 8) | src[3];
            memcpy(&state->vertex_constants[slot][reg & 3u], &bits, 4u);
            if (!state->vertex_constants_dirty) {
                state->vertex_constants_lo = slot;
                state->vertex_constants_hi = slot;
                state->vertex_constants_dirty = 1;
            } else {
                if (slot < state->vertex_constants_lo) state->vertex_constants_lo = slot;
                if (slot > state->vertex_constants_hi) state->vertex_constants_hi = slot;
            }
        }
    }
    return 1;
}

static int vec_const_pkt(rsx_state* state, u32 method, u32 count, u32 type,
                         const u8* payload, u32 payload_bytes)
{
    const u32 first = NV4097_SET_TRANSFORM_CONSTANT;
    if (!state || !payload || !count || count > 2047 || (type != 0 && type != 2) ||
        method < first || method >= first + 128u || (method & 3u) || count > payload_bytes / 4u)
        return 0;
    if (type == 0 && count > (first + 128u - method) / 4u) return 0;
    const u32 reg0 = (method - first) / 4u;
    if (type == 0u) {
        u64 first_slot = (u64)state->transform_constant_load + (u64)(reg0 >> 2);
        if (first_slot < RSX_MAX_VERTEX_CONSTANTS) {
            u32 lane0 = reg0 & 3u;
            u32 max_words = (u32)(RSX_MAX_VERTEX_CONSTANTS - first_slot) * 4u - lane0;
            u32 writes = count < max_words ? count : max_words;
            if (writes > 0u) {
                test_d2_bswap32_copy((u8*)&state->vertex_constants[first_slot][lane0], payload, writes);
                u32 lo = (u32)first_slot;
                u32 hi = (u32)(first_slot + ((lane0 + writes - 1u) >> 2));
                if (!state->vertex_constants_dirty) {
                    state->vertex_constants_lo = lo;
                    state->vertex_constants_hi = hi;
                    state->vertex_constants_dirty = 1;
                } else {
                    if (lo < state->vertex_constants_lo) state->vertex_constants_lo = lo;
                    if (hi > state->vertex_constants_hi) state->vertex_constants_hi = hi;
                }
            }
        }
    } else {
        u64 slot = (u64)state->transform_constant_load + (u64)(reg0 >> 2);
        if (slot < RSX_MAX_VERTEX_CONSTANTS) {
            u32 bits;
            memcpy(&bits, payload + (size_t)(count - 1u) * 4u, 4u);
            bits = __builtin_bswap32(bits);
            memcpy(&state->vertex_constants[slot][reg0 & 3u], &bits, 4u);
            u32 s32 = (u32)slot;
            if (!state->vertex_constants_dirty) {
                state->vertex_constants_lo = s32;
                state->vertex_constants_hi = s32;
                state->vertex_constants_dirty = 1;
            } else {
                if (s32 < state->vertex_constants_lo) state->vertex_constants_lo = s32;
                if (s32 > state->vertex_constants_hi) state->vertex_constants_hi = s32;
            }
        }
    }
    return 1;
}

static void test_packet_copy_parity(void)
{
    printf("[TEST] d2_bswap32_copy & d2_constant_packet_copy parity...\n");
    u8 src[256 * 4], dst_ref[256 * 4], dst_got[256 * 4];
    for (size_t i = 0; i < sizeof(src); i++) src[i] = (u8)((i * 37u + 11u) & 0xFFu);

    for (u32 words = 0u; words <= 67u; words++) {
        memset(dst_ref, 0xCC, sizeof(dst_ref));
        memset(dst_got, 0xCC, sizeof(dst_got));
        for (u32 i = 0u; i < words; i++) {
            dst_ref[i * 4u + 0u] = src[i * 4u + 3u];
            dst_ref[i * 4u + 1u] = src[i * 4u + 2u];
            dst_ref[i * 4u + 2u] = src[i * 4u + 1u];
            dst_ref[i * 4u + 3u] = src[i * 4u + 0u];
        }
        test_d2_bswap32_copy(dst_got, src, words);
        assert(memcmp(dst_ref, dst_got, sizeof(dst_ref)) == 0);
    }

    rsx_state* st_r = (rsx_state*)calloc(1u, sizeof(rsx_state));
    rsx_state* st_g = (rsx_state*)calloc(1u, sizeof(rsx_state));
    assert(st_r && st_g);

    const u32 loads[] = {0u, 12u, 256u, 504u, 509u, 511u, 512u};
    for (size_t li = 0; li < sizeof(loads) / sizeof(loads[0]); li++) {
        for (u32 reg0 = 0u; reg0 < 32u; reg0++) {
            for (u32 count = 1u; count <= 32u - reg0; count++) {
                memset(st_r, 0xA5, sizeof(*st_r));
                memset(st_g, 0xA5, sizeof(*st_g));
                st_r->transform_constant_load = st_g->transform_constant_load = loads[li];
                st_r->vertex_constants_dirty = st_g->vertex_constants_dirty = 0;
                st_r->vertex_constants_lo = st_g->vertex_constants_lo = 0u;
                st_r->vertex_constants_hi = st_g->vertex_constants_hi = 0u;

                u32 m = NV4097_SET_TRANSFORM_CONSTANT + reg0 * 4u;
                int r0 = ref_const_pkt(st_r, m, count, 0u, src, count * 4u);
                int g0 = vec_const_pkt(st_g, m, count, 0u, src, count * 4u);
                assert(r0 == g0);
                assert(memcmp(st_r, st_g, sizeof(*st_r)) == 0);

                /* Follow up with type=2 when already dirty */
                int r2 = ref_const_pkt(st_r, m, count, 2u, src, count * 4u);
                int g2 = vec_const_pkt(st_g, m, count, 2u, src, count * 4u);
                assert(r2 == g2);
                assert(memcmp(st_r, st_g, sizeof(*st_r)) == 0);
            }
        }
    }

    free(st_g);
    free(st_r);
    printf("  PASS\n");
}

int main(void)
{
    g_vm = (uint8_t*)calloc(1u, MOCK_VM_SIZE);
    assert(g_vm != NULL);
    vm_base = g_vm;
    fill_pattern_vram();

    test_u8_norm_bit_exact();
    test_vertex_modes_and_layouts();
    test_packet_copy_parity();

    free(g_vm);
    printf("ALL VERTEX & PACKET FAST-PATH TESTS PASSED\n");
    return 0;
}
