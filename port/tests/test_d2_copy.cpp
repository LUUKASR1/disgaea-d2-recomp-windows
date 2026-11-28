/* Asset-free regression tests for native guest memcpy (port/src/d2_copy.cpp).
 * Verifies func_00335FE4 against a pure-C big-endian reference implementation
 * across sizes 0..512, all src/dst alignments 0..15, both page-offset directions,
 * large copies, touching endpoints, zero-length calls, and fallback triggers
 * (overlaps, high 32-bit bits, 32-bit wrap, <0x10000, OOB, and GCM HLE range). */
#include "ppu_recomp.h"
#include <assert.h>
#include <initializer_list>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;
uint32_t ppu_hle_inject_base = 0x03000000u;
const func_entry function_table[] = {{0, nullptr, nullptr}};
const uint64_t function_table_count = 0;

/* Minimal stubs required by ppu_loader.cpp / spu_coherency.c when dead-stripped */
int spu_raw_is_reg(uint32_t) { return 0; }
int spu_raw_reg_load(uint32_t, uint32_t*) { return 0; }
void spu_raw_reg_store(uint32_t, uint32_t, int) {}
void cellGcm_notify_guest_write(uint32_t, uint32_t) {}
void cellGcm_ref_on_poll(void) {}
void vm_commit(uint32_t, uint32_t) {}
void spu_ch_wake(void*) {}
}

enum : uint32_t { VM = 0x400000 }; /* 4 MiB */

static uint64_t g_fallback_calls = 0;
static ppu_context g_last_fallback_ctx;

void func_00335FE4(ppu_context*);
void d2_original_00335FE4(ppu_context* ctx)
{
    g_fallback_calls++;
    g_last_fallback_ctx = *ctx;
    const uint64_t dst = ctx->gpr[3];
    const uint64_t src = ctx->gpr[4];
    const uint64_t n = ctx->gpr[5];
    if (((dst | src | n) >> 32) == 0 && (dst + n) <= VM && (src + n) <= VM && n > 0) {
        memmove(vm_base + (uint32_t)dst, vm_base + (uint32_t)src, (size_t)n);
    }
    ctx->gpr[3] = (uint32_t)dst;
}

static uint8_t g_initial_vm[VM];
static uint8_t g_want_vm[VM];

static uint64_t splitmix64(uint64_t* state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void init_pattern()
{
    uint64_t s = 0x123456789ABCDEF0ULL;
    for (uint32_t i = 0; i < VM; i += 8) {
        uint64_t v = splitmix64(&s);
        memcpy(g_initial_vm + i, &v, 8);
    }
}

static ppu_context seed_ctx(uint64_t dst, uint64_t src, uint64_t n, uint64_t salt)
{
    ppu_context c;
    memset(&c, 0, sizeof c);
    uint64_t s = salt ^ 0xA5A5A5A55A5A5A5AULL;
    for (int i = 0; i < 32; i++) c.gpr[i] = splitmix64(&s);
    c.cr = (uint32_t)splitmix64(&s);
    c.ctr = (uint64_t)(uint32_t)splitmix64(&s) | 1ULL;
    c.lr = 0x12345678ULL;
    c.xer = (uint32_t)splitmix64(&s);
    c.gpr[3] = dst;
    c.gpr[4] = src;
    c.gpr[5] = n;
    return c;
}

static void check_nonvolatile_abi(const char* tag, const ppu_context& before,
                                  const ppu_context& after, uint32_t expected_r3)
{
    if (after.gpr[3] != expected_r3) {
        fprintf(stderr, "FAIL [%s] r3=0x%llx expected=0x%x\n",
                tag, (unsigned long long)after.gpr[3], expected_r3);
        exit(1);
    }
    if (after.gpr[1] != before.gpr[1] || after.gpr[2] != before.gpr[2] ||
        after.lr != before.lr || after.xer != before.xer ||
        (after.cr & 0x00FFF000u) != (before.cr & 0x00FFF000u)) {
        fprintf(stderr, "FAIL [%s] non-volatile state mutated\n", tag);
        exit(1);
    }
    for (int i = 13; i < 32; i++) {
        if (after.gpr[i] != before.gpr[i]) {
            fprintf(stderr, "FAIL [%s] callee-saved gpr[%d] mutated\n", tag, i);
            exit(1);
        }
    }
}

static void check_fast_case(const char* tag, uint64_t dst, uint64_t src, uint64_t n,
                            uint64_t salt, uint32_t win_lo, uint32_t win_len)
{
    const ppu_context before = seed_ctx(dst, src, n, salt);
    ppu_context got = before;

    memcpy(g_want_vm + win_lo, g_initial_vm + win_lo, win_len);
    if (n > 0) {
        memcpy(g_want_vm + (uint32_t)dst, g_initial_vm + (uint32_t)src, (size_t)n);
    }

    memcpy(vm_base + win_lo, g_initial_vm + win_lo, win_len);
    const uint64_t fb_before = g_fallback_calls;
    func_00335FE4(&got);

    if (g_fallback_calls != fb_before) {
        fprintf(stderr, "FAIL [%s] unexpected fallback dst=0x%llx src=0x%llx n=0x%llx\n",
                tag, (unsigned long long)dst, (unsigned long long)src, (unsigned long long)n);
        exit(1);
    }
    if (memcmp(g_want_vm + win_lo, vm_base + win_lo, win_len) != 0) {
        for (uint32_t i = 0; i < win_len; i++) {
            uint32_t addr = win_lo + i;
            if (g_want_vm[addr] != vm_base[addr]) {
                fprintf(stderr, "FAIL [%s] byte diff @0x%06x: want=0x%02x got=0x%02x\n",
                        tag, addr, g_want_vm[addr], vm_base[addr]);
                break;
            }
        }
        exit(1);
    }
    check_nonvolatile_abi(tag, before, got, (uint32_t)dst);
}

static void check_fallback_case(const char* tag, uint64_t dst, uint64_t src, uint64_t n,
                                uint64_t salt, uint32_t win_lo, uint32_t win_len,
                                bool verify_memmove)
{
    const ppu_context before = seed_ctx(dst, src, n, salt);
    ppu_context got = before;

    memcpy(vm_base + win_lo, g_initial_vm + win_lo, win_len);
    const uint64_t fb_before = g_fallback_calls;
    func_00335FE4(&got);

    if (g_fallback_calls != fb_before + 1) {
        fprintf(stderr, "FAIL [%s] expected fallback call (dst=0x%llx src=0x%llx n=0x%llx)\n",
                tag, (unsigned long long)dst, (unsigned long long)src, (unsigned long long)n);
        exit(1);
    }
    if (g_last_fallback_ctx.gpr[3] != dst || g_last_fallback_ctx.gpr[4] != src ||
        g_last_fallback_ctx.gpr[5] != n) {
        fprintf(stderr, "FAIL [%s] fallback received altered args\n", tag);
        exit(1);
    }
    if (verify_memmove) {
        memcpy(g_want_vm + win_lo, g_initial_vm + win_lo, win_len);
        if (n > 0) {
            memmove(g_want_vm + (uint32_t)dst, g_initial_vm + (uint32_t)src, (size_t)n);
        }
        assert(memcmp(g_want_vm + win_lo, vm_base + win_lo, win_len) == 0);
    }
    check_nonvolatile_abi(tag, before, got, (uint32_t)dst);
}

int main()
{
    init_pattern();
    vm_base = (uint8_t*)malloc(VM);
    assert(vm_base);
    memcpy(vm_base, g_initial_vm, VM);
    ppu_vm_size = VM;

    uint64_t cases = 0;
    uint64_t salt = 1;

    /* 1. Exhaustive disjoint sizes 0..512 across all 16x16 alignments and
     *    both page-offset directions */
    for (uint64_t n = 0; n <= 512; n++) {
        for (uint64_t sa = 0; sa < 16; sa++) {
            for (uint64_t da = 0; da < 16; da++) {
                check_fast_case("0..512-same-page-off", 0x12000 + da, 0x10000 + sa, n,
                                salt++, 0xF800, 0x3800);
                check_fast_case("0..512-fwd-forced", 0x10000 + da, 0x10800 + sa, n,
                                salt++, 0xF800, 0x3800);
                check_fast_case("0..512-bwd-forced", 0x10800 + da, 0x10000 + sa, n,
                                salt++, 0xF800, 0x3800);
                cases += 3;
            }
        }
    }

    /* 2. Boundary & larger copies across full VM with canary verification */
    const uint64_t big_sizes[] = {
        0, 1, 7, 8, 15, 16, 17, 23, 24, 25, 47, 48, 49,
        95, 96, 97, 143, 144, 145, 255, 256, 257, 287, 288, 289,
        511, 512, 513, 4095, 4096, 4097, 65536, 0x100000
    };
    for (uint64_t n : big_sizes) {
        if (n <= 4097) {
            for (uint64_t sa = 0; sa < 16; sa += 3) {
                for (uint64_t da = 0; da < 16; da += 3) {
                    check_fast_case("fullvm-fwd", 0x10000 + da, 0x20800 + sa, n, salt++, 0, VM);
                    check_fast_case("fullvm-bwd", 0x20800 + da, 0x10000 + sa, n, salt++, 0, VM);
                    check_fast_case("fullvm-eq",  0x10000 + da, 0x20000 + sa, n, salt++, 0, VM);
                    cases += 3;
                }
            }
        } else {
            check_fast_case("large-fwd", 0x20000, 0x180000 + 5, n, salt++, 0, VM);
            check_fast_case("large-bwd", 0x180000 + 7, 0x20000, n, salt++, 0, VM);
            cases += 2;
        }
    }

    /* 3. Adjacent non-overlapping touching endpoints */
    for (uint64_t n : {0ULL, 1ULL, 7ULL, 8ULL, 16ULL, 24ULL, 48ULL, 64ULL, 256ULL, 257ULL, 512ULL}) {
        for (uint64_t off = 0; off < 16; off++) {
            uint64_t a = 0x20000 + off;
            check_fast_case("adj-dst-before-src", a, a + n, n, salt++, 0x1F000, 0x2000);
            check_fast_case("adj-src-before-dst", a + n, a, n, salt++, 0x1F000, 0x2000);
            cases += 2;
        }
    }

    /* 4. Overlapping vs disjoint nearby ranges */
    const int64_t deltas[] = {-64, -17, -16, -8, -4, -1, 0, 1, 4, 8, 16, 17, 64};
    const uint64_t ov_sizes[] = {1, 7, 8, 15, 16, 24, 48, 64, 128, 256, 257, 512};
    for (uint64_t n : ov_sizes) {
        for (int64_t d : deltas) {
            const bool overlaps = (d < 0) ? ((uint64_t)(-d) < n) : ((uint64_t)d < n);
            for (uint64_t align = 0; align < 8; align++) {
                uint64_t src = 0x20000 + align;
                uint64_t dst = (uint64_t)((int64_t)src + d);
                if (overlaps) {
                    check_fallback_case("overlap", dst, src, n, salt++, 0x1F000, 0x2000, true);
                } else {
                    check_fast_case("near-disjoint", dst, src, n, salt++, 0x1F000, 0x2000);
                }
                cases++;
            }
        }
    }

    /* 5. Zero-length fast-path vs invalid-pointer fallback cases */
    const uint64_t valid_zero_addrs[] = {0x10000ULL, 0x10007ULL, 0x20000ULL, (uint64_t)VM - 1};
    for (uint64_t dst : valid_zero_addrs) {
        for (uint64_t src : valid_zero_addrs) {
            check_fast_case("zero-len-valid", dst, src, 0, salt++, 0, VM);
            cases++;
        }
    }
    for (uint64_t bad : {0ULL, 0x800ULL, 0x8000ULL, (uint64_t)VM + 1ULL, 1ULL << 32}) {
        check_fallback_case("zero-len-bad-dst", bad, 0x10000ULL, 0, salt++, 0, VM, false);
        check_fallback_case("zero-len-bad-src", 0x10000ULL, bad, 0, salt++, 0, VM, false);
        cases += 2;
    }

    /* 6. Explicit fallback triggers */
    struct FallbackCase { const char* name; uint64_t dst, src, n; };
    const FallbackCase fallbacks[] = {
        {"hi-dst",     (1ULL << 32) | 0x20000ULL, 0x10000ULL, 32},
        {"hi-src",     0x20000ULL, (1ULL << 32) | 0x10000ULL, 32},
        {"hi-n",       0x20000ULL, 0x10000ULL, (1ULL << 32) | 8ULL},
        {"hi-n-small", 0x20000ULL, 0x10000ULL, (1ULL << 32) | 4ULL},
        {"wrap-dst",   0xFFFFFFF0ULL, 0x10000ULL, 32},
        {"wrap-src",   0x20000ULL, 0xFFFFFFF0ULL, 32},
        {"wrap-dst-1", 0xFFFFFFFFULL, 0x10000ULL, 1},
        {"wrap-src-1", 0x20000ULL, 0xFFFFFFFFULL, 1},
        {"null-dst",   0x800ULL, 0x10000ULL, 64},
        {"null-src",   0x10000ULL, 0x800ULL, 64},
        {"low-dst",    0x8000ULL, 0x10000ULL, 64},
        {"low-src",    0x10000ULL, 0x8000ULL, 64},
        {"oob-dst",    VM - 16, 0x10000ULL, 32},
        {"oob-src",    0x20000ULL, VM - 16, 32},
    };
    for (const auto& fc : fallbacks) {
        check_fallback_case(fc.name, fc.dst, fc.src, fc.n, salt++, 0, VM, false);
        cases++;
    }

    /* 7. GCM HLE window fallback: vm_copy declines [gcm, gcm + 0x8000) */
    ppu_hle_inject_base = 0x300000u;
    check_fallback_case("gcm-dst", 0x300100u, 0x10000u, 64, salt++, 0, VM, false);
    check_fallback_case("gcm-src", 0x10000u, 0x304100u, 64, salt++, 0, VM, false);
    cases += 2;
    ppu_hle_inject_base = 0x03000000u;

    printf("PASS: %llu asset-free d2_copy checks passed (fast path + fallback guards)\n",
           (unsigned long long)cases);

    free(vm_base);
    return 0;
}
