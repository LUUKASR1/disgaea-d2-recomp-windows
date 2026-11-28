/* RSX PSO & Texture cache O(1) open-addressed indexes + single-pass FP microcode
 * size/hash with byte-checked memoization.
 *
 * Pure C17 header with zero Windows/D3D12 dependency so host unit tests exercise
 * the exact same hash tables, LRU eviction, backward-shift deletion, and FP
 * microcode walker compiled into rsx_d3d12_backend.c.
 */
#ifndef D2_RSX_CACHE_INDEX_H
#define D2_RSX_CACHE_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * 1. Single-pass NV40 FP microcode size + code/raw hash + byte-checked memo
 * ------------------------------------------------------------------------- */

#define D2_FP_MEMO_SETS      32u
#define D2_FP_MEMO_WAYS      2u
#define D2_FP_MEMO_MAX_BYTES 4096u

typedef struct D2FpMemoSlot {
    const uint8_t* ucode;
    uint32_t max_bytes;
    uint32_t usz;
    uint32_t uhash;
    uint32_t lru_tick;
    int raw_constants;
    uint8_t valid;
    uint8_t bytes[D2_FP_MEMO_MAX_BYTES];
} D2FpMemoSlot;

static D2FpMemoSlot s_d2_fp_memo[D2_FP_MEMO_SETS][D2_FP_MEMO_WAYS];
static uint32_t s_d2_fp_memo_tick = 0;
static uint64_t s_d2_fp_memo_calls = 0;
static uint64_t s_d2_fp_memo_hits = 0;

static inline void d2_fp_hash_memo_reset(void)
{
    memset(s_d2_fp_memo, 0, sizeof(s_d2_fp_memo));
    s_d2_fp_memo_tick = 0;
    s_d2_fp_memo_calls = 0;
    s_d2_fp_memo_hits = 0;
}

static inline uint32_t d2_fp_read_word(const uint8_t* p)
{
    uint32_t be = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                  ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    return (be << 16) | (be >> 16);
}

static inline uint32_t d2_fp_hash_raw_fnv32(const uint8_t* p, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* Fallback matching rsx_fp_program_size + rsx_fp_code_hash verbatim if a
 * malformed branch instruction ever sets a CONST source type. */
static inline uint32_t d2_fp_fallback_size_and_hash(
    const uint8_t* ucode, uint32_t max_bytes, int raw_constants, uint32_t* out_usz)
{
    uint32_t off = 0, term = 0;
    while (off + 16u <= max_bytes) {
        uint32_t w0 = d2_fp_read_word(ucode + off + 0u);
        uint32_t w1 = d2_fp_read_word(ucode + off + 4u);
        uint32_t w2 = d2_fp_read_word(ucode + off + 8u);
        uint32_t w3 = d2_fp_read_word(ucode + off + 12u);
        off += 16u;
        if (((w1 & 3u) == 2u) || ((w2 & 3u) == 2u) || ((w3 & 3u) == 2u)) {
            if (off + 16u <= max_bytes) off += 16u;
        }
        if (w0 & 1u) { term = off; break; }
    }
    uint32_t usz = term ? term : 64u;
    *out_usz = usz;
    if (raw_constants) {
        uint32_t n = usz <= max_bytes ? usz : max_bytes;
        return d2_fp_hash_raw_fnv32(ucode, n);
    }
    uint32_t h = 2166136261u;
    off = 0;
    while (off + 16u <= max_bytes) {
        uint32_t w0 = d2_fp_read_word(ucode + off + 0u);
        uint32_t w1 = d2_fp_read_word(ucode + off + 4u);
        uint32_t w2 = d2_fp_read_word(ucode + off + 8u);
        uint32_t w3 = d2_fp_read_word(ucode + off + 12u);
        for (uint32_t i = 0; i < 16u; i++) { h ^= ucode[off + i]; h *= 16777619u; }
        off += 16u;
        int is_branch = (w2 & 0x80000000u) != 0u;
        int has_k = !is_branch &&
                    (((w1 & 3u) == 2u) || ((w2 & 3u) == 2u) || ((w3 & 3u) == 2u));
        if (has_k) off += 16u;
        if (w0 & 1u) break;
    }
    return h;
}

static inline uint32_t d2_fp_hash_single_pass(
    const uint8_t* ucode, uint32_t max_bytes, int raw_constants,
    uint32_t* out_usz, int* out_terminated)
{
    if (!ucode) {
        if (out_usz) *out_usz = 64u;
        if (out_terminated) *out_terminated = 0;
        return 2166136261u;
    }

    uint32_t off = 0, term_off = 0;
    uint32_t code_h = 2166136261u;
    uint32_t raw_h = 2166136261u;
    int diverged = 0;

    while (off + 16u <= max_bytes) {
        uint32_t w0 = d2_fp_read_word(ucode + off + 0u);
        uint32_t w1 = d2_fp_read_word(ucode + off + 4u);
        uint32_t w2 = d2_fp_read_word(ucode + off + 8u);
        uint32_t w3 = d2_fp_read_word(ucode + off + 12u);
        if (raw_constants) {
            for (uint32_t i = 0; i < 16u; i++) { raw_h ^= ucode[off + i]; raw_h *= 16777619u; }
        } else {
            for (uint32_t i = 0; i < 16u; i++) { code_h ^= ucode[off + i]; code_h *= 16777619u; }
        }
        off += 16u;
        int const_any = ((w1 & 3u) == 2u) || ((w2 & 3u) == 2u) || ((w3 & 3u) == 2u);
        int is_branch = (w2 & 0x80000000u) != 0u;
        if (const_any) {
            if (is_branch) { diverged = 1; break; }
            if (off + 16u <= max_bytes) {
                if (raw_constants) {
                    for (uint32_t i = 0; i < 16u; i++) {
                        raw_h ^= ucode[off + i]; raw_h *= 16777619u;
                    }
                }
                off += 16u;
            }
        }
        if (w0 & 1u) {
            term_off = off;
            break;
        }
    }

    if (diverged) {
        uint32_t usz = 0;
        uint32_t h = d2_fp_fallback_size_and_hash(ucode, max_bytes, raw_constants, &usz);
        if (out_usz) *out_usz = usz;
        if (out_terminated) *out_terminated = 0;
        return h;
    }

    if (term_off == 0u) {
        uint32_t usz = 64u;
        if (out_usz) *out_usz = usz;
        if (out_terminated) *out_terminated = 0;
        if (raw_constants) {
            uint32_t n = usz <= max_bytes ? usz : max_bytes;
            return d2_fp_hash_raw_fnv32(ucode, n);
        }
        return code_h;
    }

    if (out_usz) *out_usz = term_off;
    if (out_terminated) *out_terminated = 1;
    return raw_constants ? raw_h : code_h;
}

static inline uint32_t d2_fp_hash_memoized(
    const uint8_t* ucode, uint32_t max_bytes, int raw_constants, uint32_t* out_usz)
{
    const int raw_norm = raw_constants ? 1 : 0;
    s_d2_fp_memo_calls++;

    if (!ucode || max_bytes == 0u) {
        if (out_usz) *out_usz = 64u;
        return 2166136261u;
    }

    uintptr_t addr = (uintptr_t)ucode;
    uint32_t set_idx = (uint32_t)(((addr >> 4) ^ (addr >> 11)) & (D2_FP_MEMO_SETS - 1u));
    D2FpMemoSlot* ways = s_d2_fp_memo[set_idx];
    uint32_t tick = ++s_d2_fp_memo_tick;

    for (uint32_t w = 0; w < D2_FP_MEMO_WAYS; w++) {
        D2FpMemoSlot* s = &ways[w];
        if (s->valid &&
            s->ucode == ucode &&
            s->max_bytes == max_bytes &&
            s->raw_constants == raw_norm &&
            s->usz <= D2_FP_MEMO_MAX_BYTES &&
            memcmp(s->bytes, ucode, s->usz) == 0) {
            s->lru_tick = tick;
            s_d2_fp_memo_hits++;
            if (out_usz) *out_usz = s->usz;
            return s->uhash;
        }
    }

    uint32_t usz = 0;
    int terminated = 0;
    uint32_t uhash = d2_fp_hash_single_pass(ucode, max_bytes, raw_norm, &usz, &terminated);
    if (out_usz) *out_usz = usz;

    if (terminated && usz > 0u && usz <= D2_FP_MEMO_MAX_BYTES) {
        uint32_t victim = 0u;
        if (!ways[0].valid) victim = 0u;
        else if (!ways[1].valid) victim = 1u;
        else victim = (ways[0].lru_tick <= ways[1].lru_tick) ? 0u : 1u;

        D2FpMemoSlot* dst = &ways[victim];
        dst->valid = 0u;
        memcpy(dst->bytes, ucode, usz);
        dst->ucode = ucode;
        dst->max_bytes = max_bytes;
        dst->usz = usz;
        dst->uhash = uhash;
        dst->raw_constants = raw_norm;
        dst->lru_tick = tick;
        dst->valid = 1u;
    }

    return uhash;
}

/* ---------------------------------------------------------------------------
 * 2. Shared O(1) open-addressed bucket & intrusive LRU helpers
 * ------------------------------------------------------------------------- */

typedef struct D2IndexBucket {
    uint64_t hash;
    uint16_t entry_plus_1; /* 0 = empty, 1..N = entry index + 1 */
    uint16_t pad0;
    uint32_t pad1;
} D2IndexBucket;

static inline uint64_t d2_idx_avalanche64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static inline void d2_idx_insert_bucket(
    D2IndexBucket* buckets, uint32_t mask, uint64_t hash, int slot)
{
    uint32_t pos = (uint32_t)(hash & mask);
    for (uint32_t step = 0; step <= mask; step++) {
        if (buckets[pos].entry_plus_1 == 0u) {
            buckets[pos].hash = hash;
            buckets[pos].entry_plus_1 = (uint16_t)(slot + 1);
            return;
        }
        pos = (pos + 1u) & mask;
    }
}

/* Knuth Algorithm R backward-shift deletion: eliminates tombstones permanently
 * so probe chains never degrade even after millions of wrap-around evictions. */
static inline void d2_idx_remove_bucket(
    D2IndexBucket* buckets, uint32_t mask, uint64_t hash, int slot)
{
    const uint16_t target = (uint16_t)(slot + 1);
    uint32_t pos = (uint32_t)(hash & mask);
    int found = 0;
    for (uint32_t step = 0; step <= mask; step++) {
        if (buckets[pos].entry_plus_1 == 0u) return;
        if (buckets[pos].entry_plus_1 == target) { found = 1; break; }
        pos = (pos + 1u) & mask;
    }
    if (!found) return;

    uint32_t hole = pos;
    uint32_t cur = hole;
    for (;;) {
        cur = (cur + 1u) & mask;
        if (buckets[cur].entry_plus_1 == 0u) {
            buckets[hole].entry_plus_1 = 0u;
            buckets[hole].hash = 0u;
            return;
        }
        uint32_t home = (uint32_t)(buckets[cur].hash & mask);
        uint32_t dist_cur = (cur - home) & mask;
        uint32_t dist_hole = (hole - home) & mask;
        if (dist_hole < dist_cur) {
            buckets[hole] = buckets[cur];
            hole = cur;
        }
    }
}

static inline void d2_lru_unlink(
    int16_t* prev, int16_t* next, int16_t* head, int16_t* tail, int slot)
{
    int16_t p = prev[slot];
    int16_t n = next[slot];
    if (p >= 0) next[p] = n;
    else *head = n;
    if (n >= 0) prev[n] = p;
    else *tail = p;
    prev[slot] = -1;
    next[slot] = -1;
}

static inline void d2_lru_push_front(
    int16_t* prev, int16_t* next, int16_t* head, int16_t* tail, int slot)
{
    prev[slot] = -1;
    next[slot] = *head;
    if (*head >= 0) prev[*head] = (int16_t)slot;
    else *tail = (int16_t)slot;
    *head = (int16_t)slot;
}

static inline void d2_lru_move_front(
    int16_t* prev, int16_t* next, int16_t* head, int16_t* tail, int slot)
{
    if (*head == (int16_t)slot) return;
    d2_lru_unlink(prev, next, head, tail, slot);
    d2_lru_push_front(prev, next, head, tail, slot);
}

/* ---------------------------------------------------------------------------
 * 3. Guest-FP PSO Cache O(1) Index (256 entries -> 512-slot index table)
 * ------------------------------------------------------------------------- */

#define D2_FP_CACHE_CAP   256u
#define D2_FP_INDEX_SLOTS 512u
#define D2_FP_INDEX_MASK  (D2_FP_INDEX_SLOTS - 1u)

typedef struct D2FpPsoKey {
    uint32_t fp_addr;
    uint32_t vs_idx;
    uint32_t vs_hash;
    uint32_t gen;
    uint32_t blend;
    uint32_t nrt;
    uint32_t rtfmt;
    uint32_t exp32;
    uint32_t ucode_hash;
    uint32_t cmask;
    uint32_t cull;
    uint32_t cube_mask;
    uint32_t zs_depth;
    uint32_t zs_stencil;
    uint32_t zs_front;
    uint32_t zs_back;
} D2FpPsoKey;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__cplusplus)
_Static_assert(sizeof(D2FpPsoKey) == 64u, "D2FpPsoKey must have no padding");
#endif

typedef struct D2FpCacheIndex {
    D2IndexBucket buckets[D2_FP_INDEX_SLOTS];
    D2FpPsoKey keys[D2_FP_CACHE_CAP];
    uint8_t active[D2_FP_CACHE_CAP];
    int16_t lru_prev[D2_FP_CACHE_CAP];
    int16_t lru_next[D2_FP_CACHE_CAP];
    int16_t lru_head;
    int16_t lru_tail;
    int count;
    int initialized;
    uint64_t evictions;
} D2FpCacheIndex;

static inline D2FpPsoKey d2_fp_make_key(
    uint32_t fp_addr, int vs_idx, uint32_t vs_hash, uint32_t gen,
    uint32_t blend, int nrt, uint32_t rtfmt, int exp32,
    uint32_t ucode_hash, uint32_t cmask, uint32_t cull, uint32_t cube_mask,
    uint32_t zs_depth, uint32_t zs_stencil, uint32_t zs_front, uint32_t zs_back)
{
    D2FpPsoKey k;
    k.fp_addr = fp_addr;
    k.vs_idx = (uint32_t)vs_idx;
    k.vs_hash = vs_hash;
    k.gen = gen;
    k.blend = blend;
    k.nrt = (uint32_t)nrt;
    k.rtfmt = rtfmt;
    k.exp32 = (uint32_t)exp32;
    k.ucode_hash = ucode_hash;
    k.cmask = cmask;
    k.cull = cull;
    k.cube_mask = cube_mask;
    k.zs_depth = zs_depth;
    k.zs_stencil = zs_stencil;
    k.zs_front = zs_front;
    k.zs_back = zs_back;
    return k;
}

static inline int d2_fp_key_equal(const D2FpPsoKey* a, const D2FpPsoKey* b)
{
    return a->fp_addr == b->fp_addr &&
           a->vs_idx == b->vs_idx &&
           a->vs_hash == b->vs_hash &&
           a->gen == b->gen &&
           a->blend == b->blend &&
           a->nrt == b->nrt &&
           a->rtfmt == b->rtfmt &&
           a->exp32 == b->exp32 &&
           a->ucode_hash == b->ucode_hash &&
           a->cmask == b->cmask &&
           a->cull == b->cull &&
           a->cube_mask == b->cube_mask &&
           a->zs_depth == b->zs_depth &&
           a->zs_stencil == b->zs_stencil &&
           a->zs_front == b->zs_front &&
           a->zs_back == b->zs_back;
}

static inline uint64_t d2_fp_key_hash(const D2FpPsoKey* k)
{
    const uint32_t* w = (const uint32_t*)k;
    uint64_t h = 14695981039346656037ULL;
    for (uint32_t i = 0; i < 16u; i++) {
        h ^= (uint64_t)w[i];
        h *= 1099511628211ULL;
    }
    return d2_idx_avalanche64(h);
}

static inline void d2_fp_index_reset(D2FpCacheIndex* idx)
{
    memset(idx, 0, sizeof(*idx));
    for (uint32_t i = 0; i < D2_FP_CACHE_CAP; i++) {
        idx->lru_prev[i] = -1;
        idx->lru_next[i] = -1;
    }
    idx->lru_head = -1;
    idx->lru_tail = -1;
    idx->count = 0;
    idx->initialized = 1;
}

static inline int d2_fp_index_find(D2FpCacheIndex* idx, const D2FpPsoKey* key)
{
    if (!idx->initialized) d2_fp_index_reset(idx);
    if (idx->count == 0) return -1;

    const uint64_t h = d2_fp_key_hash(key);
    uint32_t pos = (uint32_t)(h & D2_FP_INDEX_MASK);
    for (uint32_t step = 0; step < D2_FP_INDEX_SLOTS; step++) {
        uint16_t e1 = idx->buckets[pos].entry_plus_1;
        if (e1 == 0u) return -1;
        if (idx->buckets[pos].hash == h) {
            int slot = (int)(e1 - 1u);
            if (idx->active[slot] && d2_fp_key_equal(&idx->keys[slot], key)) {
                d2_lru_move_front(idx->lru_prev, idx->lru_next,
                                  &idx->lru_head, &idx->lru_tail, slot);
                return slot;
            }
        }
        pos = (pos + 1u) & D2_FP_INDEX_MASK;
    }
    return -1;
}

static inline int d2_fp_index_insert(
    D2FpCacheIndex* idx, const D2FpPsoKey* key, int* out_evicted)
{
    if (!idx->initialized) d2_fp_index_reset(idx);
    if (out_evicted) *out_evicted = 0;

    int existing = d2_fp_index_find(idx, key);
    if (existing >= 0) return existing;

    const uint64_t h = d2_fp_key_hash(key);
    int slot;
    if (idx->count < (int)D2_FP_CACHE_CAP) {
        slot = idx->count++;
        idx->keys[slot] = *key;
        idx->active[slot] = 1u;
        d2_idx_insert_bucket(idx->buckets, D2_FP_INDEX_MASK, h, slot);
        d2_lru_push_front(idx->lru_prev, idx->lru_next,
                          &idx->lru_head, &idx->lru_tail, slot);
    } else {
        slot = (int)idx->lru_tail;
        if (slot < 0 || slot >= (int)D2_FP_CACHE_CAP) slot = 0;
        if (idx->active[slot]) {
            uint64_t old_h = d2_fp_key_hash(&idx->keys[slot]);
            d2_idx_remove_bucket(idx->buckets, D2_FP_INDEX_MASK, old_h, slot);
        }
        idx->keys[slot] = *key;
        idx->active[slot] = 1u;
        d2_idx_insert_bucket(idx->buckets, D2_FP_INDEX_MASK, h, slot);
        d2_lru_move_front(idx->lru_prev, idx->lru_next,
                          &idx->lru_head, &idx->lru_tail, slot);
        idx->evictions++;
        if (out_evicted) *out_evicted = 1;
    }
    return slot;
}

/* ---------------------------------------------------------------------------
 * 4. VP Texture Slot O(1) Index (96 entries -> 256-slot index table)
 * ------------------------------------------------------------------------- */

#define D2_TEX_CACHE_CAP   96u
#define D2_TEX_INDEX_SLOTS 256u
#define D2_TEX_INDEX_MASK  (D2_TEX_INDEX_SLOTS - 1u)

typedef struct D2TexKey {
    uint32_t key_off;
    uint32_t w;
    uint32_t h;
    uint32_t fmt;
    uint32_t src_pitch;
    int32_t  cube;
} D2TexKey;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__cplusplus)
_Static_assert(sizeof(D2TexKey) == 24u, "D2TexKey must have no padding");
#endif

typedef struct D2TexCacheIndex {
    D2IndexBucket buckets[D2_TEX_INDEX_SLOTS];
    D2TexKey keys[D2_TEX_CACHE_CAP];
    uint8_t active[D2_TEX_CACHE_CAP];
    uint8_t used[D2_TEX_CACHE_CAP];
    int16_t lru_prev[D2_TEX_CACHE_CAP];
    int16_t lru_next[D2_TEX_CACHE_CAP];
    int16_t lru_head;
    int16_t lru_tail;
    int initialized;
} D2TexCacheIndex;

static inline D2TexKey d2_tex_make_key(
    uint32_t key_off, uint32_t w, uint32_t h, uint32_t fmt, uint32_t src_pitch, int cube)
{
    D2TexKey k;
    k.key_off = key_off;
    k.w = w;
    k.h = h;
    k.fmt = fmt;
    k.src_pitch = src_pitch;
    k.cube = (int32_t)cube;
    return k;
}

static inline int d2_tex_key_equal(const D2TexKey* a, const D2TexKey* b)
{
    return a->key_off == b->key_off &&
           a->w == b->w &&
           a->h == b->h &&
           a->fmt == b->fmt &&
           a->src_pitch == b->src_pitch &&
           a->cube == b->cube;
}

static inline uint64_t d2_tex_key_hash(const D2TexKey* k)
{
    const uint32_t* w = (const uint32_t*)k;
    uint64_t h = 14695981039346656037ULL;
    for (uint32_t i = 0; i < 6u; i++) {
        h ^= (uint64_t)w[i];
        h *= 1099511628211ULL;
    }
    return d2_idx_avalanche64(h);
}

static inline void d2_tex_index_reset(D2TexCacheIndex* idx)
{
    memset(idx, 0, sizeof(*idx));
    /* Chain 95 (head) <-> ... <-> 1 <-> 0 (tail) so slots fill 0..95 first. */
    for (int i = 0; i < (int)D2_TEX_CACHE_CAP; i++) {
        idx->lru_next[i] = (int16_t)(i - 1);
        idx->lru_prev[i] = (i + 1 < (int)D2_TEX_CACHE_CAP) ? (int16_t)(i + 1) : -1;
    }
    idx->lru_head = (int16_t)(D2_TEX_CACHE_CAP - 1u);
    idx->lru_tail = 0;
    idx->initialized = 1;
}

static inline void d2_tex_index_begin_frame(D2TexCacheIndex* idx)
{
    if (!idx->initialized) {
        d2_tex_index_reset(idx);
        return;
    }
    memset(idx->used, 0, sizeof(idx->used));
}

static inline int d2_tex_index_find(D2TexCacheIndex* idx, const D2TexKey* key)
{
    if (!idx->initialized) d2_tex_index_reset(idx);

    const uint64_t h = d2_tex_key_hash(key);
    uint32_t pos = (uint32_t)(h & D2_TEX_INDEX_MASK);
    for (uint32_t step = 0; step < D2_TEX_INDEX_SLOTS; step++) {
        uint16_t e1 = idx->buckets[pos].entry_plus_1;
        if (e1 == 0u) return -1;
        if (idx->buckets[pos].hash == h) {
            int slot = (int)(e1 - 1u);
            if (idx->active[slot] && d2_tex_key_equal(&idx->keys[slot], key))
                return slot;
        }
        pos = (pos + 1u) & D2_TEX_INDEX_MASK;
    }
    return -1;
}

static inline void d2_tex_index_mark_used(D2TexCacheIndex* idx, int slot)
{
    if (!idx->initialized) d2_tex_index_reset(idx);
    if (slot < 0 || slot >= (int)D2_TEX_CACHE_CAP) return;
    idx->used[slot] = 1u;
    d2_lru_move_front(idx->lru_prev, idx->lru_next,
                      &idx->lru_head, &idx->lru_tail, slot);
}

static inline int d2_tex_index_pick_free(D2TexCacheIndex* idx)
{
    if (!idx->initialized) d2_tex_index_reset(idx);
    int tail = (int)idx->lru_tail;
    if (tail < 0 || tail >= (int)D2_TEX_CACHE_CAP) return -1;
    /* Because mark_used moves every used=1 slot to lru_head, lru_tail is
     * guaranteed to have used==0 whenever any free/evictable slot exists. */
    return idx->used[tail] ? -1 : tail;
}

static inline void d2_tex_index_remove_slot(D2TexCacheIndex* idx, int slot)
{
    if (!idx->initialized) return;
    if (slot < 0 || slot >= (int)D2_TEX_CACHE_CAP) return;
    if (!idx->active[slot]) return;
    uint64_t old_h = d2_tex_key_hash(&idx->keys[slot]);
    d2_idx_remove_bucket(idx->buckets, D2_TEX_INDEX_MASK, old_h, slot);
    idx->active[slot] = 0u;
}

static inline void d2_tex_index_set_slot(
    D2TexCacheIndex* idx, int slot, const D2TexKey* key)
{
    if (!idx->initialized) d2_tex_index_reset(idx);
    if (slot < 0 || slot >= (int)D2_TEX_CACHE_CAP) return;
    if (idx->active[slot]) {
        if (d2_tex_key_equal(&idx->keys[slot], key)) return;
        d2_tex_index_remove_slot(idx, slot);
    }
    uint64_t h = d2_tex_key_hash(key);
    idx->keys[slot] = *key;
    idx->active[slot] = 1u;
    d2_idx_insert_bucket(idx->buckets, D2_TEX_INDEX_MASK, h, slot);
}

#endif /* D2_RSX_CACHE_INDEX_H */
