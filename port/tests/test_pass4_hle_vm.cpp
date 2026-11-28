/*
 * Pass 4 verification test:
 *   1. ps3_hle_register / ps3_hle_register_ctx O(1) hash lookup (collisions,
 *      first-registered-wins, ctx priority over table, ps3_hle_has, ps3_hle_call
 *      TOC guard + [sp+0x28] writeback + inflight + unresolved fallback).
 *   2. milestone.c PS3_MS_ACTIVE() fast gate and g_ps3_ms_enabled behavior.
 *   3. sys_ppu_thread.c lazy host-RA profiler breadcrumb resolution via
 *      ppu_prof_set_resolver / ppu_prof_stamp_ra / ppu_thread_prof_pc /
 *      ppu_prof_snapshot.
 *   4. ppu_loader.cpp vm_read32 / vm_write32 / vm_write64 fast-path vs slow-path
 *      endian-swap roundtrips and D2VmSlowInit resolver wiring.
 *   5. rsx_commands.c zero-XMM float-bit preservation (including subnormals, -0.0,
 *      and NaN payloads) + draw/clear cold-split backend dispatch.
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "ps3emu/milestone.h"
#include "ps3emu/nid.h"
#include "sys_ppu_thread.h"
#include "rsx_commands.h"

typedef struct { uint64_t addr; void (*func)(ppu_context*); const char* name; } func_entry;

/* Declarations for runtime functions under test */
extern "C" {
void ps3_hle_register(uint32_t nid, const char* name, void* handler);
void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));
int ps3_hle_has(uint32_t nid);
void ps3_hle_call(uint32_t nid, ppu_context* ctx);

uint32_t vm_read32(uint64_t addr);
void vm_write32(uint64_t addr, uint32_t val);
uint64_t vm_read64(uint64_t addr);
void vm_write64(uint64_t addr, uint64_t val);
}

/* --------------------------------------------------------------------------
 * Minimal mock symbols required to link real ppu_hle.o + ppu_loader.o +
 * sys_ppu_thread.o + rsx_commands.o + milestone.o
 * -------------------------------------------------------------------------- */
extern "C" {
uint8_t* vm_base = nullptr;
uint32_t ppu_hle_inject_base = 0x20000000u;

uint64_t g_spu_coh_range = 0;
uint64_t g_spu_coh_line_bloom = 0;
uint64_t g_spu_coh_bitmap[64] = {0};

int ps3_poll_enabled = 0;
unsigned ps3_poll_waiters = 0;
unsigned ps3_poll_lines[4096] = {0};
void ps3_poll_backoff(uint32_t, uint32_t, const void*, uint32_t) {}
void ps3_poll_notify_slow(uint32_t, uint32_t) {}
void ps3_poll_thread_start(const char*, int) {}

void ps3_guest_worker_register(int) {}
void ps3_guest_worker_detached(void) {}
void ps3_guest_worker_join(void) {}
int ps3_intr_is_handler(void) { return 0; }
void ps3_intr_wait(void) {}
uint32_t ps3_mmapper_block_size(uint32_t) { return 0; }

uint32_t g_ydkj_real_spurs_ea = 0;
uint32_t g_ydkj_real_taskset_ea = 0;

void cellGcm_notify_guest_write(uint32_t, uint32_t) {}
void cellGcm_ref_on_poll(void) {}

uint32_t prx_export_registry_count(void) { return 0; }
uint32_t prx_resolve_export(uint32_t) { return 0; }

void ppu_gcm_pump(void) {}
void ps3_guest_frame_hook(ppu_context*) {}

int spu_coh_is_reserved(uint32_t) { return 0; }
void spu_coh_notify_write(uint32_t, uint32_t) {}
void spu_lockline_lock(uint32_t) {}
void spu_lockline_unlock(uint32_t) {}
int spu_raw_reg_load(uint32_t, uint32_t*) { return 0; }
int spu_raw_reg_store(uint32_t, uint32_t) { return 0; }

int lv2_try_syscall(uint32_t, ppu_context*) { return 0; }

extern const func_entry function_table[1] = {{0, nullptr, nullptr}};
extern const uint64_t function_table_count = 0;

void* vm_alloc_stack(uint32_t) { return nullptr; }
void* vm_translate(uint64_t a) { return vm_base ? (void*)(vm_base + (uint32_t)a) : nullptr; }
int ppu_vm_access_ok(uint64_t) { return 1; }
} /* extern "C" */

/* --------------------------------------------------------------------------
 * HLE test callbacks
 * -------------------------------------------------------------------------- */
static uint64_t hle_tbl_add(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                            uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + 100u;
}

static uint64_t hle_tbl_duplicate(uint64_t, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t, uint64_t)
{
    return 0xBADBAD01u;
}

static uint64_t hle_tbl_collide(uint64_t a0, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t, uint64_t, uint64_t)
{
    return a0 ^ 0x55AA55AAu;
}

static int g_ctx_calls = 0;
static void hle_ctx_primary(ppu_context* ctx)
{
    g_ctx_calls++;
    ctx->gpr[2] = 0xDEAD0000u; /* Clobber r2 intentionally: _TocGuard must restore caller r2 */
    ctx->gpr[3] = ctx->gpr[4] + 0x1111u;
}

static void hle_ctx_duplicate(ppu_context* ctx)
{
    ctx->gpr[3] = 0xBADBAD02u;
}

static void hle_ctx_collide(ppu_context* ctx)
{
    ctx->gpr[3] = 0xC0111DE0u;
}

/* Custom resolver for lazy host-RA breadcrumb tests */
static int g_resolver_calls = 0;
static uint32_t test_ra_resolver(void* ra)
{
    g_resolver_calls++;
    uintptr_t v = (uintptr_t)ra;
    return (uint32_t)((v & 0x000FFFFFu) | 0x00100000u);
}

/* Backend spy for rsx_commands cold-split draw/clear tests */
static int g_be_draw_arrays = 0;
static u32 g_be_prim = 0, g_be_first = 0, g_be_count = 0;
static int g_be_draw_indexed = 0;
static int g_be_clear = 0;
static u32 g_be_clear_flags = 0, g_be_clear_color = 0;
static float g_be_clear_depth = 0.0f;
static u8 g_be_clear_stencil = 0;

static void spy_draw_arrays(void*, u32 prim, u32 first, u32 count)
{
    g_be_draw_arrays++;
    g_be_prim = prim;
    g_be_first = first;
    g_be_count = count;
}

static void spy_draw_indexed(void*, u32 prim, u32 idx_off, u32 count)
{
    g_be_draw_indexed++;
    g_be_prim = prim;
    g_be_first = idx_off;
    g_be_count = count;
}

static void spy_clear(void*, u32 flags, u32 color, float depth, u8 stencil)
{
    g_be_clear++;
    g_be_clear_flags = flags;
    g_be_clear_color = color;
    g_be_clear_depth = depth;
    g_be_clear_stencil = stencil;
}

static void test_milestone_gate(void)
{
    printf("[TEST] milestone PS3_MS_ACTIVE() gate...\n");
    ps3_ms("boot:test"); /* initializes g_ps3_ms_enabled from env (unset -> 0) */
    assert(g_ps3_ms_enabled == 0);
    assert(!PS3_MS_ACTIVE());
    printf("  PASS\n");
}

static void test_hle_hash_and_dispatch(void)
{
    printf("[TEST] ppu_hle O(1) lookup, collisions, duplicates, TOC guard...\n");

    const uint32_t NID_PRIMARY = 0x12345678u;
    const uint32_t NID_COLLIDE = NID_PRIMARY ^ 0x00002000u; /* stress linear probing */
    const uint32_t NID_BOTH    = 0xAABBCCDDu;
    const uint32_t NID_CTX_A   = 0x24681357u;
    const uint32_t NID_CTX_COL = NID_CTX_A ^ 0x00000200u;

    ps3_hle_register(NID_PRIMARY, "test_tbl_add", (void*)hle_tbl_add);
    ps3_hle_register(NID_PRIMARY, "test_tbl_dup", (void*)hle_tbl_duplicate); /* must NOT overwrite */
    ps3_hle_register(NID_COLLIDE, "test_tbl_col", (void*)hle_tbl_collide);
    ps3_hle_register(NID_BOTH,    "test_tbl_low", (void*)hle_tbl_duplicate);
    ps3_hle_register(0x99990001u, "test_tbl_null", nullptr);

    /* Bulk register 2200 NIDs to exercise load factor and open addressing */
    for (uint32_t i = 0; i < 2200u; i++) {
        uint32_t nid = 0x50000000u + (i * 0x9E37u);
        ps3_hle_register(nid, "bulk_nid", (void*)hle_tbl_add);
    }

    ps3_hle_register_ctx(NID_CTX_A,   "test_ctx_a",   hle_ctx_primary);
    ps3_hle_register_ctx(NID_CTX_A,   "test_ctx_dup", hle_ctx_duplicate); /* must NOT overwrite */
    ps3_hle_register_ctx(NID_CTX_COL, "test_ctx_col", hle_ctx_collide);
    ps3_hle_register_ctx(NID_BOTH,    "test_ctx_win", hle_ctx_primary);

    for (uint32_t i = 0; i < 180u; i++) {
        ps3_hle_register_ctx(0x60000000u + i * 512u, "bulk_ctx", hle_ctx_primary);
    }

    assert(ps3_hle_has(NID_PRIMARY) == 1);
    assert(ps3_hle_has(NID_COLLIDE) == 1);
    assert(ps3_hle_has(NID_CTX_A) == 1);
    assert(ps3_hle_has(NID_CTX_COL) == 1);
    assert(ps3_hle_has(0x99990001u) == 0);
    assert(ps3_hle_has(0xDEADC0DEu) == 0);

    ppu_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.thread_id = 1;
    ctx.gpr[1] = 0x01001000u;
    ctx.gpr[2] = 0x0080123456789ABCull;
    for (int i = 0; i < 8; i++) ctx.gpr[3 + i] = (uint64_t)(i + 1);

    /* 1) First-registered-wins on table lookup + arg marshaling */
    ps3_hle_call(NID_PRIMARY, &ctx);
    assert(ctx.gpr[3] == (1u + 2u + 3u + 4u + 5u + 6u + 7u + 8u + 100u));
    assert(ctx.gpr[2] == 0x0080123456789ABCull);
    assert(vm_read64(ctx.gpr[1] + 0x28u) == 0x0080123456789ABCull);

    /* 2) Table collision probe */
    ctx.gpr[3] = 0x0F0F0F0Fu;
    ps3_hle_call(NID_COLLIDE, &ctx);
    assert(ctx.gpr[3] == (0x0F0F0F0Fu ^ 0x55AA55AAu));

    /* 3) Context handler priority over table + duplicate suppression + TOC restore */
    g_ctx_calls = 0;
    ctx.gpr[2] = 0xCAFEBABE12345678ull;
    ctx.gpr[4] = 0x2222u;
    ps3_hle_call(NID_BOTH, &ctx);
    assert(g_ctx_calls == 1);
    assert(ctx.gpr[3] == 0x3333u);
    assert(ctx.gpr[2] == 0xCAFEBABE12345678ull);
    assert(vm_read64(ctx.gpr[1] + 0x28u) == 0xCAFEBABE12345678ull);

    ctx.gpr[4] = 0x4444u;
    ps3_hle_call(NID_CTX_A, &ctx);
    assert(ctx.gpr[3] == 0x5555u);

    ps3_hle_call(NID_CTX_COL, &ctx);
    assert(ctx.gpr[3] == 0xC0111DE0u);

    /* 4) Unresolved NID sets r3 = 0 safely */
    ctx.gpr[3] = 0x9999u;
    ps3_hle_call(0x7FFFFFFEu, &ctx);
    assert(ctx.gpr[3] == 0u);

    printf("  PASS\n");
}

static void test_lazy_profiler_breadcrumb(void)
{
    printf("[TEST] sys_ppu_thread lazy host-RA breadcrumb resolution...\n");
    ppu_prof_set_resolver(test_ra_resolver);
    g_resolver_calls = 0;

    /* 1) Main guest thread path (ctx outside g_ppu_threads[], slot 0 FREE) */
    ppu_context main_ctx;
    memset(&main_ctx, 0, sizeof(main_ctx));
    main_ctx.thread_id = 1;

    void* fake_ra = (void*)(uintptr_t)0x14005ABCDull;
    for (int i = 0; i < 1000; i++) {
        ppu_prof_stamp_ra(&main_ctx, fake_ra);
    }
    /* Stamping hot path must NOT invoke resolver eagerly */
    assert(g_resolver_calls == 0);

    unsigned out_tid = 0, out_cia = 0;
    const char* out_name = nullptr;
    assert(ppu_prof_snapshot(0, &out_tid, &out_cia, &out_name) == 1);
    assert(g_resolver_calls == 1);
    assert(out_tid == 1u);
    assert(out_cia == test_ra_resolver(fake_ra));

    /* 2) Worker thread path (ctx embedded in g_ppu_threads[1], tid = 2) */
    memset(&g_ppu_threads[1], 0, sizeof(g_ppu_threads[1]));
    g_ppu_threads[1].state = PPU_THREAD_STATE_RUNNING;
    g_ppu_threads[1].ctx.thread_id = 2;
    snprintf(g_ppu_threads[1].name, sizeof(g_ppu_threads[1].name), "worker_test");

    g_resolver_calls = 0;
    void* fake_ra2 = (void*)(uintptr_t)0x140094321ull;
    for (int i = 0; i < 1000; i++) {
        ppu_prof_stamp_ra(&g_ppu_threads[1].ctx, fake_ra2);
    }
    assert(g_resolver_calls == 0);
    assert(g_ppu_threads[1].prof_ra == fake_ra2);
    assert(g_ppu_threads[1].prof_pc == 0u);

    unsigned pc = ppu_thread_prof_pc(2u);
    assert(g_resolver_calls == 1);
    assert(pc == test_ra_resolver(fake_ra2));

    out_tid = out_cia = 0;
    out_name = nullptr;
    assert(ppu_prof_snapshot(1, &out_tid, &out_cia, &out_name) == 1);
    assert(out_tid == 2u);
    assert(out_cia == pc);
    assert(strcmp(out_name, "worker_test") == 0);

    /* Explicit LR stamp still takes precedence with zero resolver calls */
    g_resolver_calls = 0;
    ppu_prof_stamp(&g_ppu_threads[1].ctx, 0x00123450u);
    assert(ppu_thread_prof_pc(2u) == 0x00123450u);
    assert(g_resolver_calls == 0);

    memset(&g_ppu_threads[1], 0, sizeof(g_ppu_threads[1]));
    printf("  PASS\n");
}

static void test_vm_rw_roundtrips(void)
{
    printf("[TEST] ppu_loader vm_read32 / vm_write32 / vm_write64 fast-path parity...\n");
    const uint32_t base = 0x00200000u;
    const uint32_t vals32[6] = {
        0x00000000u, 0xFFFFFFFFu, 0x12345678u, 0x80010002u, 0xDEADFACEu, 0x3F800000u
    };
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t addr = base + i * 8u;
        vm_write32(addr, vals32[i]);
        assert(vm_base[addr + 0] == ((vals32[i] >> 24) & 0xFFu));
        assert(vm_base[addr + 1] == ((vals32[i] >> 16) & 0xFFu));
        assert(vm_base[addr + 2] == ((vals32[i] >>  8) & 0xFFu));
        assert(vm_base[addr + 3] == ((vals32[i] >>  0) & 0xFFu));
        assert(vm_read32(addr) == vals32[i]);
    }

    const uint64_t vals64[4] = {
        0x0123456789ABCDEFull, 0xFEDCBA9876543210ull, 0x00000000CAFEBABEull, 0xFFFFFFFF00000001ull
    };
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t addr = base + 0x100u + i * 8u;
        vm_write64(addr, vals64[i]);
        uint32_t hi = vm_read32(addr);
        uint32_t lo = vm_read32(addr + 4u);
        assert((((uint64_t)hi << 32) | lo) == vals64[i]);
        assert(vm_read64(addr) == vals64[i]);
    }

    /* Inject window (~0x20000000) above 256MB also must work via fast/slow fallback */
    vm_write32(0x10000100u, 0xA5A55A5Au);
    assert(vm_read32(0x10000100u) == 0xA5A55A5Au);
    vm_write64(0x10000200u, 0x1122334455667788ull);
    assert(vm_read64(0x10000200u) == 0x1122334455667788ull);

    printf("  PASS\n");
}

static void test_rsx_commands_zero_xmm(void)
{
    printf("[TEST] rsx_commands float-bit preservation & cold-split dispatch...\n");
    rsx_backend be;
    memset(&be, 0, sizeof(be));
    be.draw_arrays = spy_draw_arrays;
    be.draw_indexed = spy_draw_indexed;
    be.clear = spy_clear;
    rsx_set_backend(&be);

    rsx_state st;
    rsx_state_init(&st);

    /* Non-canonical / signaling-looking float bitpatterns, -0.0, subnormals must
     * be preserved bit-for-bit without XMM/x87 conversion normalizing them. */
    const u32 weird_bits[6] = {
        0x80000000u, /* -0.0f */
        0x7FA12345u, /* SNaN payload */
        0xFF800001u, /* -NaN payload */
        0x00000001u, /* min subnormal */
        0x3F800000u, /* 1.0f */
        0xC2C80000u  /* -100.0f */
    };

    assert(rsx_process_method(&st, NV4097_SET_TRANSFORM_CONSTANT_LOAD, 12u) == 0);
    for (u32 i = 0; i < 6; i++) {
        u32 bits = weird_bits[i];
        assert(rsx_process_method(&st, NV4097_SET_TRANSFORM_CONSTANT + i * 4u, bits) == 0);
        u32 got = 0;
        memcpy(&got, &st.vertex_constants[12u + (i >> 2)][i & 3u], 4);
        assert(got == bits);
    }

    /* NV4097_SET_VERTEX_DATA4F_M range: 0x1C00..0x1CFF */
    assert(rsx_process_method(&st, 0x1C00u + 2u * 16u + 3u * 4u, weird_bits[1]) == 0);
    {
        u32 got = 0;
        memcpy(&got, &st.vertex_data4f[2][3], 4);
        assert(got == weird_bits[1]);
    }

    /* Viewport offset (0x0A20..0x0A2C) and scale (0x0A30..0x0A3C) + clip bounds init */
    assert(rsx_process_method(&st, 0x0A20u + 4u, weird_bits[2]) == 0);
    assert(rsx_process_method(&st, 0x0A30u + 8u, weird_bits[3]) == 0);
    assert(rsx_process_method(&st, 0x0A20u + 0u, weird_bits[0]) == 0);
    assert(rsx_process_method(&st, 0x0A30u + 12u, weird_bits[4]) == 0);
    {
        u32 o0 = 0, o1 = 0, s2 = 0, s3 = 0;
        memcpy(&o0, &st.viewport_offset[0], 4);
        memcpy(&o1, &st.viewport_offset[1], 4);
        memcpy(&s2, &st.viewport_scale[2], 4);
        memcpy(&s3, &st.viewport_scale[3], 4);
        assert(o0 == weird_bits[0]);
        assert(o1 == weird_bits[2]);
        assert(s2 == weird_bits[3]);
        assert(s3 == weird_bits[4]);
    }

    /* BEGIN_END + DRAW_ARRAYS + DRAW_INDEX_ARRAY + CLEAR_SURFACE dispatch */
    g_be_draw_arrays = g_be_draw_indexed = g_be_clear = 0;
    assert(rsx_process_method(&st, NV4097_SET_BEGIN_END, RSX_PRIMITIVE_TRIANGLES) == 0);
    assert(st.in_begin_end == 1);
    assert(st.begin_epoch == 1u);

    assert(rsx_process_method(&st, NV4097_DRAW_ARRAYS, (12u << 24) | 3u) == 0); /* first=3, count=12+1 */
    assert(g_be_draw_arrays == 1);
    assert(g_be_prim == RSX_PRIMITIVE_TRIANGLES);
    assert(g_be_first == 3u && g_be_count == 13u);

    assert(rsx_process_method(&st, NV4097_DRAW_INDEX_ARRAY, (7u << 24) | 10u) == 0);
    assert(g_be_draw_indexed == 1);
    assert(g_be_prim == RSX_PRIMITIVE_TRIANGLES);
    assert(g_be_first == 10u && g_be_count == 8u);

    assert(rsx_process_method(&st, NV4097_SET_BEGIN_END, 0u) == 0);
    assert(st.in_begin_end == 0);

    assert(rsx_process_method(&st, NV4097_SET_COLOR_CLEAR_VALUE, 0xAABBCCDDu) == 0);
    assert(rsx_process_method(&st, NV4097_SET_ZSTENCIL_CLEAR_VALUE, 0x0080002Au) == 0);
    assert(rsx_process_method(&st, NV4097_CLEAR_SURFACE, 0xF3u) == 0);
    assert(g_be_clear == 1);
    assert(g_be_clear_flags == 0xF3u);
    assert(g_be_clear_color == 0xAABBCCDDu);
    assert(g_be_clear_stencil == 0x2Au);

    rsx_set_backend(nullptr);
    printf("  PASS\n");
}

int main(void)
{
    void* mem = mmap(nullptr, 0x20000000ull, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(mem != MAP_FAILED);
    vm_base = (uint8_t*)mem;

    test_milestone_gate();
    test_hle_hash_and_dispatch();
    test_lazy_profiler_breadcrumb();
    test_vm_rw_roundtrips();
    test_rsx_commands_zero_xmm();

    munmap(mem, 0x20000000ull);
    printf("ALL PASS 4 HLE/VM/RSX TESTS PASSED\n");
    return 0;
}
