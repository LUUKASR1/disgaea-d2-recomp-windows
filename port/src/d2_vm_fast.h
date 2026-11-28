/* Inlined PPU guest memory fast path for lifted translation units.
 * Included via -include before ppu_recomp.h; #pragma once in ppu_recomp.h
 * prevents redeclaration of vm_read/vm_write after macro redirection below. */
#pragma once

#if defined(__clang__) || defined(__GNUC__)

#include "ppu_recomp.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
extern uint8_t* vm_base;
extern uint32_t ppu_vm_size;
extern uint32_t ppu_hle_inject_base;
extern int      g_vm_read_slow_active;
extern int      g_vm_write_slow_active;
extern uint32_t g_barrier_sync_watch;
extern uint32_t g_null_sweep_hi;
extern uint64_t g_spu_coh_range;
extern uint64_t g_spu_coh_line_bloom;
extern unsigned char g_spu_coh_bitmap[];
#ifdef __cplusplus
}
#endif

#if defined(__GNUC__) || defined(__clang__)
#define D2_VM_INLINE static inline __attribute__((always_inline))
#else
#define D2_VM_INLINE static inline
#endif

D2_VM_INLINE int d2_vm_read_is_slow(uint32_t ea)
{
    if (__builtin_expect((g_vm_read_slow_active | (int)ppu_vm_size) != 0, 0))
        return 1;
    if (__builtin_expect((uint32_t)(ea - 0x10000u) >= (0xDFFFB000u - 0x10000u), 0))
        return 1;
    if (__builtin_expect((uint32_t)(ea - ppu_hle_inject_base) < 0x3000u, 0))
        return 1;
    return 0;
}

D2_VM_INLINE int d2_vm_write_is_slow(uint32_t ea)
{
    if (__builtin_expect((g_vm_write_slow_active | (int)ppu_vm_size |
                          (int)g_barrier_sync_watch | (int)g_null_sweep_hi) != 0, 0))
        return 1;
    if (__builtin_expect((uint32_t)(ea - 0x10000u) >= (0xDFFFB000u - 0x10000u), 0))
        return 1;
    if (__builtin_expect((uint32_t)(ea - (ppu_hle_inject_base - 8u)) < 0x3008u, 0))
        return 1;
    /* L1-hot atomic [min_ea, max_ea] + 64-bit line bloom guard keeps stack
     * spills and unrelated heap writes off the 4 MiB g_spu_coh_bitmap. */
    {
        uint64_t range = __atomic_load_n(&g_spu_coh_range, __ATOMIC_ACQUIRE);
        uint32_t coh_min = (uint32_t)range;
        uint32_t coh_max = (uint32_t)(range >> 32);
        if (__builtin_expect(ea >= coh_min && ea <= coh_max, 0)) {
            uint32_t line = ea >> 7;
            uint64_t bloom = __atomic_load_n(&g_spu_coh_line_bloom, __ATOMIC_ACQUIRE);
            if (__builtin_expect((bloom >> ((line ^ (line >> 6) ^ (line >> 12)) & 63u)) & 1u, 0)) {
                if (__builtin_expect((__atomic_load_n(&g_spu_coh_bitmap[line >> 3],
                                                      __ATOMIC_ACQUIRE) >> (line & 7)) & 1u, 0))
                    return 1;
            }
        }
    }
    return 0;
}

D2_VM_INLINE uint8_t d2_vm_read8(uint64_t addr)
{
    uint32_t ea = (uint32_t)addr;
    if (__builtin_expect(d2_vm_read_is_slow(ea), 0))
        return (vm_read8)(addr);
    return vm_base[ea];
}

D2_VM_INLINE uint16_t d2_vm_read16(uint64_t addr)
{
    uint32_t ea = (uint32_t)addr;
    if (__builtin_expect(d2_vm_read_is_slow(ea), 0))
        return (vm_read16)(addr);
    uint16_t v;
    memcpy(&v, vm_base + ea, sizeof(v));
    return __builtin_bswap16(v);
}

D2_VM_INLINE uint32_t d2_vm_read32(uint64_t addr)
{
    uint32_t ea = (uint32_t)addr;
    if (__builtin_expect(d2_vm_read_is_slow(ea), 0))
        return (vm_read32)(addr);
    uint32_t v;
    memcpy(&v, vm_base + ea, sizeof(v));
    return __builtin_bswap32(v);
}

D2_VM_INLINE uint64_t d2_vm_read64(uint64_t addr)
{
    uint32_t ea = (uint32_t)addr;
    if (__builtin_expect(d2_vm_read_is_slow(ea), 0))
        return (vm_read64)(addr);
    uint64_t v;
    memcpy(&v, vm_base + ea, sizeof(v));
    return __builtin_bswap64(v);
}

D2_VM_INLINE void d2_vm_write8(uint64_t addr, uint8_t val)
{
    uint32_t ea = (uint32_t)addr;
    if (__builtin_expect(d2_vm_write_is_slow(ea), 0)) {
        (vm_write8)(addr, val);
        return;
    }
    vm_base[ea] = val;
}

D2_VM_INLINE void d2_vm_write16(uint64_t addr, uint16_t val)
{
    uint32_t ea = (uint32_t)addr;
    if (__builtin_expect(d2_vm_write_is_slow(ea), 0)) {
        (vm_write16)(addr, val);
        return;
    }
    uint16_t be = __builtin_bswap16(val);
    memcpy(vm_base + ea, &be, sizeof(be));
}

D2_VM_INLINE void d2_vm_write32(uint64_t addr, uint32_t val)
{
    uint32_t ea = (uint32_t)addr;
    if (__builtin_expect(d2_vm_write_is_slow(ea), 0)) {
        (vm_write32)(addr, val);
        return;
    }
    uint32_t be = __builtin_bswap32(val);
    memcpy(vm_base + ea, &be, sizeof(be));
}

D2_VM_INLINE void d2_vm_write64(uint64_t addr, uint64_t val)
{
    uint32_t ea = (uint32_t)addr;
    if (__builtin_expect(d2_vm_write_is_slow(ea), 0)) {
        (vm_write64)(addr, val);
        return;
    }
    uint64_t be = __builtin_bswap64(val);
    memcpy(vm_base + ea, &be, sizeof(be));
}

#define vm_read8(a)     d2_vm_read8(a)
#define vm_read16(a)    d2_vm_read16(a)
#define vm_read32(a)    d2_vm_read32(a)
#define vm_read64(a)    d2_vm_read64(a)
#define vm_write8(a,v)  d2_vm_write8((a), (v))
#define vm_write16(a,v) d2_vm_write16((a), (v))
#define vm_write32(a,v) d2_vm_write32((a), (v))
#define vm_write64(a,v) d2_vm_write64((a), (v))

#endif /* __clang__ || __GNUC__ */
