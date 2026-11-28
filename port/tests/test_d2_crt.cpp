/* Asset-free regression tests for native guest CRT overrides (port/src/d2_crt.cpp):
 *   memmove (003365A8), strlen (00338980), strcmp (0033A320),
 *   strcpy  (0033A474), strncmp (0033A6C4), strncpy (0033A888).
 *
 * Verifies exact guest memory bytes (including surrounding canaries), unsigned-byte
 * comparison semantics, PS3 32-bit return values (r3), non-volatile ABI preservation
 * (r1, r2, r13..r31, LR, XER, CR2..CR4), and fallback delegation to d2_original_*. */
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
extern int g_vm_read_slow_active;
extern int g_vm_write_slow_active;
extern uint32_t g_barrier_sync_watch;
extern uint32_t g_null_sweep_hi;
const func_entry function_table[] = {{0, nullptr, nullptr}};
const uint64_t function_table_count = 0;

int spu_raw_is_reg(uint32_t) { return 0; }
int spu_raw_reg_load(uint32_t, uint32_t*) { return 0; }
void spu_raw_reg_store(uint32_t, uint32_t, int) {}
void cellGcm_notify_guest_write(uint32_t, uint32_t) {}
void cellGcm_ref_on_poll(void) {}
void vm_commit(uint32_t, uint32_t) {}
void spu_ch_wake(void*) {}
}

enum : uint32_t { VM = 0x200000 }; /* 2 MiB */

static uint64_t g_fb_memmove = 0;
static uint64_t g_fb_strlen  = 0;
static uint64_t g_fb_strcmp  = 0;
static uint64_t g_fb_strcpy  = 0;
static uint64_t g_fb_strncmp = 0;
static uint64_t g_fb_strncpy = 0;

void func_003365A8(ppu_context*);
void func_00338980(ppu_context*);
void func_0033A320(ppu_context*);
void func_0033A474(ppu_context*);
void func_0033A6C4(ppu_context*);
void func_0033A888(ppu_context*);

void d2_original_003365A8(ppu_context* ctx) { g_fb_memmove++; ctx->gpr[3] = (uint32_t)ctx->gpr[3]; }
void d2_original_00338980(ppu_context*) { g_fb_strlen++; }
void d2_original_0033A320(ppu_context*) { g_fb_strcmp++; }
void d2_original_0033A474(ppu_context* ctx) { g_fb_strcpy++;  ctx->gpr[3] = (uint32_t)ctx->gpr[3]; }
void d2_original_0033A6C4(ppu_context*) { g_fb_strncmp++; }
void d2_original_0033A888(ppu_context* ctx) { g_fb_strncpy++; ctx->gpr[3] = (uint32_t)ctx->gpr[3]; }

static uint8_t g_base_vm[VM];
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
    uint64_t s = 0xCAFEBABEDEADBEEFULL;
    for (uint32_t i = 0; i < VM; i += 8) {
        uint64_t v = splitmix64(&s);
        memcpy(g_base_vm + i, &v, 8);
    }
    for (uint32_t i = 0; i < VM; i++) {
        if (g_base_vm[i] == 0) g_base_vm[i] = 0xA5;
    }
}

static ppu_context seed_ctx(uint64_t r3, uint64_t r4, uint64_t r5, uint64_t salt)
{
    ppu_context c;
    memset(&c, 0, sizeof c);
    uint64_t s = salt ^ 0x55555555AAAAAAAAULL;
    for (int i = 0; i < 32; i++) c.gpr[i] = splitmix64(&s);
    c.cr = (uint32_t)splitmix64(&s);
    c.ctr = (uint64_t)(uint32_t)splitmix64(&s) | 1ULL;
    c.lr = 0x12345678ULL;
    c.xer = (uint32_t)splitmix64(&s);
    c.gpr[3] = r3;
    c.gpr[4] = r4;
    c.gpr[5] = r5;
    return c;
}

static void check_nonvolatile_abi(const char* name, const char* tag,
                                  const ppu_context& before, const ppu_context& after)
{
    if (after.gpr[1] != before.gpr[1] || after.gpr[2] != before.gpr[2] ||
        after.lr != before.lr || after.xer != before.xer ||
        (after.cr & 0x00FFF000u) != (before.cr & 0x00FFF000u)) {
        fprintf(stderr, "FAIL [%s / %s] non-volatile ABI state mutated\n", name, tag);
        exit(1);
    }
    for (int i = 13; i < 32; i++) {
        if (after.gpr[i] != before.gpr[i]) {
            fprintf(stderr, "FAIL [%s / %s] callee-saved gpr[%d] mutated\n", name, tag, i);
            exit(1);
        }
    }
}

static void fill_str(uint8_t* buf, uint32_t addr, uint32_t len, uint64_t* s)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)(splitmix64(s) & 0xFF);
        if (c == 0) c = (uint8_t)(0x41 + (i & 15));
        buf[addr + i] = c;
    }
    buf[addr + len] = 0;
}

static uint64_t ref_strcmp_u8(const uint8_t* a, const uint8_t* b)
{
    for (;;) {
        if (*a != *b) return (*a < *b) ? (uint64_t)(int64_t)-1 : 1ULL;
        if (*a == 0) return 0ULL;
        a++;
        b++;
    }
}

static uint64_t ref_strncmp_u8(const uint8_t* a, const uint8_t* b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (a[i] < b[i]) ? (uint64_t)(int64_t)-1 : 1ULL;
        if (a[i] == 0) return 0ULL;
    }
    return 0ULL;
}

int main()
{
    init_pattern();
    vm_base = (uint8_t*)malloc(VM);
    assert(vm_base);
    memcpy(vm_base, g_base_vm, VM);
    ppu_vm_size = VM;

    static uint8_t snap[VM];
    uint64_t cases = 0;
    uint64_t salt = 1;
    uint64_t rng = 0x8765432101234567ULL;

    /* ---------------- 1. memmove (003365A8) ---------------- */
    for (uint64_t n : {0ULL, 1ULL, 2ULL, 3ULL, 4ULL, 7ULL, 8ULL, 15ULL, 16ULL, 24ULL,
                       31ULL, 32ULL, 33ULL, 47ULL, 48ULL, 63ULL, 64ULL, 65ULL, 128ULL, 257ULL, 512ULL}) {
        for (uint64_t sa = 0; sa < 16; sa += 3) {
            for (uint64_t da = 0; da < 16; da += 3) {
                for (int dir = 0; dir < 2; dir++) {
                    uint64_t dst = (dir == 0 ? 0x10000 : 0x12000) + da;
                    uint64_t src = (dir == 0 ? 0x12000 : 0x10000) + sa;
                    ppu_context before = seed_ctx(dst, src, n, salt++);
                    ppu_context got = before;
                    memcpy(g_want_vm + 0xF800, g_base_vm + 0xF800, 0x3800);
                    memmove(g_want_vm + dst, g_base_vm + src, (size_t)n);
                    memcpy(vm_base + 0xF800, g_base_vm + 0xF800, 0x3800);
                    uint64_t fb = g_fb_memmove;
                    func_003365A8(&got);
                    assert(g_fb_memmove == fb);
                    assert(memcmp(g_want_vm + 0xF800, vm_base + 0xF800, 0x3800) == 0);
                    assert(got.gpr[3] == (uint32_t)dst);
                    check_nonvolatile_abi("memmove", "disjoint", before, got);
                    cases++;
                }
            }
        }
    }
    for (uint64_t n = 0; n <= 128; n++) {
        for (int64_t delta : {-64, -31, -16, -8, -7, -4, -3, -2, -1, 0, 1, 2, 3, 4, 7, 8, 16, 31, 64}) {
            uint64_t src = 0x20000 + (n & 15);
            uint64_t dst = (uint64_t)((int64_t)src + delta);
            ppu_context before = seed_ctx(dst, src, n, salt++);
            ppu_context got = before;
            memcpy(g_want_vm + 0x1F000, g_base_vm + 0x1F000, 0x2000);
            memmove(g_want_vm + dst, g_base_vm + src, (size_t)n);
            memcpy(vm_base + 0x1F000, g_base_vm + 0x1F000, 0x2000);
            uint64_t fb = g_fb_memmove;
            func_003365A8(&got);
            assert(g_fb_memmove == fb);
            assert(memcmp(g_want_vm + 0x1F000, vm_base + 0x1F000, 0x2000) == 0);
            assert(got.gpr[3] == (uint32_t)dst);
            check_nonvolatile_abi("memmove", "overlap", before, got);
            cases++;
        }
    }
    /* Large memmove + fallback guards */
    {
        ppu_context before = seed_ctx(0x40000 + 7, 0x40000 + 1, 65536, salt++);
        ppu_context got = before;
        memcpy(g_want_vm, g_base_vm, VM);
        memmove(g_want_vm + 0x40007, g_base_vm + 0x40001, 65536);
        memcpy(vm_base, g_base_vm, VM);
        uint64_t fb = g_fb_memmove;
        func_003365A8(&got);
        assert(g_fb_memmove == fb && memcmp(g_want_vm, vm_base, VM) == 0);
        check_nonvolatile_abi("memmove", "large-overlap", before, got);
        cases++;
    }
    struct { uint64_t dst, src, n; } mm_fb[] = {
        {0x800, 0x10000, 32}, {VM - 16, 0x10000, 32},
        {(1ULL << 32) | 0x20000, 0x10000, 16}, {0xFFFFFFF0ULL, 0x10000, 32},
    };
    for (const auto& c : mm_fb) {
        ppu_context got = seed_ctx(c.dst, c.src, c.n, salt++);
        uint64_t fb = g_fb_memmove;
        func_003365A8(&got);
        assert(g_fb_memmove == fb + 1);
        cases++;
    }
    ppu_hle_inject_base = 0x50000u;
    {
        ppu_context got = seed_ctx(0x50100, 0x10000, 32, salt++);
        uint64_t fb = g_fb_memmove;
        func_003365A8(&got);
        assert(g_fb_memmove == fb + 1);
    }
    ppu_hle_inject_base = 0x03000000u;
    g_vm_read_slow_active = 1;
    { ppu_context got = seed_ctx(0x20000, 0x10000, 32, salt++); uint64_t fb = g_fb_memmove; func_003365A8(&got); assert(g_fb_memmove == fb + 1); }
    g_vm_read_slow_active = 0;
    g_vm_write_slow_active = 1;
    { ppu_context got = seed_ctx(0x20000, 0x10000, 32, salt++); uint64_t fb = g_fb_memmove; func_003365A8(&got); assert(g_fb_memmove == fb + 1); }
    g_vm_write_slow_active = 0;
    g_barrier_sync_watch = 1;
    { ppu_context got = seed_ctx(0x20000, 0x10000, 32, salt++); uint64_t fb = g_fb_memmove; func_003365A8(&got); assert(g_fb_memmove == fb + 1); }
    g_barrier_sync_watch = 0;
    g_null_sweep_hi = 1;
    { ppu_context got = seed_ctx(0x20000, 0x10000, 32, salt++); uint64_t fb = g_fb_memmove; func_003365A8(&got); assert(g_fb_memmove == fb + 1); }
    g_null_sweep_hi = 0;
    cases += 5;

    /* ---------------- 2. strlen (00338980) ---------------- */
    for (uint32_t len = 0; len <= 160; len++) {
        for (uint32_t al = 0; al < 16; al++) {
            memcpy(vm_base + 0x10000, g_base_vm + 0x10000, 0x200);
            fill_str(vm_base, 0x10000 + al, len, &rng);
            memcpy(snap + 0x10000, vm_base + 0x10000, 0x200);
            ppu_context before = seed_ctx(0x10000 + al, 0, 0, salt++);
            ppu_context got = before;
            uint64_t fb = g_fb_strlen;
            func_00338980(&got);
            assert(g_fb_strlen == fb);
            assert(got.gpr[3] == len);
            assert(memcmp(snap + 0x10000, vm_base + 0x10000, 0x200) == 0);
            check_nonvolatile_abi("strlen", "len-align", before, got);
            cases++;
        }
    }
    memcpy(vm_base, g_base_vm, VM);
    fill_str(vm_base, VM - 17, 16, &rng);
    {
        ppu_context before = seed_ctx(VM - 17, 0, 0, salt++);
        ppu_context got = before;
        uint64_t fb = g_fb_strlen;
        func_00338980(&got);
        assert(g_fb_strlen == fb && got.gpr[3] == 16);
        check_nonvolatile_abi("strlen", "vm-end-exact", before, got);
    }
    vm_base[VM - 1] = 0x55; /* unterminated -> fallback */
    for (uint64_t bad : {(uint64_t)(VM - 17), 0x8000ULL, (1ULL << 32) | 0x10000ULL}) {
        ppu_context got = seed_ctx(bad, 0, 0, salt++);
        uint64_t fb = g_fb_strlen;
        func_00338980(&got);
        assert(g_fb_strlen == fb + 1);
    }
    g_vm_read_slow_active = 1;
    { ppu_context got = seed_ctx(0x10000, 0, 0, salt++); uint64_t fb = g_fb_strlen; func_00338980(&got); assert(g_fb_strlen == fb + 1); }
    g_vm_read_slow_active = 0;
    cases += 5;

    /* ---------------- 3. strcmp (0033A320) ---------------- */
    for (uint32_t len = 0; len <= 40; len++) {
        for (uint32_t a1 = 0; a1 < 16; a1 += 3) {
            for (uint32_t a2 = 0; a2 < 16; a2 += 3) {
                memcpy(vm_base + 0x10000, g_base_vm + 0x10000, 0x400);
                fill_str(vm_base, 0x10000 + a1, len, &rng);
                memcpy(vm_base + 0x10200 + a2, vm_base + 0x10000 + a1, len + 1);
                {
                    ppu_context before = seed_ctx(0x10000 + a1, 0x10200 + a2, 0, salt++);
                    ppu_context got = before;
                    uint64_t fb = g_fb_strcmp;
                    func_0033A320(&got);
                    assert(g_fb_strcmp == fb && got.gpr[3] == 0);
                    check_nonvolatile_abi("strcmp", "eq", before, got);
                    cases++;
                }
                for (uint32_t pos = 0; pos <= len; pos += (pos < 8 || pos + 2 >= len ? 1 : 5)) {
                    uint8_t old2 = vm_base[0x10200 + a2 + pos];
                    vm_base[0x10200 + a2 + pos] = (uint8_t)(old2 ^ 0x81u);
                    if (vm_base[0x10200 + a2 + pos] == vm_base[0x10000 + a1 + pos])
                        vm_base[0x10200 + a2 + pos]++;
                    uint64_t want = ref_strcmp_u8(vm_base + 0x10000 + a1, vm_base + 0x10200 + a2);
                    ppu_context before = seed_ctx(0x10000 + a1, 0x10200 + a2, 0, salt++);
                    ppu_context got = before;
                    uint64_t fb = g_fb_strcmp;
                    func_0033A320(&got);
                    assert(g_fb_strcmp == fb && got.gpr[3] == want);
                    check_nonvolatile_abi("strcmp", "diff", before, got);
                    vm_base[0x10200 + a2 + pos] = old2;
                    cases++;
                }
            }
        }
    }
    memcpy(vm_base, g_base_vm, VM);
    for (uint8_t b1 : {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF}) {
        for (uint8_t b2 : {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF}) {
            vm_base[0x10000] = b1; vm_base[0x10001] = 0;
            vm_base[0x10200] = b2; vm_base[0x10201] = 0;
            uint64_t want = ref_strcmp_u8(vm_base + 0x10000, vm_base + 0x10200);
            ppu_context got = seed_ctx(0x10000, 0x10200, 0, salt++);
            uint64_t fb = g_fb_strcmp;
            func_0033A320(&got);
            assert(g_fb_strcmp == fb && got.gpr[3] == want);
            cases++;
        }
    }
    {
        ppu_context g1 = seed_ctx(0x800, 0x10000, 0, salt++);
        ppu_context g2 = seed_ctx((1ULL << 32) | 0x10000, 0x10200, 0, salt++);
        uint64_t fb = g_fb_strcmp;
        func_0033A320(&g1); func_0033A320(&g2);
        assert(g_fb_strcmp == fb + 2);
        cases += 2;
    }

    /* ---------------- 4. strcpy (0033A474) ---------------- */
    for (uint32_t len = 0; len <= 64; len++) {
        for (uint32_t sa = 0; sa < 16; sa++) {
            for (uint32_t da = 0; da < 16; da += 3) {
                memcpy(g_want_vm + 0x10000, g_base_vm + 0x10000, 0x400);
                fill_str(g_want_vm, 0x10000 + sa, len, &rng);
                memcpy(vm_base + 0x10000, g_want_vm + 0x10000, 0x400);
                memcpy(g_want_vm + 0x10200 + da, g_want_vm + 0x10000 + sa, len + 1);
                ppu_context before = seed_ctx(0x10200 + da, 0x10000 + sa, 0, salt++);
                ppu_context got = before;
                uint64_t fb = g_fb_strcpy;
                func_0033A474(&got);
                assert(g_fb_strcpy == fb);
                assert(memcmp(g_want_vm + 0x10000, vm_base + 0x10000, 0x400) == 0);
                assert(got.gpr[3] == 0x10200 + da);
                check_nonvolatile_abi("strcpy", "norm", before, got);
                cases++;
            }
        }
    }
    memcpy(vm_base, g_base_vm, VM);
    fill_str(vm_base, 0x10000, 16, &rng);
    for (auto c : std::initializer_list<std::pair<uint64_t, uint64_t>>{
            {0x10000, 0x10000}, {0x10004, 0x10000}, {0x800, 0x10000}, {VM - 8, 0x10000}}) {
        ppu_context got = seed_ctx(c.first, c.second, 0, salt++);
        uint64_t fb = g_fb_strcpy;
        func_0033A474(&got);
        assert(g_fb_strcpy == fb + 1);
        cases++;
    }

    /* ---------------- 5. strncmp (0033A6C4) ---------------- */
    for (uint32_t n = 0; n <= 33; n++) {
        for (uint32_t a1 = 0; a1 < 16; a1 += 3) {
            for (uint32_t a2 = 0; a2 < 16; a2 += 3) {
                memcpy(vm_base + 0x10000, g_base_vm + 0x10000, 0x400);
                uint32_t slen = (n > 0) ? n + 3 : 5;
                fill_str(vm_base, 0x10000 + a1, slen, &rng);
                memcpy(vm_base + 0x10200 + a2, vm_base + 0x10000 + a1, slen + 1);
                {
                    ppu_context before = seed_ctx(0x10000 + a1, 0x10200 + a2, n, salt++);
                    ppu_context got = before;
                    uint64_t fb = g_fb_strncmp;
                    func_0033A6C4(&got);
                    assert(g_fb_strncmp == fb && got.gpr[3] == 0);
                    check_nonvolatile_abi("strncmp", "eq", before, got);
                    cases++;
                }
                if (n > 0) {
                    for (uint32_t pos : {0u, n / 2u, n - 1u, n}) {
                        uint8_t old2 = vm_base[0x10200 + a2 + pos];
                        vm_base[0x10200 + a2 + pos] = (uint8_t)(old2 ^ 0x83u);
                        if (vm_base[0x10200 + a2 + pos] == vm_base[0x10000 + a1 + pos])
                            vm_base[0x10200 + a2 + pos]++;
                        uint64_t want = ref_strncmp_u8(vm_base + 0x10000 + a1, vm_base + 0x10200 + a2, n);
                        ppu_context before = seed_ctx(0x10000 + a1, 0x10200 + a2, n, salt++);
                        ppu_context got = before;
                        uint64_t fb = g_fb_strncmp;
                        func_0033A6C4(&got);
                        assert(g_fb_strncmp == fb && got.gpr[3] == want);
                        check_nonvolatile_abi("strncmp", "diff", before, got);
                        vm_base[0x10200 + a2 + pos] = old2;
                        cases++;
                    }
                    if (n >= 2) {
                        uint32_t cut = n / 2u;
                        vm_base[0x10000 + a1 + cut] = 0;
                        vm_base[0x10200 + a2 + cut] = 0;
                        ppu_context got = seed_ctx(0x10000 + a1, 0x10200 + a2, n, salt++);
                        uint64_t fb = g_fb_strncmp;
                        func_0033A6C4(&got);
                        assert(g_fb_strncmp == fb && got.gpr[3] == 0);
                        cases++;
                    }
                }
            }
        }
    }
    memcpy(vm_base, g_base_vm, VM);
    {
        ppu_context got = seed_ctx(0, 0, 0, salt++);
        uint64_t fb = g_fb_strncmp;
        func_0033A6C4(&got);
        assert(g_fb_strncmp == fb && got.gpr[3] == 0);
        /* Equal 8 non-zero bytes ending cleanly at VM requires speculative load guard fallback */
        memset(vm_base + VM - 8, 0x42, 8);
        memset(vm_base + 0x10000, 0x42, 8);
        got = seed_ctx(VM - 8, 0x10000, 8, salt++);
        func_0033A6C4(&got);
        assert(g_fb_strncmp == fb + 1);
        cases += 2;
    }

    /* ---------------- 6. strncpy (0033A888) ---------------- */
    for (uint32_t n = 0; n <= 26; n++) {
        for (uint32_t slen : {0u, 1u, 3u, 4u, 7u, 8u, 15u, 16u, 24u, 30u}) {
            if (slen > n + 8) continue;
            for (uint32_t sa = 0; sa < 8; sa += 2) {
                for (uint32_t da = 0; da < 8; da += 2) {
                    memcpy(g_want_vm + 0x10000, g_base_vm + 0x10000, 0x400);
                    fill_str(g_want_vm, 0x10000 + sa, slen, &rng);
                    memcpy(vm_base + 0x10000, g_want_vm + 0x10000, 0x400);
                    strncpy((char*)(g_want_vm + 0x10200 + da),
                            (const char*)(g_want_vm + 0x10000 + sa), n);
                    ppu_context before = seed_ctx(0x10200 + da, 0x10000 + sa, n, salt++);
                    ppu_context got = before;
                    uint64_t fb = g_fb_strncpy;
                    func_0033A888(&got);
                    assert(g_fb_strncpy == fb);
                    assert(memcmp(g_want_vm + 0x10000, vm_base + 0x10000, 0x400) == 0);
                    assert(got.gpr[3] == 0x10200 + da);
                    check_nonvolatile_abi("strncpy", "norm", before, got);
                    cases++;
                }
            }
        }
    }
    memcpy(vm_base, g_base_vm, VM);
    {
        ppu_context got = seed_ctx(0, 0, 0, salt++);
        uint64_t fb = g_fb_strncpy;
        func_0033A888(&got);
        assert(g_fb_strncpy == fb && got.gpr[3] == 0);
        got = seed_ctx(VM - 8, 0x10000, 16, salt++);
        func_0033A888(&got);
        assert(g_fb_strncpy == fb + 1);
        got = seed_ctx(0x10002, 0x10000, 16, salt++);
        func_0033A888(&got);
        assert(g_fb_strncpy == fb + 2);
        cases += 3;
    }

    printf("PASS: %llu asset-free d2_crt checks passed (memmove, strlen, strcmp, strcpy, strncmp, strncpy)\n",
           (unsigned long long)cases);
    free(vm_base);
    return 0;
}
