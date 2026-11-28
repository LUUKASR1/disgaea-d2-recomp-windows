/* BLUS31313 1.40 guest memcpy (func_00335FE4: r3 dst, r4 src, r5 size).
 * One vm_copy replaces the lifted loop when inputs are 32-bit, non-wrapping,
 * disjoint, and accepted by vm_copy; otherwise falls back to the lift.
 * Volatile GPRs, CR fields, CTR, and return value r3 end identically to the
 * lifted body so callers reading any scratch register agree. */
#include "ppu_recomp.h"
#include <cstring>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#if !defined(D2_GAME_VERSION) || D2_GAME_VERSION == 140

extern "C" {
extern uint8_t* vm_base;
int vm_copy(uint32_t dst, uint32_t src, uint32_t n);
}
void d2_original_00335FE4(ppu_context*);

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

static inline uint64_t be64(uint32_t ea)
{
    uint64_t v;
    std::memcpy(&v, vm_base + ea, sizeof v);
#if defined(_MSC_VER) && !defined(__clang__)
    return _byteswap_uint64(v);
#else
    return __builtin_bswap64(v);
#endif
}

static inline uint64_t ppc_rotl64(uint64_t v, int n)
{
    n &= 63;
    return n ? ((v << n) | (v >> (64 - n))) : v;
}

static inline uint64_t ppc_mask64(int mb, int me)
{
    uint64_t mask;
    if (mb <= me) {
        mask = (mb == 0) ? ~0ULL : (~0ULL >> mb);
        mask &= (me == 63) ? ~0ULL : (~0ULL << (63 - me));
    } else {
        mask = (~0ULL >> mb) | (~0ULL << (63 - me));
    }
    return mask;
}

static inline uint64_t ppc_rldicl(uint64_t rs, int sh, int mb)
{
    return ppc_rotl64(rs, sh) & ppc_mask64(mb, 63);
}

static inline uint64_t ppc_rldicr(uint64_t rs, int sh, int me)
{
    return ppc_rotl64(rs, sh) & ppc_mask64(0, me);
}

static void reconstruct_backward(ppu_context* ctx, uint32_t src32, uint32_t dst32, uint64_t n)
{
    uint64_t r4 = (uint64_t)src32 + n;
    uint64_t r11 = (uint64_t)dst32 + n;
    uint64_t r5 = n;
    ctx->gpr[10] = r4 - 128ULL;
    const uint64_t pre = r4 & 7ULL;
    ctx->gpr[7] = pre;
    set_cr(ctx, 4, cmps64((int64_t)pre, 0));
    if (pre != 0) {
        r4 -= pre;
        r5 -= pre;
        r11 -= pre;
        ctx->gpr[8] = vm_base[(uint32_t)r11];
        ctx->ctr = 0;
    }

    set_cr(ctx, 24, cmps64((int64_t)r5, 0));
    const uint64_t align4 = r11 & 3ULL;
    ctx->gpr[0] = align4;
    set_cr(ctx, 4, cmps64((int64_t)align4, 1));
    if (align4 == 1) {
        set_cr(ctx, 0, cmpu64(r5, 15));
        if (r5 > 15) {
            const uint64_t q = r5 >> 4;
            r4 -= q * 16ULL;
            r11 -= q * 16ULL;
            r5 -= q * 16ULL;
            const uint64_t w0 = be64((uint32_t)r11);
            const uint64_t w1 = be64((uint32_t)(r11 + 8ULL));
            ctx->gpr[6] = w0;
            ctx->gpr[8] = w1;
            ctx->gpr[7] = ppc_rldicl(w0, 8, 56);
            ctx->gpr[9] = ppc_rldicl(w1, 24, 40) | ppc_rldicr(w0, 24, 39);
            ctx->gpr[10] = ppc_rldicl(w1, 56, 8);
            ctx->gpr[12] = ppc_rldicl(w0, 56, 8);
            ctx->gpr[0] = ppc_rldicl(w0, 24, 40);
            ctx->ctr = 0;
        }
    } else {
        set_cr(ctx, 24, cmps64((int64_t)align4, 2));
        if (align4 == 2) {
            set_cr(ctx, 4, cmpu64(r5, 15));
            if (r5 > 15) {
                const uint64_t q = r5 >> 4;
                r4 -= q * 16ULL;
                r11 -= q * 16ULL;
                r5 -= q * 16ULL;
                const uint64_t w0 = be64((uint32_t)r11);
                const uint64_t w1 = be64((uint32_t)(r11 + 8ULL));
                ctx->gpr[12] = w1;
                ctx->gpr[8] = w0;
                ctx->gpr[6] = ppc_rldicr(w0, 16, 47);
                ctx->gpr[7] = ppc_rldicl(w0, 16, 48);
                ctx->gpr[9] = ppc_rldicl(w1, 16, 48) | ctx->gpr[6];
                ctx->gpr[10] = ppc_rldicl(w1, 48, 16);
                ctx->gpr[0] = ppc_rldicl(w0, 48, 16);
                ctx->ctr = 0;
            }
        } else {
            set_cr(ctx, 28, cmps64((int64_t)align4, 3));
            if (align4 == 3) {
                set_cr(ctx, 24, cmpu64(r5, 15));
                if (r5 > 15) {
                    const uint64_t q = r5 >> 4;
                    r4 -= q * 16ULL;
                    r11 -= q * 16ULL;
                    r5 -= q * 16ULL;
                    const uint64_t w0 = be64((uint32_t)r11);
                    const uint64_t w1 = be64((uint32_t)(r11 + 8ULL));
                    ctx->gpr[10] = w1;
                    ctx->gpr[12] = w0;
                    ctx->gpr[7] = ppc_rldicl(w0, 8, 56);
                    ctx->gpr[6] = ppc_rldicl(w1, 8, 56) | ppc_rldicr(w0, 8, 55);
                    ctx->gpr[8] = ppc_rldicl(w1, 56, 8);
                    ctx->gpr[9] = ppc_rldicl(w1, 40, 24);
                    ctx->gpr[0] = ppc_rldicl(w0, 40, 24);
                    ctx->ctr = 0;
                }
            } else {
                const uint64_t r12 = r11 & 7ULL;
                ctx->gpr[12] = r12;
                set_cr(ctx, 0, cmps64((int64_t)r12, 0));
                if (r12 != 0) {
                    set_cr(ctx, 0, cmpu64(r5, 23));
                    if (r5 > 23) {
                        const uint64_t k = r5 / 24ULL;
                        r4 -= k * 24ULL;
                        r11 -= k * 24ULL;
                        r5 -= k * 24ULL;
                        const uint64_t w0 = be64((uint32_t)r11);
                        const uint64_t w1 = be64((uint32_t)(r11 + 8ULL));
                        const uint64_t w2 = be64((uint32_t)(r11 + 16ULL));
                        ctx->gpr[6] = w0;
                        ctx->gpr[8] = w1;
                        ctx->gpr[10] = w2;
                        ctx->gpr[0] = ppc_rldicl(w2, 32, 32);
                        ctx->gpr[9] = ppc_rldicl(w1, 32, 32);
                        ctx->gpr[12] = ppc_rldicl(w0, 32, 32);
                    }
                } else {
                    set_cr(ctx, 28, cmpu64(r5, 47));
                    if (r5 > 47) {
                        const uint64_t k = r5 / 48ULL;
                        r4 -= k * 48ULL;
                        r11 -= k * 48ULL;
                        r5 -= k * 48ULL;
                        ctx->gpr[0] = be64((uint32_t)(r11 + 0x28ULL));
                        ctx->gpr[9] = be64((uint32_t)(r11 + 0x20ULL));
                        ctx->gpr[12] = be64((uint32_t)(r11 + 0x18ULL));
                        ctx->gpr[10] = be64((uint32_t)(r11 + 0x10ULL));
                        ctx->gpr[8] = be64((uint32_t)(r11 + 0x08ULL));
                        ctx->gpr[7] = be64((uint32_t)(r11 + 0x00ULL));
                        if (k & 1ULL) {
                            if (k >= 3ULL) {
                                ctx->gpr[6] = (r4 + 48ULL) - 256ULL;
                                set_cr(ctx, 28, 4u);
                            }
                            set_cr(ctx, 24, cmpu64(r5, 47));
                        } else {
                            ctx->gpr[6] = r4 - 256ULL;
                            set_cr(ctx, 24, 4u);
                            set_cr(ctx, 28, cmpu64(r5, 47));
                        }
                    }
                    set_cr(ctx, 4, cmpu64(r5, 7));
                    if (r5 > 7) {
                        const uint64_t q = r5 >> 3;
                        ctx->gpr[6] = q;
                        r4 -= q * 8ULL;
                        r11 -= q * 8ULL;
                        r5 -= q * 8ULL;
                        ctx->gpr[7] = be64((uint32_t)r11);
                        ctx->ctr = 0;
                    }
                }
            }
        }
    }

    set_cr(ctx, 0, cmps64((int64_t)r5, 0));
    ctx->ctr = 0;
    if (r5 > 0) {
        r4 -= r5;
        r11 -= r5;
        ctx->gpr[6] = vm_base[(uint32_t)r11];
    }
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[11] = r11;
}

static void reconstruct_forward(ppu_context* ctx, uint32_t src32, uint32_t dst32, uint64_t n)
{
    uint64_t r4 = src32;
    uint64_t r11 = dst32;
    uint64_t r5 = n;
    ctx->gpr[10] = r4 + 0x80ULL;
    const uint64_t pre = r4 & 7ULL;
    ctx->gpr[7] = pre;
    set_cr(ctx, 24, cmps64((int64_t)pre, 0));
    if (pre != 0) {
        const uint64_t rem = 8ULL - pre;
        ctx->gpr[7] = rem;
        r4 += rem;
        r5 -= rem;
        r11 += rem;
        ctx->gpr[6] = vm_base[(uint32_t)(r11 - 1ULL)];
        ctx->ctr = 0;
    }

    set_cr(ctx, 28, cmps64((int64_t)r5, 0));
    const uint64_t align4 = r11 & 3ULL;
    ctx->gpr[0] = align4;
    set_cr(ctx, 24, cmps64((int64_t)align4, 1));
    if (align4 == 1) {
        set_cr(ctx, 4, cmpu64(r5, 15));
        if (r5 > 15) {
            const uint64_t q = r5 >> 4;
            const uint32_t base = (uint32_t)(r11 + (q - 1ULL) * 16ULL);
            const uint64_t w0 = be64(base);
            const uint64_t w1 = be64(base + 8u);
            r4 += q * 16ULL;
            r11 += q * 16ULL;
            r5 -= q * 16ULL;
            ctx->gpr[12] = w0;
            ctx->gpr[10] = w1;
            ctx->gpr[9] = ppc_rldicl(w1, 24, 40) | ppc_rldicr(w0, 24, 39);
            ctx->gpr[7] = ppc_rldicl(w0, 8, 56);
            ctx->gpr[8] = ppc_rldicl(w0, 24, 40);
            ctx->gpr[0] = ppc_rldicl(w0, 56, 8);
            ctx->gpr[6] = ppc_rldicl(w1, 56, 8);
            ctx->ctr = 0;
        }
    } else {
        set_cr(ctx, 28, cmps64((int64_t)align4, 2));
        if (align4 == 2) {
            set_cr(ctx, 24, cmpu64(r5, 15));
            if (r5 > 15) {
                const uint64_t q = r5 >> 4;
                const uint32_t base = (uint32_t)(r11 + (q - 1ULL) * 16ULL);
                const uint64_t w0 = be64(base);
                const uint64_t w1 = be64(base + 8u);
                r4 += q * 16ULL;
                r11 += q * 16ULL;
                r5 -= q * 16ULL;
                ctx->gpr[12] = w0;
                ctx->gpr[6] = w1;
                ctx->gpr[10] = ppc_rldicr(w0, 16, 47);
                ctx->gpr[8] = ppc_rldicl(w0, 16, 48);
                ctx->gpr[9] = ppc_rldicl(w1, 16, 48) | ctx->gpr[10];
                ctx->gpr[0] = ppc_rldicl(w0, 48, 16);
                ctx->gpr[7] = ppc_rldicl(w1, 48, 16);
                ctx->ctr = 0;
            }
        } else {
            set_cr(ctx, 0, cmps64((int64_t)align4, 3));
            if (align4 == 3) {
                set_cr(ctx, 28, cmpu64(r5, 15));
                if (r5 > 15) {
                    const uint64_t q = r5 >> 4;
                    const uint32_t base = (uint32_t)(r11 + (q - 1ULL) * 16ULL);
                    const uint64_t w0 = be64(base);
                    const uint64_t w1 = be64(base + 8u);
                    r4 += q * 16ULL;
                    r11 += q * 16ULL;
                    r5 -= q * 16ULL;
                    ctx->gpr[7] = w0;
                    ctx->gpr[12] = w1;
                    ctx->gpr[8] = ppc_rldicl(w0, 8, 56);
                    ctx->gpr[9] = ppc_rldicl(w1, 8, 56) | ppc_rldicr(w0, 8, 55);
                    ctx->gpr[0] = ppc_rldicl(w0, 40, 24);
                    ctx->gpr[10] = ppc_rldicl(w1, 40, 24);
                    ctx->gpr[6] = ppc_rldicl(w1, 56, 8);
                    ctx->ctr = 0;
                }
            } else {
                const uint64_t r6 = r11 & 7ULL;
                ctx->gpr[6] = r6;
                set_cr(ctx, 4, cmps64((int64_t)r6, 0));
                if (r6 != 0) {
                    set_cr(ctx, 0, cmpu64(r5, 23));
                    if (r5 > 23) {
                        const uint64_t k = r5 / 24ULL;
                        const uint32_t base = (uint32_t)(r11 + (k - 1ULL) * 24ULL);
                        const uint64_t w0 = be64(base);
                        const uint64_t w1 = be64(base + 8u);
                        const uint64_t w2 = be64(base + 16u);
                        r4 += k * 24ULL;
                        r11 += k * 24ULL;
                        r5 -= k * 24ULL;
                        ctx->gpr[10] = w0;
                        ctx->gpr[8] = w1;
                        ctx->gpr[12] = w2;
                        ctx->gpr[0] = ppc_rldicl(w0, 32, 32);
                        ctx->gpr[9] = ppc_rldicl(w1, 32, 32);
                        ctx->gpr[6] = ppc_rldicl(w2, 32, 32);
                        set_cr(ctx, 0, cmpu64(r5, 23));
                    }
                } else {
                    set_cr(ctx, 0, cmpu64(r5, 0x100));
                    if (r5 > 0x100) {
                        const uint64_t k = r5 / 48ULL;
                        const uint32_t base = (uint32_t)(r11 + (k - 1ULL) * 48ULL);
                        const uint64_t w0 = be64(base + 0x00u);
                        const uint64_t w1 = be64(base + 0x08u);
                        const uint64_t w2 = be64(base + 0x10u);
                        const uint64_t w3 = be64(base + 0x18u);
                        const uint64_t w4 = be64(base + 0x20u);
                        const uint64_t w5 = be64(base + 0x28u);
                        r4 += k * 48ULL;
                        r11 += k * 48ULL;
                        r5 -= k * 48ULL;
                        if (k & 1ULL) {
                            ctx->gpr[7] = w0;
                            ctx->gpr[0] = w1;
                            ctx->gpr[9] = w2;
                            ctx->gpr[6] = w3;
                            ctx->gpr[10] = w4;
                            ctx->gpr[8] = w5;
                            ctx->gpr[12] = (r4 - 48ULL) + 0x100ULL;
                            set_cr(ctx, 0, 4u);
                        } else {
                            ctx->gpr[0] = w0;
                            ctx->gpr[9] = w1;
                            ctx->gpr[6] = w2;
                            ctx->gpr[10] = w3;
                            ctx->gpr[8] = w4;
                            ctx->gpr[7] = w5;
                            ctx->gpr[12] = r4 + 0x100ULL;
                            set_cr(ctx, 0, cmpu64(r5, 47));
                        }
                    }
                    set_cr(ctx, 24, cmpu64(r5, 15));
                    if (r5 > 15) {
                        const uint64_t q = r5 >> 3;
                        ctx->gpr[12] = q;
                        const uint32_t tail_base = (uint32_t)(r11 + (q - 1ULL) * 8ULL);
                        ctx->gpr[7] = be64(tail_base);
                        r4 += q * 8ULL;
                        r11 += q * 8ULL;
                        r5 -= q * 8ULL;
                        ctx->ctr = 0;
                    }
                }
            }
        }
    }

    set_cr(ctx, 28, cmps64((int64_t)r5, 0));
    if (r5 > 0) {
        r4 += r5;
        r11 += r5;
        ctx->gpr[0] = vm_base[(uint32_t)(r11 - 1ULL)];
        ctx->ctr = 0;
    }
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[11] = r11;
}

void func_00335FE4(ppu_context* ctx)
{
    const uint64_t dst = ctx->gpr[3];
    const uint64_t src = ctx->gpr[4];
    const uint64_t n = ctx->gpr[5];
    const uint64_t dst_end = dst + n;
    const uint64_t src_end = src + n;
    if ((dst | src | n) >> 32 || (dst_end | src_end) >> 32 ||
        (dst < src_end && src < dst_end) ||
        !vm_copy((uint32_t)dst, (uint32_t)src, (uint32_t)n)) {
        d2_original_00335FE4(ctx);
        return;
    }

    const uint32_t dst32 = (uint32_t)dst;
    const uint32_t src32 = (uint32_t)src;
    set_cr(ctx, 0, cmpu64(n, 7));
    ctx->gpr[3] = dst32;
    ctx->gpr[4] = src32;
    ctx->gpr[11] = dst32;

    if (n <= 7) {
        if (n == 0) {
            set_cr(ctx, 28, 2u);
            return;
        }
        set_cr(ctx, 24, 8u);
        set_cr(ctx, 28, 4u);
        ctx->gpr[0] = vm_base[dst32 + (uint32_t)n - 1u];
        ctx->gpr[4] = src_end;
        ctx->gpr[11] = dst_end;
        ctx->ctr = 0;
        return;
    }

    const uint64_t src_off = src32 & 0xFFFu;
    const uint64_t dst_off = dst32 & 0xFFFu;
    set_cr(ctx, 24, cmpu64(src_off, dst_off));
    ctx->gpr[6] = src_off;
    ctx->gpr[9] = dst_off;

    if (src_off < dst_off) {
        reconstruct_backward(ctx, src32, dst32, n);
    } else {
        reconstruct_forward(ctx, src32, dst32, n);
    }
}

#endif
