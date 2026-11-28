/* BLUS31313 1.40 native guest CRT overrides (ppu_recomp_018.cpp):
 *   - func_003365A8: memmove (overlap-safe via vm_copy / memmove)
 *   - func_00338980: strlen
 *   - func_0033A320: strcmp
 *   - func_0033A474: strcpy
 *   - func_0033A6C4: strncmp
 *   - func_0033A888: strncpy
 *
 * All fast paths operate only on validated non-null, non-GCM, non-MMIO, in-bounds
 * 32-bit guest ranges when read/write diagnostics are inactive, falling back to
 * d2_original_<addr>(ctx) otherwise. Return register r3 and callee-saved state
 * match 100%; string functions also reproduce the exact volatile GPR/CR/CTR state
 * of their lifted counterparts. */
#include "ppu_recomp.h"
#include "guest_poll.h"
#include <algorithm>
#include <cstring>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#if !defined(D2_GAME_VERSION) || D2_GAME_VERSION == 140

extern "C" {
extern uint8_t* vm_base;
extern uint32_t ppu_vm_size;
extern uint32_t ppu_hle_inject_base;
extern int      g_vm_read_slow_active;
extern int      g_vm_write_slow_active;
extern uint32_t g_barrier_sync_watch;
extern uint32_t g_null_sweep_hi;
extern int      g_spu_coh_armed;

int  vm_copy(uint32_t dst, uint32_t src, uint32_t n);
int  vm_fill(uint32_t a, uint8_t v, uint32_t n);
int  spu_coh_is_reserved(uint32_t addr);
void spu_lockline_lock(void);
void spu_lockline_unlock(void);
void spu_coh_notify_write(uint32_t ea);
}

void d2_original_003365A8(ppu_context*);
void d2_original_00338980(ppu_context*);
void d2_original_0033A320(ppu_context*);
void d2_original_0033A474(ppu_context*);
void d2_original_0033A6C4(ppu_context*);
void d2_original_0033A888(ppu_context*);

static inline void set_cr(ppu_context* ctx, int shift, uint32_t val)
{
    ctx->cr = (ctx->cr & ~(0xFu << shift)) | (val << shift);
}

static inline uint32_t cmpu64(uint64_t a, uint64_t b)
{
    return a < b ? 8u : a > b ? 4u : 2u;
}

static inline uint32_t cmps64(int64_t a, int64_t b)
{
    return a < b ? 8u : a > b ? 4u : 2u;
}

static inline uint32_t bswap32(uint32_t v)
{
#if defined(_MSC_VER) && !defined(__clang__)
    return _byteswap_ulong(v);
#else
    return __builtin_bswap32(v);
#endif
}

static inline uint64_t bswap64(uint64_t v)
{
#if defined(_MSC_VER) && !defined(__clang__)
    return _byteswap_uint64(v);
#else
    return __builtin_bswap64(v);
#endif
}

static inline uint32_t be32(uint32_t ea)
{
    uint32_t v;
    std::memcpy(&v, vm_base + ea, sizeof v);
    return bswap32(v);
}

static inline uint64_t be64(uint32_t ea)
{
    uint64_t v;
    std::memcpy(&v, vm_base + ea, sizeof v);
    return bswap64(v);
}

static inline int clz32(uint32_t x)
{
    if (!x) return 32;
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long idx;
    _BitScanReverse(&idx, x);
    return 31 - (int)idx;
#else
    return __builtin_clz(x);
#endif
}

static inline int clz64(uint64_t x)
{
    if (!x) return 64;
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return 63 - (int)idx;
#else
    return __builtin_clzll(x);
#endif
}

static inline uint64_t zero_mask64(uint64_t w, uint64_t* r_and, uint64_t* r_or)
{
    const uint64_t m = 0x7F7F7F7F7F7F7F7FULL;
    const uint64_t a = (w & m) + m;
    const uint64_t o = w | m;
    if (r_and) *r_and = a;
    if (r_or) *r_or = o;
    return ~(a | o);
}

static inline uint64_t zero_mask32(uint32_t w, uint32_t* r_and, uint64_t* r_or)
{
    const uint32_t m = 0x7F7F7F7Fu;
    const uint32_t a = (w & m) + m;
    const uint64_t o = (uint64_t)w | m;
    if (r_and) *r_and = a;
    if (r_or) *r_or = o;
    return ~((uint64_t)a | o);
}

/* Exclusive upper bound for fast guest memory accesses starting at `ea`,
 * stopping before SPU raw MMIO (0xE0000000), ppu_vm_size, and the 32 KiB
 * GCM HLE window. Returns 0 if `ea` is below 0x10000 or inside an excluded span. */
static inline uint32_t safe_guest_end(uint32_t ea)
{
    const uint32_t spu_raw = 0xE0000000u;
    uint32_t lim = (ppu_vm_size && ppu_vm_size < spu_raw) ? ppu_vm_size : spu_raw;
    const uint32_t gcm = ppu_hle_inject_base;
    if (ea < 0x10000u || ea >= lim) return 0;
    if (ea >= gcm && ea < gcm + 0x8000u) return 0;
    if (ea < gcm && lim > gcm) lim = gcm;
    return lim;
}

static inline bool safe_guest_span(uint64_t ea, uint64_t n)
{
    if ((ea | n) >> 32) return false;
    const uint64_t end = ea + n;
    if (end >> 32) return false;
    const uint32_t lim = safe_guest_end((uint32_t)ea);
    return lim != 0 && end <= lim;
}

static inline bool ranges_overlap(uint32_t a, uint32_t b, uint32_t n)
{
    const uint64_t a_end = (uint64_t)a + n;
    const uint64_t b_end = (uint64_t)b + n;
    return (uint64_t)a < b_end && (uint64_t)b < a_end;
}

static void memmove_commit_overlap(uint32_t dst, uint32_t src, uint32_t n)
{
    if (!g_spu_coh_armed) {
        std::memmove(vm_base + dst, vm_base + src, n);
        ps3_poll_notify(dst, n);
        return;
    }
    const uint32_t first = dst & ~127u;
    const uint32_t last = (uint32_t)((uint64_t)dst + n - 1u) & ~127u;
    bool reserved = false;
    for (uint32_t line = first; !reserved; line += 128u) {
        reserved = spu_coh_is_reserved(line) != 0;
        if (line == last) break;
    }
    if (reserved) spu_lockline_lock();
    std::memmove(vm_base + dst, vm_base + src, n);
    if (reserved) {
        for (uint32_t line = first;; line += 128u) {
            if (spu_coh_is_reserved(line)) spu_coh_notify_write(line);
            if (line == last) break;
        }
        spu_lockline_unlock();
    }
    ps3_poll_notify(dst, n);
}

/* -------------------------------------------------------------------------
 * func_003365A8: memmove(r3 dst, r4 src, r5 n) -> r3 dst
 * ------------------------------------------------------------------------- */
void func_003365A8(ppu_context* ctx)
{
    const uint64_t dst = ctx->gpr[3];
    const uint64_t src = ctx->gpr[4];
    const uint64_t n = ctx->gpr[5];
    if ((dst | src | n) >> 32 ||
        !safe_guest_span(dst, n) || !safe_guest_span(src, n) ||
        g_vm_read_slow_active || g_vm_write_slow_active ||
        g_barrier_sync_watch || g_null_sweep_hi) {
        d2_original_003365A8(ctx);
        return;
    }
    const uint32_t dst32 = (uint32_t)dst;
    const uint32_t src32 = (uint32_t)src;
    const uint32_t n32 = (uint32_t)n;
    if (n32 != 0 && dst32 != src32) {
        if (!ranges_overlap(dst32, src32, n32)) {
            if (!vm_copy(dst32, src32, n32)) {
                d2_original_003365A8(ctx);
                return;
            }
        } else {
            memmove_commit_overlap(dst32, src32, n32);
        }
    }
    ctx->gpr[3] = dst32;
}

/* -------------------------------------------------------------------------
 * func_00338980: strlen(r3 s) -> r3 len
 * ------------------------------------------------------------------------- */
void func_00338980(ppu_context* ctx)
{
    const uint64_t s = ctx->gpr[3];
    if (g_vm_read_slow_active || (s >> 32)) {
        d2_original_00338980(ctx);
        return;
    }
    const uint32_t s32 = (uint32_t)s;
    const uint32_t lim = safe_guest_end(s32);
    if (!lim) {
        d2_original_00338980(ctx);
        return;
    }
    const uint8_t* p = vm_base + s32;
    const void* nul = std::memchr(p, 0, lim - s32);
    if (!nul) {
        d2_original_00338980(ctx);
        return;
    }
    const uint64_t len = (uint64_t)((const uint8_t*)nul - p);
    const uint32_t pre = (8u - (s32 & 7u)) & 7u;

    ctx->gpr[5] = 0;
    ctx->gpr[3] = len;
    ctx->gpr[4] = (uint64_t)s32 + len;

    if (pre != 0 && len < pre) {
        ctx->gpr[0] = 0;
        set_cr(ctx, 28, 2u);
        ctx->ctr = pre - len;
        return;
    }

    const uint32_t aligned = s32 + pre;
    const uint64_t tail = len - pre;
    const uint64_t q = tail >> 3;
    const uint32_t word_ea = aligned + (uint32_t)(q * 8ULL);
    const uint64_t word = be64(word_ea);
    uint64_t r10 = 0, r9 = 0;
    const uint64_t r11 = zero_mask64(word, &r10, &r9);
    const uint64_t r0 = (uint64_t)clz64(r11) >> 3;

    ctx->gpr[6] = 0x7F7F7F7F7F7F7F7FULL;
    ctx->gpr[7] = word & 0x7F7F7F7F7F7F7F7FULL;
    ctx->gpr[8] = 0x10;
    ctx->gpr[9] = r9;
    ctx->gpr[10] = r10;
    ctx->gpr[11] = r11;
    ctx->gpr[0] = r0;
    set_cr(ctx, 28, cmpu64((uint32_t)r0, 8u));
    ctx->ctr = 16u - (q & 15u);
}

/* -------------------------------------------------------------------------
 * func_0033A320: strcmp(r3 s1, r4 s2) -> r3 (-1 / 0 / 1)
 * ------------------------------------------------------------------------- */
static void strcmp_tail_bytes(ppu_context* ctx, uint32_t s1, uint32_t s2,
                              uint32_t off, uint64_t end_idx, bool equal)
{
    const uint64_t rem = end_idx - off;
    const uint8_t b1 = vm_base[s1 + (uint32_t)end_idx];
    const uint8_t b2 = vm_base[s2 + (uint32_t)end_idx];
    ctx->gpr[0] = b1;
    ctx->gpr[9] = b2;
    ctx->gpr[4] = (uint64_t)s2 + end_idx;
    if (rem >= 1) {
        set_cr(ctx, 4, cmps64((int32_t)b1, 0));
    }
    if (equal) {
        set_cr(ctx, 0, 2u);
        ctx->gpr[3] = 0;
        return;
    }
    set_cr(ctx, 0, cmpu64(b1, b2));
    ctx->gpr[3] = (b1 < b2) ? (uint64_t)(int64_t)-1 : 1ULL;
}

void func_0033A320(ppu_context* ctx)
{
    const uint64_t s1 = ctx->gpr[3];
    const uint64_t s2 = ctx->gpr[4];
    if (g_vm_read_slow_active || ((s1 | s2) >> 32)) {
        d2_original_0033A320(ctx);
        return;
    }
    const uint32_t s1_32 = (uint32_t)s1;
    const uint32_t s2_32 = (uint32_t)s2;
    const uint32_t lim1 = safe_guest_end(s1_32);
    const uint32_t lim2 = safe_guest_end(s2_32);
    if (!lim1 || !lim2) {
        d2_original_0033A320(ctx);
        return;
    }
    const uint32_t max_len = std::min(lim1 - s1_32, lim2 - s2_32);
    const uint8_t* p1 = vm_base + s1_32;
    const uint8_t* p2 = vm_base + s2_32;

    const void* nul1 = std::memchr(p1, 0, max_len);
    const uint32_t cmp_len = nul1 ? (uint32_t)((const uint8_t*)nul1 - p1) + 1u : max_len;
    const int mem_cmp = std::memcmp(p1, p2, cmp_len);
    if (!nul1 && mem_cmp == 0) {
        d2_original_0033A320(ctx);
        return;
    }

    const bool equal = (mem_cmp == 0);
    uint64_t end_idx = cmp_len - 1u;
    if (!equal) {
        uint32_t i = 0;
        while (i < cmp_len && p1[i] == p2[i]) i++;
        end_idx = i;
    }

    const uint32_t align = s1_32 | s2_32;
    if ((align & 7u) == 0) {
        const uint32_t word_off = (uint32_t)(end_idx & ~7ULL);
        const uint64_t w1 = be64(s1_32 + word_off);
        const uint64_t w2 = be64(s2_32 + word_off);
        if (word_off == 0) {
            if (w1 != w2) {
                ctx->gpr[11] = align;
                ctx->gpr[0] = w1;
                ctx->gpr[9] = w2;
                set_cr(ctx, 0, cmps64((int64_t)w2, (int64_t)w1));
                strcmp_tail_bytes(ctx, s1_32, s2_32, 0, end_idx, equal);
                return;
            }
            uint64_t r0 = 0, r9 = 0;
            const uint64_t r10 = zero_mask64(w2, &r0, &r9);
            ctx->gpr[11] = 0x7F7F7F7F7F7F7F7FULL;
            ctx->gpr[0] = r0;
            ctx->gpr[9] = r9;
            ctx->gpr[10] = r10;
            set_cr(ctx, 28, cmps64((int64_t)r10, 0));
            set_cr(ctx, 0, 2u);
            ctx->gpr[3] = 0;
            return;
        }
        uint64_t r9 = 0, r10 = 0;
        const uint64_t r0 = zero_mask64(w2, &r9, &r10);
        ctx->gpr[8] = 0x7F7F7F7F7F7F7F7FULL;
        ctx->gpr[11] = w1;
        ctx->gpr[0] = r0;
        ctx->gpr[9] = r9;
        ctx->gpr[10] = r10;
        set_cr(ctx, 0, cmps64((int64_t)w2, (int64_t)w1));
        set_cr(ctx, 28, cmps64((int64_t)r0, 0));
        if (w1 == w2) {
            ctx->gpr[4] = (uint64_t)s2_32 + word_off;
            ctx->gpr[3] = 0;
            return;
        }
        strcmp_tail_bytes(ctx, s1_32, s2_32, word_off, end_idx, equal);
        return;
    }

    if ((align & 3u) == 0) {
        const uint32_t word_off = (uint32_t)(end_idx & ~3ULL);
        const uint32_t w1 = be32(s1_32 + word_off);
        const uint32_t w2 = be32(s2_32 + word_off);
        if (word_off == 0) {
            if (w1 != w2) {
                ctx->gpr[11] = align;
                ctx->gpr[0] = w1;
                ctx->gpr[9] = w2;
                set_cr(ctx, 0, cmps64((int32_t)w2, (int32_t)w1));
                strcmp_tail_bytes(ctx, s1_32, s2_32, 0, end_idx, equal);
                return;
            }
            uint32_t r9 = 0;
            uint64_t r11 = 0;
            const uint64_t r9_fin = zero_mask32(w2, &r9, &r11);
            ctx->gpr[0] = 0x7F7F7F7FULL;
            ctx->gpr[8] = 0x7F7F7F7FULL;
            ctx->gpr[11] = r11;
            ctx->gpr[9] = r9_fin;
            set_cr(ctx, 0, cmps64((int32_t)r9_fin, 0));
            ctx->gpr[3] = 0;
            return;
        }
        uint32_t r9 = 0;
        uint64_t unused_or = 0;
        const uint64_t r0 = zero_mask32(w2, &r9, &unused_or);
        ctx->gpr[8] = 0x7F7F7F7FULL;
        ctx->gpr[11] = w2;
        ctx->gpr[10] = w1;
        ctx->gpr[9] = r9;
        ctx->gpr[0] = r0;
        set_cr(ctx, 0, cmps64((int32_t)w2, (int32_t)w1));
        set_cr(ctx, 4, cmps64((int32_t)r0, 0));
        if (w1 == w2) {
            ctx->gpr[4] = (uint64_t)s2_32 + word_off;
            ctx->gpr[3] = 0;
            return;
        }
        strcmp_tail_bytes(ctx, s1_32, s2_32, word_off, end_idx, equal);
        return;
    }

    ctx->gpr[11] = align;
    strcmp_tail_bytes(ctx, s1_32, s2_32, 0, end_idx, equal);
}

/* -------------------------------------------------------------------------
 * func_0033A474: strcpy(r3 dst, r4 src) -> r3 dst
 * ------------------------------------------------------------------------- */
void func_0033A474(ppu_context* ctx)
{
    const uint64_t dst = ctx->gpr[3];
    const uint64_t src = ctx->gpr[4];
    if (g_vm_read_slow_active || ((dst | src) >> 32)) {
        d2_original_0033A474(ctx);
        return;
    }
    const uint32_t dst32 = (uint32_t)dst;
    const uint32_t src32 = (uint32_t)src;
    const uint32_t lim = safe_guest_end(src32);
    if (!lim) {
        d2_original_0033A474(ctx);
        return;
    }
    const void* nul = std::memchr(vm_base + src32, 0, lim - src32);
    if (!nul) {
        d2_original_0033A474(ctx);
        return;
    }
    const uint64_t len = (uint64_t)((const uint8_t*)nul - (vm_base + src32));
    const uint64_t n = len + 1ULL;
    if ((dst + n) >> 32 || ranges_overlap(dst32, src32, (uint32_t)n) ||
        !vm_copy(dst32, src32, (uint32_t)n)) {
        d2_original_0033A474(ctx);
        return;
    }

    const uint32_t align = dst32 | src32;
    ctx->gpr[6] = dst32;
    ctx->gpr[3] = dst32;
    ctx->gpr[4] = (uint64_t)src32 + n;
    ctx->gpr[0] = 0;
    set_cr(ctx, 0, 2u);

    if ((align & 7u) == 0) {
        const uint32_t word_off = (uint32_t)(len & ~7ULL);
        const uint64_t w = be64(src32 + word_off);
        uint64_t r0 = 0, r9 = 0;
        const uint64_t r8 = zero_mask64(w, &r0, &r9);
        ctx->gpr[10] = 0x7F7F7F7F7F7F7F7FULL;
        ctx->gpr[11] = w;
        ctx->gpr[9] = r9;
        ctx->gpr[8] = r8;
        set_cr(ctx, 28, cmps64((int64_t)r8, 0));
    } else if ((align & 3u) == 0) {
        const uint32_t word_off = (uint32_t)(len & ~3ULL);
        const uint32_t w = be32(src32 + word_off);
        uint32_t r9_and = 0;
        uint64_t unused_or = 0;
        const uint64_t r9 = zero_mask32(w, &r9_and, &unused_or);
        ctx->gpr[10] = 0x7F7F7F7FULL;
        ctx->gpr[11] = w;
        ctx->gpr[9] = r9;
    } else {
        ctx->gpr[11] = align;
    }
}

/* -------------------------------------------------------------------------
 * func_0033A6C4: strncmp(r3 s1, r4 s2, r5 n) -> r3 (-1 / 0 / 1)
 * ------------------------------------------------------------------------- */
static void strncmp_byte_loop(ppu_context* ctx, uint32_t s1, uint32_t s2,
                              uint32_t off, uint32_t rem_n, uint64_t end_idx, bool equal)
{
    ctx->gpr[5] = rem_n;
    const uint8_t b1 = vm_base[s1 + (uint32_t)end_idx];
    const uint8_t b2 = vm_base[s2 + (uint32_t)end_idx];
    const uint32_t step = (uint32_t)(end_idx - off);
    ctx->gpr[0] = b1;
    ctx->gpr[9] = b2;
    ctx->gpr[4] = (uint64_t)s2 + end_idx;

    if (step == 0) {
        if (!equal) {
            set_cr(ctx, 0, cmpu64(b1, b2));
            ctx->gpr[3] = (b1 < b2) ? (uint64_t)(int64_t)-1 : 1ULL;
            return;
        }
        ctx->ctr = rem_n;
        set_cr(ctx, 0, cmps64((int32_t)b1, 0));
        if (b1 == 0 || rem_n == 1) {
            if (rem_n == 1 && b1 != 0) ctx->ctr = 0;
            ctx->gpr[3] = 0;
            return;
        }
    } else {
        set_cr(ctx, 4, cmps64((int32_t)b1, 0));
        if (!equal) {
            ctx->ctr = rem_n - step;
            set_cr(ctx, 0, cmpu64(b1, b2));
            ctx->gpr[3] = (b1 < b2) ? (uint64_t)(int64_t)-1 : 1ULL;
            return;
        }
        ctx->ctr = rem_n - step;
        if (step == rem_n - 1u && b1 != 0) {
            set_cr(ctx, 0, 2u);
            ctx->ctr = 0;
            ctx->gpr[3] = 0;
            return;
        }
        set_cr(ctx, 0, 2u);
        ctx->gpr[3] = 0;
    }
}

void func_0033A6C4(ppu_context* ctx)
{
    const uint64_t s1 = ctx->gpr[3];
    const uint64_t s2 = ctx->gpr[4];
    const uint64_t n = ctx->gpr[5];
    if (g_vm_read_slow_active || ((s1 | s2 | n) >> 32)) {
        d2_original_0033A6C4(ctx);
        return;
    }
    const uint32_t s1_32 = (uint32_t)s1;
    const uint32_t s2_32 = (uint32_t)s2;
    const uint32_t n32 = (uint32_t)n;
    if (n32 == 0) {
        set_cr(ctx, 0, 2u);
        ctx->gpr[11] = s1_32;
        ctx->gpr[10] = s2_32;
        ctx->gpr[7] = s2_32;
        ctx->gpr[3] = 0;
        return;
    }

    const uint32_t lim1 = safe_guest_end(s1_32);
    const uint32_t lim2 = safe_guest_end(s2_32);
    if (!lim1 || !lim2) {
        d2_original_0033A6C4(ctx);
        return;
    }
    const uint32_t safe_len = std::min(lim1 - s1_32, lim2 - s2_32);
    const uint32_t probe = std::min(n32, safe_len);
    const uint8_t* p1 = vm_base + s1_32;
    const uint8_t* p2 = vm_base + s2_32;
    const void* nul1 = std::memchr(p1, 0, probe);
    const uint32_t cmp_len = nul1 ? (uint32_t)((const uint8_t*)nul1 - p1) + 1u : probe;
    const int mem_cmp = std::memcmp(p1, p2, cmp_len);

    if (safe_len < n32 && !nul1 && mem_cmp == 0) {
        d2_original_0033A6C4(ctx);
        return;
    }

    const bool equal = (mem_cmp == 0);
    uint32_t end_idx = cmp_len - 1u;
    if (!equal) {
        uint32_t i = 0;
        while (i < cmp_len && p1[i] == p2[i]) i++;
        end_idx = i;
    }

    const uint32_t align = s1_32 | s2_32;
    if (n32 > 7u && (align & 7u) == 0) {
        const uint32_t q = n32 >> 3;
        const uint32_t rem_n = n32 & 7u;
        const uint32_t word_idx = (equal && !nul1) ? q : (end_idx >> 3);
        /* When words 0..q-1 match with no NUL, loc_0033A74C speculatively loads q*8..q*8+7. */
        if (word_idx >= q && ((uint64_t)q * 8ULL + 8ULL > safe_len)) {
            d2_original_0033A6C4(ctx);
            return;
        }
        ctx->gpr[7] = s2_32;
        ctx->gpr[8] = align;
        ctx->gpr[6] = q;
        ctx->gpr[5] = rem_n;

        const uint32_t word_off = (word_idx < q) ? (word_idx * 8u) : 0u;
        const uint64_t w1 = be64(s1_32 + word_off);
        const uint64_t w2 = be64(s2_32 + word_off);
        if (word_idx == 0) {
            if (w1 != w2) {
                ctx->gpr[11] = s1_32;
                ctx->gpr[10] = s2_32;
                ctx->gpr[9] = w1;
                ctx->gpr[0] = w2;
                strncmp_byte_loop(ctx, s1_32, s2_32, 0, 8u, end_idx, equal);
                return;
            }
            if (equal) {
                uint64_t r0 = 0, r9 = 0;
                const uint64_t r10 = zero_mask64(w1, &r0, &r9);
                ctx->gpr[11] = 0x7F7F7F7F7F7F7F7FULL;
                ctx->gpr[0] = r0;
                ctx->gpr[9] = r9;
                ctx->gpr[10] = r10;
                set_cr(ctx, 28, cmps64((int64_t)r10, 0));
                set_cr(ctx, 0, 2u);
                ctx->gpr[3] = 0;
                return;
            }
        }
        const uint32_t loop_idx = (word_idx < q) ? word_idx : q;
        const uint32_t loop_off = loop_idx * 8u;
        const uint64_t lw1 = be64(s1_32 + loop_off);
        const uint64_t lw2 = be64(s2_32 + loop_off);
        uint64_t r9 = 0, r10 = 0;
        const uint64_t r0 = zero_mask64(lw2, &r9, &r10);
        ctx->gpr[8] = 0x7F7F7F7F7F7F7F7FULL;
        ctx->gpr[11] = lw1;
        ctx->gpr[0] = r0;
        ctx->gpr[9] = r9;
        ctx->gpr[10] = r10;
        ctx->ctr = q - loop_idx;
        set_cr(ctx, 28, cmps64((int64_t)r0, 0));
        if (loop_idx < q) {
            if (lw1 != lw2) {
                strncmp_byte_loop(ctx, s1_32, s2_32, loop_off, 8u, end_idx, equal);
                return;
            }
            set_cr(ctx, 0, 2u);
            ctx->gpr[4] = (uint64_t)s2_32 + loop_off;
            ctx->gpr[3] = 0;
            return;
        }
        set_cr(ctx, 0, cmps64((int64_t)lw1, (int64_t)lw2));
        if (rem_n == 0) {
            set_cr(ctx, 0, 2u);
            ctx->gpr[4] = (uint64_t)s2_32 + loop_off;
            ctx->gpr[3] = 0;
            return;
        }
        strncmp_byte_loop(ctx, s1_32, s2_32, loop_off, rem_n, end_idx, equal);
        return;
    }

    if (n32 > 7u && (align & 3u) == 0) {
        const uint32_t q = n32 >> 2;
        const uint32_t rem_n = n32 & 3u;
        const uint32_t word_idx = (equal && !nul1) ? q : (end_idx >> 2);
        if (word_idx >= q && ((uint64_t)q * 4ULL + 4ULL > safe_len)) {
            d2_original_0033A6C4(ctx);
            return;
        }
        ctx->gpr[7] = s2_32;
        ctx->gpr[8] = align;
        ctx->gpr[10] = q;
        ctx->gpr[5] = rem_n;

        const uint32_t word_off = (word_idx < q) ? (word_idx * 4u) : 0u;
        const uint32_t w1 = be32(s1_32 + word_off);
        const uint32_t w2 = be32(s2_32 + word_off);
        if (word_idx == 0) {
            if (w1 != w2) {
                ctx->gpr[11] = s1_32;
                ctx->gpr[9] = w1;
                ctx->gpr[0] = w2;
                strncmp_byte_loop(ctx, s1_32, s2_32, 0, 4u, end_idx, equal);
                return;
            }
            if (equal) {
                uint32_t r9 = 0;
                uint64_t r11 = 0;
                const uint64_t r9_fin = zero_mask32(w1, &r9, &r11);
                ctx->gpr[0] = 0x7F7F7F7FULL;
                ctx->gpr[8] = 0x7F7F7F7FULL;
                ctx->gpr[11] = r11;
                ctx->gpr[9] = r9_fin;
                ctx->ctr = q;
                set_cr(ctx, 0, cmps64((int32_t)r9_fin, 0));
                ctx->gpr[3] = 0;
                return;
            }
        }
        const uint32_t loop_idx = (word_idx < q) ? word_idx : q;
        const uint32_t loop_off = loop_idx * 4u;
        const uint32_t lw1 = be32(s1_32 + loop_off);
        const uint32_t lw2 = be32(s2_32 + loop_off);
        uint32_t r9_and = 0;
        uint64_t r11 = 0;
        const uint64_t r9_fin = zero_mask32(lw2, &r9_and, &r11);
        ctx->gpr[8] = 0x7F7F7F7FULL;
        ctx->gpr[0] = lw2;
        ctx->gpr[10] = lw1;
        ctx->gpr[11] = r11;
        ctx->gpr[9] = r9_fin;
        ctx->ctr = q - loop_idx;
        set_cr(ctx, 4, cmps64((int32_t)lw1, (int32_t)lw2));
        if (loop_idx < q) {
            if (lw1 != lw2) {
                set_cr(ctx, 0, cmps64((int32_t)r9_fin, 0));
                strncmp_byte_loop(ctx, s1_32, s2_32, loop_off, 4u, end_idx, equal);
                return;
            }
            set_cr(ctx, 0, cmps64((int32_t)r9_fin, 0));
            ctx->gpr[4] = (uint64_t)s2_32 + loop_off;
            ctx->gpr[3] = 0;
            return;
        }
        set_cr(ctx, 0, 2u);
        if (rem_n == 0) {
            ctx->gpr[4] = (uint64_t)s2_32 + loop_off;
            ctx->gpr[3] = 0;
            return;
        }
        strncmp_byte_loop(ctx, s1_32, s2_32, loop_off, rem_n, end_idx, equal);
        return;
    }

    ctx->gpr[11] = s1_32;
    ctx->gpr[10] = s2_32;
    ctx->gpr[7] = s2_32;
    if (n32 > 7u) {
        ctx->gpr[8] = align;
        ctx->gpr[0] = align & 3u;
    }
    strncmp_byte_loop(ctx, s1_32, s2_32, 0, n32, end_idx, equal);
}

/* -------------------------------------------------------------------------
 * func_0033A888: strncpy(r3 dst, r4 src, r5 n) -> r3 dst
 * ------------------------------------------------------------------------- */
void func_0033A888(ppu_context* ctx)
{
    const uint64_t dst = ctx->gpr[3];
    const uint64_t src = ctx->gpr[4];
    const uint64_t n = ctx->gpr[5];
    if (g_vm_read_slow_active || g_vm_write_slow_active ||
        g_barrier_sync_watch || g_null_sweep_hi ||
        ((dst | src | n) >> 32)) {
        d2_original_0033A888(ctx);
        return;
    }
    const uint32_t dst32 = (uint32_t)dst;
    const uint32_t src32 = (uint32_t)src;
    const uint32_t n32 = (uint32_t)n;
    const uint32_t align = dst32 | src32;

    if (n32 == 0) {
        ctx->gpr[9] = align;
        ctx->gpr[7] = src32;
        ctx->gpr[6] = dst32;
        ctx->gpr[5] = 0;
        ctx->gpr[3] = dst32;
        if ((align & 7u) == 0 || (align & 3u) == 0) {
            ctx->gpr[8] = 0;
        }
        ctx->gpr[0] = ((align & 7u) == 0) ? 0ULL : (uint64_t)(align & 3u);
        set_cr(ctx, 0, 2u);
        return;
    }

    const uint32_t lim_src = safe_guest_end(src32);
    if (!lim_src || !safe_guest_span(dst32, n32)) {
        d2_original_0033A888(ctx);
        return;
    }
    const uint32_t src_avail = std::min(n32, lim_src - src32);
    const void* nul = std::memchr(vm_base + src32, 0, src_avail);
    if (!nul && src_avail < n32) {
        d2_original_0033A888(ctx);
        return;
    }
    const uint32_t src_len = nul ? (uint32_t)((const uint8_t*)nul - (vm_base + src32)) : n32;
    const uint32_t copy_len = nul ? (src_len + 1u) : n32;
    const uint32_t pad_len = n32 - copy_len;

    if ((uint64_t)dst32 < (uint64_t)src32 + copy_len &&
        (uint64_t)src32 < (uint64_t)dst32 + n32) {
        d2_original_0033A888(ctx);
        return;
    }
    if (!vm_copy(dst32, src32, copy_len)) {
        d2_original_0033A888(ctx);
        return;
    }
    if (pad_len > 0) {
        (void)vm_fill(dst32 + copy_len, 0, pad_len);
    }

    ctx->gpr[6] = dst32;
    ctx->gpr[7] = src32;
    ctx->gpr[3] = dst32;
    ctx->gpr[9] = align;

    uint32_t off = 0;
    uint32_t rem_n = n32;
    bool padded_in_word = false;

    if ((align & 7u) == 0) {
        const uint32_t q = n32 >> 3;
        rem_n = n32 & 7u;
        ctx->gpr[8] = q;
        ctx->gpr[5] = rem_n;
        if (q > 0) {
            const uint32_t word_idx = std::min(src_len >> 3, q);
            if (word_idx < q) {
                padded_in_word = true;
                const uint32_t w_ea = dst32 + word_idx * 8u;
                const uint64_t w_src = be64(src32 + word_idx * 8u);
                uint64_t r0_and = 0, r9_or = 0;
                const uint64_t mask = zero_mask64(w_src, &r0_and, &r9_or);
                const uint32_t zpos = (uint32_t)clz64(mask);
                const uint64_t keep_mask = ~((zpos & 64u) ? 0ULL : (~0ULL >> (zpos & 63u)));
                const uint64_t tail_w = w_src & keep_mask;
                const uint32_t tail_words = q - word_idx - 1u;
                ctx->gpr[8] = q - word_idx;
                ctx->gpr[4] = (uint64_t)src32 + word_idx * 8u;
                ctx->gpr[0] = zpos;
                ctx->gpr[11] = tail_words;
                ctx->gpr[10] = (uint64_t)w_ea + 8ULL;
                ctx->gpr[9] = tail_w;
                ctx->ctr = q - word_idx;
                if (tail_words > 0) {
                    ctx->gpr[0] = 0;
                    ctx->ctr = 0;
                    ctx->gpr[11] = (uint64_t)w_ea + 8ULL + (uint64_t)tail_words * 8ULL;
                    ctx->gpr[9] = ctx->gpr[11] + 8ULL;
                    ctx->gpr[10] = ctx->gpr[11];
                }
                if (rem_n > 0) {
                    ctx->gpr[0] = 0;
                    ctx->ctr = 0;
                    ctx->gpr[11] = (uint64_t)dst32 + n32;
                    ctx->gpr[9] = (uint64_t)dst32 + n32 - 1ULL;
                }
                set_cr(ctx, 28, cmps64((int64_t)mask, 0));
                set_cr(ctx, 0, cmps64((int32_t)rem_n, 0));
                return;
            }
            off = q * 8u;
            const uint32_t last_w = (q - 1u) * 8u;
            const uint64_t w = be64(dst32 + last_w);
            uint64_t r0_and = 0, r9_or = 0;
            (void)zero_mask64(w, &r0_and, &r9_or);
            ctx->gpr[8] = 0;
            ctx->gpr[10] = w;
            ctx->gpr[11] = 0x7F7F7F7F7F7F7F7FULL;
            ctx->gpr[9] = (q == 1u) ? 0x7F7F7F7F7F7F7F7FULL : r9_or;
            ctx->gpr[0] = ~(r0_and | r9_or);
            ctx->gpr[4] = (uint64_t)src32 + off;
            ctx->ctr = 0;
            set_cr(ctx, 28, 2u);
        }
    } else if ((align & 3u) == 0) {
        const uint32_t q = n32 >> 2;
        rem_n = n32 & 3u;
        ctx->gpr[8] = q;
        ctx->gpr[5] = rem_n;
        if (q > 0) {
            const uint32_t word_idx = std::min(src_len >> 2, q);
            if (word_idx < q) {
                padded_in_word = true;
                const uint32_t w_ea = dst32 + word_idx * 4u;
                const uint32_t w_src = be32(src32 + word_idx * 4u);
                uint32_t r9_and = 0;
                uint64_t r0_or = 0;
                const uint64_t mask = zero_mask32(w_src, &r9_and, &r0_or);
                const uint32_t zpos = (uint32_t)clz32((uint32_t)mask);
                const uint32_t keep_mask = ~((zpos & 32u) ? 0u : (0xFFFFFFFFu >> (zpos & 31u)));
                const uint32_t tail_w = w_src & keep_mask;
                const uint32_t tail_words = q - word_idx - 1u;
                ctx->gpr[8] = q - word_idx;
                ctx->gpr[4] = (uint64_t)src32 + word_idx * 4u;
                ctx->gpr[0] = zpos;
                ctx->gpr[7] = tail_words;
                ctx->gpr[11] = w_src;
                ctx->gpr[10] = (uint64_t)w_ea + 4ULL;
                ctx->gpr[9] = tail_w;
                ctx->ctr = q - word_idx;
                if (tail_words > 0) {
                    ctx->gpr[0] = 0;
                    ctx->ctr = 0;
                    ctx->gpr[11] = (uint64_t)w_ea + 4ULL + (uint64_t)tail_words * 4ULL;
                    ctx->gpr[9] = ctx->gpr[11] + 4ULL;
                    ctx->gpr[10] = ctx->gpr[11];
                }
                if (rem_n > 0) {
                    ctx->gpr[0] = 0;
                    ctx->ctr = 0;
                    ctx->gpr[11] = (uint64_t)dst32 + n32;
                    ctx->gpr[9] = (uint64_t)dst32 + n32 - 1ULL;
                }
                set_cr(ctx, 0, cmps64((int32_t)rem_n, 0));
                return;
            }
            off = q * 4u;
            const uint32_t last_w = (q - 1u) * 4u;
            const uint32_t w = be32(dst32 + last_w);
            uint32_t r9_and = 0;
            uint64_t r0_or = 0;
            const uint64_t z = zero_mask32(w, &r9_and, &r0_or);
            ctx->gpr[8] = 0;
            ctx->gpr[11] = w;
            ctx->gpr[10] = q;
            ctx->gpr[9] = r9_and;
            ctx->gpr[0] = 0x7F7F7F7FULL;
            ctx->gpr[4] = (uint64_t)src32 + off;
            ctx->ctr = 0;
            set_cr(ctx, 0, cmps64((int32_t)z, 0));
        }
    } else {
        ctx->gpr[9] = align;
        ctx->gpr[0] = align & 3u;
    }

    (void)padded_in_word;
    if (rem_n == 0) {
        set_cr(ctx, 0, 2u);
        return;
    }

    const uint32_t tail_rem = (src_len >= off) ? std::min(rem_n, src_len - off) : 0u;
    if (tail_rem < rem_n) {
        if (tail_rem > 0) ctx->gpr[9] = rem_n;
        ctx->gpr[4] = (uint64_t)src32 + off + tail_rem;
        ctx->gpr[5] = rem_n - tail_rem;
        ctx->gpr[0] = 0;
        ctx->ctr = 0;
        set_cr(ctx, 0, 2u);
    } else {
        ctx->gpr[4] = (uint64_t)src32 + off + (rem_n - 1u);
        ctx->gpr[5] = 0;
        ctx->gpr[9] = rem_n;
        ctx->gpr[0] = vm_base[dst32 + n32 - 1u];
        ctx->ctr = 0;
        set_cr(ctx, 0, 4u);
    }
}

#endif
