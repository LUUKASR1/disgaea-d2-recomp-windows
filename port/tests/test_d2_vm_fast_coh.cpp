#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d2_vm_fast.h"
#include "spu_coherency.h"

/* Stubs for the runtime symbols referenced by d2_vm_fast.h and spu_coherency.c */
static uint8_t g_mem[64u * 1024u * 1024u]; /* 64 MiB */
uint8_t* vm_base = g_mem;
uint32_t ppu_vm_size = 0;
uint32_t ppu_hle_inject_base = 0x03000000u;
int g_vm_read_slow_active = 0;
int g_vm_write_slow_active = 0;
uint32_t g_barrier_sync_watch = 0;
uint32_t g_null_sweep_hi = 0;

static unsigned long g_slow_write_calls = 0;
static unsigned long g_wakes = 0;

extern "C" void spu_ch_wake(spu_context* ctx) {
    (void)ctx;
    __atomic_add_fetch(&g_wakes, 1, __ATOMIC_RELAXED);
}

static void slow_write_impl(uint32_t ea, const void* be_bytes, size_t n) {
    __atomic_add_fetch(&g_slow_write_calls, 1, __ATOMIC_RELAXED);
    if (spu_coh_is_reserved(ea)) {
        spu_lockline_lock();
        memcpy(vm_base + ea, be_bytes, n);
        spu_coh_notify_write(ea);
        spu_lockline_unlock();
    } else {
        memcpy(vm_base + ea, be_bytes, n);
    }
}

extern "C" {
uint8_t (vm_read8)(uint64_t a) { return vm_base[(uint32_t)a]; }
uint16_t (vm_read16)(uint64_t a) {
    uint16_t v; memcpy(&v, vm_base + (uint32_t)a, 2); return __builtin_bswap16(v);
}
uint32_t (vm_read32)(uint64_t a) {
    uint32_t v; memcpy(&v, vm_base + (uint32_t)a, 4); return __builtin_bswap32(v);
}
uint64_t (vm_read64)(uint64_t a) {
    uint64_t v; memcpy(&v, vm_base + (uint32_t)a, 8); return __builtin_bswap64(v);
}
void (vm_write8)(uint64_t a, uint8_t v) { slow_write_impl((uint32_t)a, &v, 1); }
void (vm_write16)(uint64_t a, uint16_t v) { uint16_t b = __builtin_bswap16(v); slow_write_impl((uint32_t)a, &b, 2); }
void (vm_write32)(uint64_t a, uint32_t v) { uint32_t b = __builtin_bswap32(v); slow_write_impl((uint32_t)a, &b, 4); }
void (vm_write64)(uint64_t a, uint64_t v) { uint64_t b = __builtin_bswap64(v); slow_write_impl((uint32_t)a, &b, 8); }
}

static spu_context g_spu0, g_spu1;

static void reserve_line(spu_context* ctx, uint32_t ea) {
    ea &= ~127u;
    spu_lockline_lock();
    spu_coh_reserve(ctx, ea);
    ctx->resv_ea = ea;
    ctx->resv_valid = 1;
    spu_lockline_unlock();
}

static uint32_t bloom_bucket(uint32_t ea) {
    uint32_t line = ea >> 7;
    return (line ^ (line >> 6) ^ (line >> 12)) & 63u;
}

/* Concurrent lockless handoff stress test across fresh widening lines:
 * SPU reserves under lock, publishes reserved_seq BEFORE releasing lock
 * (so PPU starts d2_vm_write32 concurrently with SPU's snapshot+unlock),
 * verifying zero missed slow-path detections / SPU_EVENT_LR notifications. */
struct race_state {
    uint32_t base_ea;
    uint32_t iters;
    int go_spu;
    int reserved_seq;
    int spu_done;
    uint32_t snap;
    spu_context ctx;
};

static void* race_spu_thread(void* arg) {
    struct race_state* r = (struct race_state*)arg;
    spu_context_init(&r->ctx, 7);
    for (uint32_t i = 0; i < r->iters; i++) {
        uint32_t ea = r->base_ea + i * 128u;
        while (__atomic_load_n(&r->go_spu, __ATOMIC_ACQUIRE) != (int)(i + 1)) {}

        spu_lockline_lock();
        spu_coh_reserve(&r->ctx, ea);
        r->ctx.resv_ea = ea;
        r->ctx.resv_valid = 1;
        r->ctx.event_status = 0;
        /* Signal PPU immediately after spu_coh_reserve while still inside lock */
        __atomic_store_n(&r->reserved_seq, (int)(i + 1), __ATOMIC_RELEASE);
        uint32_t v;
        memcpy(&v, vm_base + ea, 4);
        r->snap = __builtin_bswap32(v);
        spu_lockline_unlock();

        __atomic_store_n(&r->spu_done, (int)(i + 1), __ATOMIC_RELEASE);
    }
    return NULL;
}

int main(void) {
    const uint32_t STACK_EA = 0x03800000u; /* representative stack/non-SPU EA inside g_mem */
    const uint32_t REAL_STACK_EA = 0xD0001000u; /* real PS3 PPU stack address (out of range) */
    const uint32_t LINE_A = 0x00040000u;
    const uint32_t LINE_B = 0x00800000u;

    /* 1. Unarmed fast bypass */
    assert(g_spu_coh_range == 0x00000000FFFFFFFFULL);
    assert(d2_vm_write_is_slow(LINE_A) == 0);
    assert(d2_vm_write_is_slow(STACK_EA) == 0);
    assert(d2_vm_write_is_slow(REAL_STACK_EA) == 0);

    d2_vm_write32(STACK_EA, 0x12345678u);
    assert(g_slow_write_calls == 0);
    assert(vm_read32(STACK_EA) == 0x12345678u);

    /* 2. Reserve LINE_A: out-of-range (stack & high addresses) must stay fast */
    spu_context_init(&g_spu0, 0);
    reserve_line(&g_spu0, LINE_A);

    assert((uint32_t)g_spu_coh_range == LINE_A);
    assert((uint32_t)(g_spu_coh_range >> 32) == (LINE_A | 127u));
    assert(d2_vm_write_is_slow(REAL_STACK_EA) == 0);
    assert(d2_vm_write_is_slow(STACK_EA) == 0);
    assert(d2_vm_write_is_slow(LINE_A - 128u) == 0);
    assert(d2_vm_write_is_slow(LINE_A + 128u) == 0);
    assert(d2_vm_write_is_slow(LINE_A + 16u) == 1);

    /* Stack-like write bypasses slow path completely and leaves reservation intact */
    unsigned long slow_before = g_slow_write_calls;
    d2_vm_write32(STACK_EA, 0xCAFEBABEu);
    assert(g_slow_write_calls == slow_before);
    assert(vm_read32(STACK_EA) == 0xCAFEBABEu);
    assert(g_spu0.resv_valid == 1);
    assert((g_spu0.event_status & SPU_EVENT_LR) == 0);

    /* 3. Widen range with LINE_B: intermediate non-reserved lines bypass via bloom & bitmap */
    spu_context_init(&g_spu1, 1);
    reserve_line(&g_spu1, LINE_B);
    assert((uint32_t)g_spu_coh_range == LINE_A);
    assert((uint32_t)(g_spu_coh_range >> 32) == (LINE_B | 127u));

    uint32_t mid_no_bloom = 0;
    uint32_t mid_bloom_collide = 0;
    uint32_t b_a = bloom_bucket(LINE_A);
    for (uint32_t ea = LINE_A + 128u; ea < LINE_B; ea += 128u) {
        uint32_t b = bloom_bucket(ea);
        if (!mid_no_bloom && !((g_spu_coh_line_bloom >> b) & 1ULL))
            mid_no_bloom = ea;
        if (!mid_bloom_collide && b == b_a)
            mid_bloom_collide = ea;
        if (mid_no_bloom && mid_bloom_collide) break;
    }
    assert(mid_no_bloom != 0 && mid_bloom_collide != 0);
    assert(d2_vm_write_is_slow(mid_no_bloom) == 0);
    assert(d2_vm_write_is_slow(mid_bloom_collide) == 0);

    d2_vm_write32(mid_no_bloom + 4u, 0x11223344u);
    d2_vm_write64(mid_bloom_collide + 8u, 0x0102030405060708ULL);
    assert(g_slow_write_calls == slow_before);
    assert(g_spu0.resv_valid == 1 && g_spu1.resv_valid == 1);

    /* 4. Inside-range writes to reserved lines MUST invalidate & wake across all widths */
    d2_vm_write8(LINE_A + 3u, 0xAAu);
    assert(g_slow_write_calls == slow_before + 1);
    assert(vm_read8(LINE_A + 3u) == 0xAAu);
    assert(g_spu0.resv_valid == 0);
    assert((g_spu0.event_status & SPU_EVENT_LR) == SPU_EVENT_LR);
    assert(g_spu1.resv_valid == 1);

    d2_vm_write16(LINE_B + 2u, 0xBEEFu);
    assert(g_slow_write_calls == slow_before + 2);
    assert(vm_read16(LINE_B + 2u) == 0xBEEFu);
    assert(g_spu1.resv_valid == 0);
    assert((g_spu1.event_status & SPU_EVENT_LR) == SPU_EVENT_LR);

    g_spu0.event_status = 0;
    reserve_line(&g_spu0, LINE_A);
    d2_vm_write32(LINE_A + 16u, 0xDEADBEEFu);
    assert(g_spu0.resv_valid == 0 && (g_spu0.event_status & SPU_EVENT_LR));
    assert(vm_read32(LINE_A + 16u) == 0xDEADBEEFu);

    g_spu1.event_status = 0;
    reserve_line(&g_spu1, LINE_B);
    d2_vm_write64(LINE_B + 24u, 0x1122334455667788ULL);
    assert(g_spu1.resv_valid == 0 && (g_spu1.event_status & SPU_EVENT_LR));
    assert(vm_read64(LINE_B + 24u) == 0x1122334455667788ULL);

    /* 5. Concurrent reservation widening + stack fast-bypass vs reserved invalidation */
    static struct race_state rs;
    rs.base_ea = 0x01000000u;
    rs.iters = 4000;
    unsigned long base_slow = __atomic_load_n(&g_slow_write_calls, __ATOMIC_RELAXED);
    pthread_t th;
    assert(pthread_create(&th, NULL, race_spu_thread, &rs) == 0);
    for (uint32_t i = 0; i < rs.iters; i++) {
        uint32_t ea = rs.base_ea + i * 128u;
        __atomic_store_n(&rs.go_spu, (int)(i + 1), __ATOMIC_RELEASE);
        while (__atomic_load_n(&rs.reserved_seq, __ATOMIC_ACQUIRE) != (int)(i + 1)) {}
        /* Stack write stays fast; reserved write MUST see widened range+bloom+bitmap */
        d2_vm_write32(STACK_EA, i + 1u);
        d2_vm_write32(ea, 0xA0000000u + i);
        while (__atomic_load_n(&rs.spu_done, __ATOMIC_ACQUIRE) != (int)(i + 1)) {}
        assert(rs.snap == 0u);
        assert((rs.ctx.event_status & SPU_EVENT_LR) == SPU_EVENT_LR);
        assert(rs.ctx.resv_valid == 0);
        assert(vm_read32(ea) == (0xA0000000u + i));
    }
    pthread_join(th, NULL);
    /* Exactly rs.iters slow writes (all rs.iters stack writes bypassed the slow path) */
    assert(__atomic_load_n(&g_slow_write_calls, __ATOMIC_RELAXED) == base_slow + rs.iters);

    spu_coh_unregister(&rs.ctx);
    spu_coh_unregister(&g_spu0);
    spu_coh_unregister(&g_spu1);

    puts("d2_vm_fast + spu_coherency guard tests: PASS (range/bloom bypass, 4 widths, 4000 concurrent widening races)");
    return 0;
}
