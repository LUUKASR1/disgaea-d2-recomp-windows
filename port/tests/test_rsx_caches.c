#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ps3emu/ps3types.h"
#include "rsx_commands.h"
#include "rsx_fp_decompiler.h"
#include "rsx_texture_layout.h"
#include "d2_rsx_cache_index.h"

#define MAX_DRAWS                2048u
#define VP_CB_STRIDE             8448u
#define VP_FPCB_STRIDE           1280u
typedef struct {
    int vp_parity;
    void* vp_cb_mapped;
    void* vp_fpcb_mapped;
    const rsx_state* current_rsx_state;
    struct { u32 cb_slot; } draws[MAX_DRAWS];
} TestMockD3DState;
static TestMockD3DState s_d3d;
#include "d2_fp_const_cache.h"
#include "d2_constant_pair.h"

static uint32_t rng_state = 0x12345678u;
static uint32_t next_u32(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static void write_fp_word(uint8_t* p, uint32_t host_word)
{
    /* Inverse of rsx_fp_read_word: halfword swap + big-endian encode */
    uint32_t be = (host_word << 16) | (host_word >> 16);
    p[0] = (uint8_t)((be >> 24) & 0xFFu);
    p[1] = (uint8_t)((be >> 16) & 0xFFu);
    p[2] = (uint8_t)((be >> 8) & 0xFFu);
    p[3] = (uint8_t)(be & 0xFFu);
}

static uint32_t ref_fp_size_and_hash(
    const uint8_t* ucode, uint32_t max_bytes, int raw_constants, uint32_t* out_usz)
{
    uint32_t usz = rsx_fp_program_size(ucode, max_bytes);
    if (usz == 0) usz = 64u;
    *out_usz = usz;
    if (raw_constants) {
        uint32_t h = 2166136261u;
        for (uint32_t i = 0; i < usz; i++) {
            h ^= ucode[i];
            h *= 16777619u;
        }
        return h;
    }
    return rsx_fp_code_hash(ucode, max_bytes);
}

/* ---------------------------------------------------------------------------
 * Test 1: Single-pass FP size+hash & memoization vs upstream reference
 * ------------------------------------------------------------------------- */
static void test_fp_single_pass_and_memo(void)
{
    printf("[TEST] FP single-pass size+hash + memoization...\n");
    d2_fp_hash_memo_reset();

    /* Hand-crafted multi-instruction FP with inline CONSTs, branch, and END */
    uint8_t prog[4096];
    memset(prog, 0, sizeof(prog));

    /* Instr 0: MOV with CONST in src0 (off 0..31) */
    write_fp_word(prog + 0,  (0x01u << 24));
    write_fp_word(prog + 4,  2u); /* src0 = CONST */
    write_fp_word(prog + 8,  0u);
    write_fp_word(prog + 12, 0u);
    write_fp_word(prog + 16, 0x3F800000u);
    write_fp_word(prog + 20, 0x00000000u);
    write_fp_word(prog + 24, 0x3F000000u);
    write_fp_word(prog + 28, 0x3F800000u);

    /* Instr 1: Branch without CONST (off 32..47) */
    write_fp_word(prog + 32, (0x02u << 24));
    write_fp_word(prog + 36, 0u);
    write_fp_word(prog + 40, 0x80000000u); /* FP_BRANCH */
    write_fp_word(prog + 44, 16u);

    /* Instr 2: MAD with CONST in src2 + FP_END (off 48..79) */
    write_fp_word(prog + 48, (0x04u << 24) | 1u); /* FP_END */
    write_fp_word(prog + 52, 0u);
    write_fp_word(prog + 56, 1u);
    write_fp_word(prog + 60, 2u); /* src2 = CONST */
    write_fp_word(prog + 64, 0x40000000u);
    write_fp_word(prog + 68, 0x40400000u);
    write_fp_word(prog + 72, 0x40800000u);
    write_fp_word(prog + 76, 0x3F800000u);

    uint32_t ref_sz = 0, ref_h = 0, got_sz = 0, got_h = 0;

    /* Check raw_constants = 0 */
    ref_h = ref_fp_size_and_hash(prog, 4096u, 0, &ref_sz);
    got_h = d2_fp_hash_memoized(prog, 4096u, 0, &got_sz);
    assert(ref_sz == 80u && got_sz == ref_sz && got_h == ref_h);

    /* Repeat should hit memo */
    uint64_t hits_before = s_d2_fp_memo_hits;
    got_h = d2_fp_hash_memoized(prog, 4096u, 0, &got_sz);
    assert(s_d2_fp_memo_hits == hits_before + 1u);
    assert(got_sz == ref_sz && got_h == ref_h);

    /* Mutate inline constant in place: raw=0 preserves code hash, raw=1 changes hash */
    write_fp_word(prog + 16, 0x41200000u);
    ref_h = ref_fp_size_and_hash(prog, 4096u, 0, &ref_sz);
    got_h = d2_fp_hash_memoized(prog, 4096u, 0, &got_sz);
    assert(got_sz == ref_sz && got_h == ref_h);

    uint32_t raw_ref_h = ref_fp_size_and_hash(prog, 4096u, 1, &ref_sz);
    uint32_t raw_got_h = d2_fp_hash_memoized(prog, 4096u, 1, &got_sz);
    assert(raw_got_h == raw_ref_h && got_sz == ref_sz);

    /* Mutate inline constant again with raw=1 -> must NOT serve stale memo! */
    write_fp_word(prog + 64, 0xBF800000u);
    uint32_t raw_ref_h2 = ref_fp_size_and_hash(prog, 4096u, 1, &ref_sz);
    uint32_t raw_got_h2 = d2_fp_hash_memoized(prog, 4096u, 1, &got_sz);
    assert(raw_ref_h2 != raw_ref_h);
    assert(raw_got_h2 == raw_ref_h2 && got_sz == ref_sz);

    /* Mutate instruction opcode in place -> both modes update immediately */
    write_fp_word(prog + 0, (0x03u << 24));
    ref_h = ref_fp_size_and_hash(prog, 4096u, 0, &ref_sz);
    got_h = d2_fp_hash_memoized(prog, 4096u, 0, &got_sz);
    assert(got_sz == ref_sz && got_h == ref_h);

    /* Move FP_END earlier (instr 0) -> shrinks usz to 32 */
    write_fp_word(prog + 0, (0x03u << 24) | 1u);
    ref_h = ref_fp_size_and_hash(prog, 4096u, 0, &ref_sz);
    got_h = d2_fp_hash_memoized(prog, 4096u, 0, &got_sz);
    assert(ref_sz == 32u && got_sz == 32u && got_h == ref_h);

    /* Unterminated program -> usz = 64 fallback */
    uint8_t unterm[4096];
    memset(unterm, 0, sizeof(unterm));
    for (int raw = 0; raw <= 1; raw++) {
        ref_h = ref_fp_size_and_hash(unterm, 4096u, raw, &ref_sz);
        got_h = d2_fp_hash_memoized(unterm, 4096u, raw, &got_sz);
        assert(ref_sz == 64u && got_sz == 64u && got_h == ref_h);
    }

    /* Synthetic branch + CONST edge case */
    uint8_t branch_const[128];
    memset(branch_const, 0, sizeof(branch_const));
    write_fp_word(branch_const + 0, (0x02u << 24));
    write_fp_word(branch_const + 4, 2u); /* CONST */
    write_fp_word(branch_const + 8, 0x80000000u); /* FP_BRANCH */
    write_fp_word(branch_const + 16, (0x01u << 24) | 1u);
    write_fp_word(branch_const + 32, (0x01u << 24) | 1u);
    for (int raw = 0; raw <= 1; raw++) {
        ref_h = ref_fp_size_and_hash(branch_const, 128u, raw, &ref_sz);
        got_h = d2_fp_hash_memoized(branch_const, 128u, raw, &got_sz);
        assert(got_sz == ref_sz && got_h == ref_h);
    }

    /* 5,000 randomized programs across buffer pool (> 64 entries to exercise memo eviction) */
    static uint8_t pool[96][512];
    for (int iter = 0; iter < 5000; iter++) {
        uint8_t* buf = pool[iter % 96];
        uint32_t ninstr = 1u + (next_u32() % 14u);
        uint32_t off = 0;
        for (uint32_t ins = 0; ins < ninstr && off + 16u <= sizeof(pool[0]); ins++) {
            uint32_t w0 = (next_u32() & ~1u) | ((ins + 1u == ninstr) ? 1u : 0u);
            uint32_t w1 = next_u32();
            uint32_t w2 = next_u32();
            uint32_t w3 = next_u32();
            if ((next_u32() & 3u) == 0u) w2 |= 0x80000000u; else w2 &= ~0x80000000u;
            if ((w2 & 0x80000000u) != 0u) {
                if ((w1 & 3u) == 2u) w1 &= ~3u;
                if ((w2 & 3u) == 2u) w2 &= ~3u;
                if ((w3 & 3u) == 2u) w3 &= ~3u;
            }
            write_fp_word(buf + off + 0u, w0);
            write_fp_word(buf + off + 4u, w1);
            write_fp_word(buf + off + 8u, w2);
            write_fp_word(buf + off + 12u, w3);
            off += 16u;
            if (((w1 & 3u) == 2u || (w2 & 3u) == 2u || (w3 & 3u) == 2u) &&
                off + 16u <= sizeof(pool[0])) {
                for (int c = 0; c < 4; c++) write_fp_word(buf + off + (uint32_t)c * 4u, next_u32());
                off += 16u;
            }
        }
        int raw = (int)(next_u32() & 1u);
        ref_h = ref_fp_size_and_hash(buf, sizeof(pool[0]), raw, &ref_sz);
        got_h = d2_fp_hash_memoized(buf, sizeof(pool[0]), raw, &got_sz);
        assert(got_sz == ref_sz && got_h == ref_h);
        /* Immediate second call must match and hit */
        uint32_t got_sz2 = 0;
        uint32_t got_h2 = d2_fp_hash_memoized(buf, sizeof(pool[0]), raw, &got_sz2);
        assert(got_sz2 == ref_sz && got_h2 == ref_h);
    }
    printf("  PASS (calls=%llu hits=%llu)\n",
           (unsigned long long)s_d2_fp_memo_calls,
           (unsigned long long)s_d2_fp_memo_hits);
}

/* ---------------------------------------------------------------------------
 * Test 2: vp_fp O(1) 512-slot index: collisions, LRU/FIFO wrap, reset
 * ------------------------------------------------------------------------- */
static void assert_fp_index_invariants(D2FpCacheIndex* idx, const D2FpPsoKey* oracle, int oracle_n)
{
    assert(idx->count == oracle_n);
    int bucket_count = 0;
    for (uint32_t b = 0; b < D2_FP_INDEX_SLOTS; b++) {
        if (idx->buckets[b].entry_plus_1 != 0u) {
            int s = (int)(idx->buckets[b].entry_plus_1 - 1u);
            assert(s >= 0 && s < idx->count);
            assert(idx->active[s]);
            bucket_count++;
        }
    }
    assert(bucket_count == idx->count);

    for (int i = 0; i < oracle_n; i++) {
        int found = d2_fp_index_find(idx, &oracle[i]);
        assert(found == i);
    }
}

static D2FpPsoKey make_random_fp_key(uint32_t id)
{
    return d2_fp_make_key(
        0x00B90001u + (id & 0x3FFu) * 4u,
        (int)(id % 17u) - 1,
        0x20000000u ^ (id * 2654435761u),
        1u + (id % 3u),
        id * 0x101u,
        (int)(1u + (id % 4u)),
        28u + (id % 3u),
        (int)(id & 1u),
        id * 16777619u,
        id & 0xFu,
        id & 7u,
        id & 0xFu,
        id & 0x1Fu,
        id & 0xFFFFu,
        id & 0xFFFu,
        (id >> 3) & 0xFFFu);
}

static void test_fp_cache_index(void)
{
    printf("[TEST] vp_fp 512-slot open-addressed index...\n");
    D2FpCacheIndex idx;
    d2_fp_index_reset(&idx);

    D2FpPsoKey oracle[D2_FP_CACHE_CAP];
    int oracle_n = 0;

    /* 1. Find 20 keys that all collide onto the EXACT SAME bucket (hash & 511 == 42) */
    D2FpPsoKey colliding[20];
    int ncol = 0;
    uint32_t probe = 1u;
    while (ncol < 20) {
        D2FpPsoKey k = make_random_fp_key(probe++);
        if ((d2_fp_key_hash(&k) & D2_FP_INDEX_MASK) == 42u)
            colliding[ncol++] = k;
    }

    for (int i = 0; i < 20; i++) {
        int ev = 0;
        int s = d2_fp_index_insert(&idx, &colliding[i], &ev);
        assert(ev == 0 && s == i);
        oracle[oracle_n++] = colliding[i];
    }
    assert_fp_index_invariants(&idx, oracle, oracle_n);

    /* 2. Per-field mismatch check against colliding[0] */
    for (int field = 0; field < 16; field++) {
        D2FpPsoKey m = colliding[0];
        uint32_t* words = (uint32_t*)&m;
        words[field] ^= 1u;
        assert(d2_fp_index_find(&idx, &m) == -1);
    }

    /* 3. Fill up to 256, then keep touching slot 0 (LRU protection) and evict 300+ more */
    for (int i = 20; i < (int)D2_FP_CACHE_CAP; i++) {
        D2FpPsoKey k = make_random_fp_key(10000u + (uint32_t)i);
        int ev = 0;
        int s = d2_fp_index_insert(&idx, &k, &ev);
        assert(ev == 0 && s == i);
        oracle[oracle_n++] = k;
    }
    assert(oracle_n == (int)D2_FP_CACHE_CAP);
    assert_fp_index_invariants(&idx, oracle, oracle_n);

    /* Touch colliding[0] (slot 0) so slot 0 becomes MRU, slot 1 becomes LRU tail */
    assert(d2_fp_index_find(&idx, &colliding[0]) == 0);

    D2FpPsoKey k256 = make_random_fp_key(999999u);
    int ev = 0;
    int victim = d2_fp_index_insert(&idx, &k256, &ev);
    assert(ev == 1);
    assert(victim == 1); /* colliding[1] was evicted, colliding[0] survived! */
    oracle[1] = k256;
    assert(d2_fp_index_find(&idx, &colliding[1]) == -1);
    assert(d2_fp_index_find(&idx, &colliding[0]) == 0);
    assert(d2_fp_index_find(&idx, &k256) == 1);

    /* 4. High-volume wrap-around eviction stress (25,000 ops: hits + misses + colliding keys) */
    uint64_t lru_seq[D2_FP_CACHE_CAP];
    uint64_t tick = 1000u;
    for (int i = 0; i < oracle_n; i++) lru_seq[i] = (uint64_t)i;
    /* Wait: reset cleanly and track exact LRU oracle */
    d2_fp_index_reset(&idx);
    oracle_n = 0;
    tick = 1u;
    for (int step = 0; step < 25000; step++) {
        D2FpPsoKey k;
        if ((next_u32() & 3u) == 0u && oracle_n > 0) {
            /* Lookup existing key */
            int pick = (int)(next_u32() % (uint32_t)oracle_n);
            k = oracle[pick];
            int got = d2_fp_index_find(&idx, &k);
            assert(got == pick);
            lru_seq[pick] = tick++;
        } else {
            /* New key (mix colliding keys and random keys) */
            if ((next_u32() & 7u) == 0u)
                k = colliding[next_u32() % 20u];
            else
                k = make_random_fp_key(2000000u + (next_u32() % 900u));

            int expected = -1;
            for (int i = 0; i < oracle_n; i++) {
                if (d2_fp_key_equal(&oracle[i], &k)) { expected = i; break; }
            }
            int got = d2_fp_index_find(&idx, &k);
            assert(got == expected);
            if (got >= 0) {
                lru_seq[got] = tick++;
            } else {
                int ev_got = 0;
                int ins = d2_fp_index_insert(&idx, &k, &ev_got);
                if (oracle_n < (int)D2_FP_CACHE_CAP) {
                    assert(ev_got == 0 && ins == oracle_n);
                    oracle[oracle_n] = k;
                    lru_seq[oracle_n] = tick++;
                    oracle_n++;
                } else {
                    /* Oracle LRU victim */
                    int exp_victim = 0;
                    for (int i = 1; i < (int)D2_FP_CACHE_CAP; i++) {
                        if (lru_seq[i] < lru_seq[exp_victim]) exp_victim = i;
                    }
                    assert(ev_got == 1 && ins == exp_victim);
                    oracle[exp_victim] = k;
                    lru_seq[exp_victim] = tick++;
                }
            }
        }
        if ((step % 500) == 499) {
            /* Periodically check bucket count & non-corruption without disturbing LRU */
            int bcnt = 0;
            for (uint32_t b = 0; b < D2_FP_INDEX_SLOTS; b++)
                if (idx.buckets[b].entry_plus_1) bcnt++;
            assert(bcnt == oracle_n);
        }
    }
    assert_fp_index_invariants(&idx, oracle, oracle_n);

    /* 5. Device reset */
    d2_fp_index_reset(&idx);
    assert(idx.count == 0);
    assert(d2_fp_index_find(&idx, &oracle[0]) == -1);
    printf("  PASS\n");
}

/* ---------------------------------------------------------------------------
 * Test 3: vp_tex O(1) 256-slot index: frames, saturation, evictions, resets
 * ------------------------------------------------------------------------- */
typedef struct {
    int has_res;
    D2TexKey key;
    uint32_t csum;
    int used;
} SimTexSlot;

static void assert_tex_invariants(D2TexCacheIndex* idx, const SimTexSlot* sim)
{
    int bcnt = 0, acnt = 0;
    for (uint32_t b = 0; b < D2_TEX_INDEX_SLOTS; b++) {
        if (idx->buckets[b].entry_plus_1 != 0u) {
            int s = (int)(idx->buckets[b].entry_plus_1 - 1u);
            assert(s >= 0 && s < (int)D2_TEX_CACHE_CAP);
            assert(idx->active[s]);
            bcnt++;
        }
    }
    for (int i = 0; i < (int)D2_TEX_CACHE_CAP; i++) {
        assert(idx->used[i] == (uint8_t)sim[i].used);
        if (sim[i].has_res) {
            assert(idx->active[i]);
            assert(d2_tex_key_equal(&idx->keys[i], &sim[i].key));
            assert(d2_tex_index_find(idx, &sim[i].key) == i);
            acnt++;
        } else {
            assert(!idx->active[i]);
        }
    }
    assert(bcnt == acnt);
}

static D2TexKey make_tex_key(uint32_t id)
{
    return d2_tex_make_key(
        0x40100000u + id * 0x1000u,
        64u << (id & 3u),
        64u << ((id >> 2) & 3u),
        0x85u + (id % 5u),
        256u + (id & 7u) * 64u,
        (int)(id % 7u == 0u));
}

static void test_tex_cache_index(void)
{
    printf("[TEST] vp_tex 256-slot open-addressed index...\n");
    D2TexCacheIndex idx;
    SimTexSlot sim[D2_TEX_CACHE_CAP];
    d2_tex_index_reset(&idx);
    memset(sim, 0, sizeof(sim));

    /* Find 16 colliding texture keys in bucket 77 */
    D2TexKey col_tex[16];
    int ncol = 0;
    uint32_t pid = 1u;
    while (ncol < 16) {
        D2TexKey k = make_tex_key(pid++);
        if ((d2_tex_key_hash(&k) & D2_TEX_INDEX_MASK) == 77u)
            col_tex[ncol++] = k;
    }

    /* Frame 0: fill all 96 slots and verify saturation returns -1 */
    d2_tex_index_begin_frame(&idx);
    for (int i = 0; i < (int)D2_TEX_CACHE_CAP; i++) {
        D2TexKey k = (i < 16) ? col_tex[i] : make_tex_key(5000u + (uint32_t)i);
        assert(d2_tex_index_find(&idx, &k) == -1);
        int free_slot = d2_tex_index_pick_free(&idx);
        assert(free_slot == i);
        sim[free_slot].has_res = 1;
        sim[free_slot].key = k;
        sim[free_slot].csum = 0xA000u + (uint32_t)i;
        sim[free_slot].used = 1;
        d2_tex_index_set_slot(&idx, free_slot, &k);
        d2_tex_index_mark_used(&idx, free_slot);
    }
    assert(d2_tex_index_pick_free(&idx) == -1); /* saturated */
    assert_tex_invariants(&idx, sim);

    /* Frame 1: begin_frame resets used=0 while preserving all 96 slots */
    d2_tex_index_begin_frame(&idx);
    for (int i = 0; i < (int)D2_TEX_CACHE_CAP; i++) sim[i].used = 0;
    assert_tex_invariants(&idx, sim);

    /* Touch slot 0 (col_tex[0]), then insert new texture -> must NOT evict slot 0 */
    assert(d2_tex_index_find(&idx, &col_tex[0]) == 0);
    sim[0].used = 1;
    d2_tex_index_mark_used(&idx, 0);

    D2TexKey extra = make_tex_key(900000u);
    int victim = d2_tex_index_pick_free(&idx);
    assert(victim == 1); /* slot 1 was oldest untouched */
    sim[victim].has_res = 1;
    sim[victim].key = extra;
    sim[victim].csum = 0xBEEFu;
    sim[victim].used = 1;
    d2_tex_index_set_slot(&idx, victim, &extra);
    d2_tex_index_mark_used(&idx, victim);
    assert(d2_tex_index_find(&idx, &col_tex[1]) == -1);
    assert(d2_tex_index_find(&idx, &col_tex[0]) == 0);
    assert(d2_tex_index_find(&idx, &extra) == 1);
    assert_tex_invariants(&idx, sim);

    /* Simulate dim-change release + allocation failure on slot 2 */
    d2_tex_index_remove_slot(&idx, 2);
    sim[2].has_res = 0;
    sim[2].used = 0;
    assert(d2_tex_index_find(&idx, &col_tex[2]) == -1);
    assert_tex_invariants(&idx, sim);

    /* Multi-frame stress with 180-texture working set + colliding textures */
    for (int frame = 2; frame < 400; frame++) {
        d2_tex_index_begin_frame(&idx);
        for (int i = 0; i < (int)D2_TEX_CACHE_CAP; i++) sim[i].used = 0;

        for (int draw = 0; draw < 120; draw++) {
            D2TexKey k = (draw < 16 && (frame & 1))
                ? col_tex[draw]
                : make_tex_key(10000u + ((uint32_t)(frame * 17 + draw) % 180u));

            int oracle_hit = -1;
            for (int i = 0; i < (int)D2_TEX_CACHE_CAP; i++) {
                if (sim[i].has_res && d2_tex_key_equal(&sim[i].key, &k)) {
                    oracle_hit = i;
                    break;
                }
            }
            int got = d2_tex_index_find(&idx, &k);
            assert(got == oracle_hit);
            if (got >= 0) {
                sim[got].used = 1;
                d2_tex_index_mark_used(&idx, got);
            } else {
                int fs = d2_tex_index_pick_free(&idx);
                int has_unused = 0;
                for (int i = 0; i < (int)D2_TEX_CACHE_CAP; i++)
                    if (!sim[i].used) has_unused = 1;
                if (!has_unused) {
                    assert(fs == -1);
                } else {
                    assert(fs >= 0 && !sim[fs].used);
                    if (sim[fs].has_res && (next_u32() & 1u)) {
                        d2_tex_index_remove_slot(&idx, fs);
                        sim[fs].has_res = 0;
                    }
                    /* Simulate occasional failed CreateCommittedResource after remove */
                    if ((next_u32() & 31u) == 0u && !sim[fs].has_res) {
                        continue;
                    }
                    sim[fs].has_res = 1;
                    sim[fs].key = k;
                    sim[fs].used = 1;
                    d2_tex_index_set_slot(&idx, fs, &k);
                    d2_tex_index_mark_used(&idx, fs);
                }
            }
        }
        assert_tex_invariants(&idx, sim);
    }

    /* Device reset */
    d2_tex_index_reset(&idx);
    assert(d2_tex_index_find(&idx, &extra) == -1);
    assert(d2_tex_index_pick_free(&idx) == 0);
    printf("  PASS\n");
}

/* ---------------------------------------------------------------------------
 * Test 5: D2FpCacheIndex evictions epoch counter
 * ------------------------------------------------------------------------- */
static void test_fp_index_eviction_epoch(void)
{
    printf("[TEST] FP PSO index eviction epoch tracking...\n");
    D2FpCacheIndex idx;
    d2_fp_index_reset(&idx);
    assert(idx.evictions == 0u);

    D2FpPsoKey keys[D2_FP_CACHE_CAP + 50u];
    for (uint32_t i = 0; i < D2_FP_CACHE_CAP + 50u; i++)
        keys[i] = make_random_fp_key(0x770000u + i);

    for (uint32_t i = 0; i < D2_FP_CACHE_CAP; i++) {
        int ev = -1;
        int s = d2_fp_index_insert(&idx, &keys[i], &ev);
        assert(ev == 0 && s == (int)i);
        assert(idx.evictions == 0u);
    }

    /* Duplicate hit/insert must not increment evictions */
    assert(d2_fp_index_find(&idx, &keys[0]) == 0);
    int dup_ev = -1;
    assert(d2_fp_index_insert(&idx, &keys[0], &dup_ev) == 0);
    assert(dup_ev == 0 && idx.evictions == 0u);

    /* Next insert evicts slot 1 (since slot 0 was moved to MRU) */
    int ev = 0;
    int s = d2_fp_index_insert(&idx, &keys[D2_FP_CACHE_CAP], &ev);
    assert(ev == 1 && s == 1 && idx.evictions == 1u);

    for (uint32_t i = 1u; i < 50u; i++) {
        int e = 0;
        (void)d2_fp_index_insert(&idx, &keys[D2_FP_CACHE_CAP + i], &e);
        assert(e == 1);
    }
    assert(idx.evictions == 50u);

    d2_fp_index_reset(&idx);
    assert(idx.evictions == 0u && idx.count == 0);
    printf("  PASS\n");
}

/* ---------------------------------------------------------------------------
 * Test 6: rsx_swizzle_offset Morton LUT vs exact bit-by-bit reference
 * ------------------------------------------------------------------------- */
static u32 ref_swizzle_offset(u32 x, u32 y, u32 log2w, u32 log2h)
{
    u32 off = 0u, bit = 0u;
    u32 m = log2w < log2h ? log2w : log2h;
    for (u32 i = 0u; i < m; i++) {
        off |= ((x >> i) & 1u) << (bit++);
        off |= ((y >> i) & 1u) << (bit++);
    }
    for (u32 i = m; i < log2w; i++) off |= ((x >> i) & 1u) << (bit++);
    for (u32 i = m; i < log2h; i++) off |= ((y >> i) & 1u) << (bit++);
    return off;
}

static void test_morton_swizzle_lut(void)
{
    printf("[TEST] Morton LUT rsx_swizzle_offset exact parity...\n");

    /* Exhaustive check over all small POT dimensions up to 64x64 */
    for (u32 lw = 0u; lw <= 6u; lw++) {
        for (u32 lh = 0u; lh <= 6u; lh++) {
            u32 w = 1u << lw;
            u32 h = 1u << lh;
            for (u32 y = 0u; y < h; y++) {
                for (u32 x = 0u; x < w; x++) {
                    u32 got = rsx_swizzle_offset(x, y, lw, lh);
                    u32 exp = ref_swizzle_offset(x, y, lw, lh);
                    assert(got == exp);
                }
            }
        }
    }

    /* Non-square POTs and larger m (8 < m <= 15) + out-of-range bit masking */
    static const u32 dims[][2] = {
        {8u, 8u}, {9u, 9u}, {10u, 8u}, {8u, 10u},
        {12u, 0u}, {0u, 12u}, {11u, 4u}, {4u, 11u},
        {15u, 15u}, {16u, 15u}, {15u, 16u}
    };
    for (size_t di = 0; di < sizeof(dims) / sizeof(dims[0]); di++) {
        u32 lw = dims[di][0], lh = dims[di][1];
        u32 mx = (lw < 31u) ? ((1u << lw) - 1u) : 0x7FFFFFFFu;
        u32 my = (lh < 31u) ? ((1u << lh) - 1u) : 0x7FFFFFFFu;
        /* Edges + high bits (note: high bits above log2w/log2h are unmasked in ref
         * only if callers keep x < 2^lw; verify exact match on valid coordinates
         * and identical tail handling). */
        const u32 samples_x[] = {0u, 1u, 2u, 0xFFu, 0x100u, 0x155u, 0xAAAu, mx & 0xFFFFu, mx};
        const u32 samples_y[] = {0u, 1u, 3u, 0xFEu, 0x100u, 0x2AAu, 0x555u, my & 0xFFFFu, my};
        for (size_t xi = 0; xi < sizeof(samples_x) / sizeof(samples_x[0]); xi++) {
            for (size_t yi = 0; yi < sizeof(samples_y) / sizeof(samples_y[0]); yi++) {
                u32 x = samples_x[xi] & mx;
                u32 y = samples_y[yi] & my;
                assert(rsx_swizzle_offset(x, y, lw, lh) == ref_swizzle_offset(x, y, lw, lh));
            }
        }
        for (int s = 0; s < 500; s++) {
            u32 x = next_u32() & mx;
            u32 y = next_u32() & my;
            assert(rsx_swizzle_offset(x, y, lw, lh) == ref_swizzle_offset(x, y, lw, lh));
        }
    }
    printf("  PASS\n");
}

/* ---------------------------------------------------------------------------
 * Test 7: d2_fp_const_cache (64 LRU, 128-slot index, in-place mutation)
 * ------------------------------------------------------------------------- */
static void build_test_fp_program(uint8_t* dst, size_t cap, int num_consts,
                                  int include_branch_fake_const, uint32_t seed)
{
    memset(dst, 0xCC, cap);
    u32 off = 0u;
    if (include_branch_fake_const) {
        write_fp_word(dst + off + 0u, (0x02u << 24));
        write_fp_word(dst + off + 4u, 2u); /* CONST bit, ignored on branch */
        write_fp_word(dst + off + 8u, 0x80000000u);
        write_fp_word(dst + off + 12u, seed);
        off += 16u;
    }
    if (num_consts == 0) {
        write_fp_word(dst + off + 0u, (0x01u << 24) | 1u); /* END */
        write_fp_word(dst + off + 4u, 0u);
        write_fp_word(dst + off + 8u, 0u);
        write_fp_word(dst + off + 12u, 0u);
        return;
    }
    for (int k = 0; k < num_consts; k++) {
        u32 end_bit = (k == num_consts - 1) ? 1u : 0u;
        write_fp_word(dst + off + 0u, (0x01u << 24) | end_bit);
        write_fp_word(dst + off + 4u, (k % 3 == 0) ? 2u : 0u);
        write_fp_word(dst + off + 8u, (k % 3 == 1) ? 2u : 1u);
        write_fp_word(dst + off + 12u, (k % 3 == 2) ? 2u : 0u);
        off += 16u;
        for (u32 c = 0u; c < 4u; c++) {
            float fv = (float)(seed + (uint32_t)k * 7u + c) * 0.125f;
            uint32_t bits = 0u;
            memcpy(&bits, &fv, 4u);
            write_fp_word(dst + off + c * 4u, bits);
        }
        off += 16u;
    }
}

static void assert_fp_const_parity(const uint8_t* ucode, u32 max_bytes, int max_out)
{
    float ref_out[64 * 4], got_out[64 * 4];
    for (size_t i = 0; i < 64u * 4u; i++) {
        ref_out[i] = -999.0f;
        got_out[i] = -999.0f;
    }
    int ref_n = rsx_fp_extract_consts(ucode, max_bytes, ref_out, max_out);
    int got_n = d2_fp_extract_consts_cached(ucode, max_bytes, got_out, max_out);
    assert(got_n == ref_n);
    assert(memcmp(got_out, ref_out, (size_t)(max_out * 4) * sizeof(float)) == 0);
}

static void test_fp_const_cache(void)
{
    printf("[TEST] d2_fp_const_cache 64-entry LRU + 128-slot index + mutation...\n");
    setenv("D2_FP_CONST_CACHE", "1", 1);
    unsetenv("FP_KDBG");
    d2_fp_const_cache_reset();

    uint8_t (*progs)[512] = (uint8_t (*)[512])calloc(100u, 512u);
    assert(progs != NULL);

    for (int i = 0; i < 100; i++)
        build_test_fp_program(progs[i], 512u, 1 + (i % 5), (i % 7) == 0, (uint32_t)(i + 1));

    uint64_t hits_before = d2_fpk_hits;
    /* 1. Fill all 64 slots */
    for (int i = 0; i < (int)D2_FPK_CACHE_CAP; i++)
        assert_fp_const_parity(progs[i], 512u, 64);
    assert(d2_fpk_hits == hits_before);
    assert(s_d2_fpk_count == (int)D2_FPK_CACHE_CAP);

    /* 2. Touch progs[0] (hit -> moves to MRU; progs[1] is now LRU tail) */
    hits_before = d2_fpk_hits;
    assert_fp_const_parity(progs[0], 512u, 64);
    assert(d2_fpk_hits == hits_before + 1u);

    /* Modify irrelevant trailing byte after FP_END on progs[0] -> still hits! */
    progs[0][500] ^= 0x5Au;
    hits_before = d2_fpk_hits;
    assert_fp_const_parity(progs[0], 512u, 64);
    assert(d2_fpk_hits == hits_before + 1u);

    /* 3. Insert progs[64..99] -> evicts progs[1..36] via LRU */
    hits_before = d2_fpk_hits;
    for (int i = (int)D2_FPK_CACHE_CAP; i < 100; i++)
        assert_fp_const_parity(progs[i], 512u, 64);
    assert(d2_fpk_hits == hits_before);

    /* progs[1] must have been evicted (miss); progs[64..99] must all hit */
    hits_before = d2_fpk_hits;
    assert_fp_const_parity(progs[1], 512u, 64);
    assert(d2_fpk_hits == hits_before); /* miss re-inserted progs[1], evicting progs[37] */

    hits_before = d2_fpk_hits;
    for (int i = 64; i < 100; i++)
        assert_fp_const_parity(progs[i], 512u, 64);
    assert(d2_fpk_hits == hits_before + 36u);

    /* 4. In-place inline constant mutation MUST miss & return new values */
    progs[80][16] ^= 0x01u;
    hits_before = d2_fpk_hits;
    assert_fp_const_parity(progs[80], 512u, 64);
    assert(d2_fpk_hits == hits_before);
    hits_before = d2_fpk_hits;
    assert_fp_const_parity(progs[80], 512u, 64);
    assert(d2_fpk_hits == hits_before + 1u);

    /* 5. Edge cases: 0 consts, max_out < written, tight max_bytes */
    uint8_t edge[512];
    build_test_fp_program(edge, sizeof(edge), 0, 1, 99u);
    assert_fp_const_parity(edge, 512u, 64);
    assert_fp_const_parity(progs[90], 512u, 2);
    assert_fp_const_parity(progs[90], 32u, 64);

    /* 6. Randomized stress with in-place mutations */
    for (int step = 0; step < 5000; step++) {
        int id = (int)(next_u32() % 100u);
        if ((next_u32() & 15u) == 0u) {
            progs[id][17] ^= (uint8_t)(next_u32() | 1u);
        }
        assert_fp_const_parity(progs[id], 512u, 64);
    }

    d2_fp_const_cache_reset();
    assert(s_d2_fpk_count == 0);
    free(progs);
    unsetenv("D2_FP_CONST_CACHE");
    printf("  PASS\n");
}

/* ---------------------------------------------------------------------------
 * Test 8: d2_constant_pair (16-entry multi-slot LRU, discard, frame/parity)
 * ------------------------------------------------------------------------- */
static void make_pair_payload(uint8_t* vp_buf, uint8_t* tail_buf, uint8_t* fp_buf, uint32_t id)
{
    memset(vp_buf, 0, RSX_MAX_VERTEX_CONSTANTS * 16u);
    for (uint32_t i = 0u; i < RSX_MAX_VERTEX_CONSTANTS * 16u; i += 64u) {
        uint32_t w = id * 0x9E3779B1u + i;
        memcpy(vp_buf + i, &w, 4u);
    }
    float tail[8] = {
        1.0f + (float)id * 0.25f, 2.0f + (float)id * 0.5f, 0.0f, 0.0f,
        10.0f + (float)id, 20.0f + (float)id, 0.0f, 1.0f
    };
    memcpy(tail_buf, tail, 32u);
    memset(fp_buf, 0, D2_PAIR_FP_BYTES);
    for (uint32_t i = 0u; i < D2_PAIR_FP_BYTES; i += 32u) {
        uint32_t w = (id ^ 0xA5A5u) * 2654435761u + i;
        memcpy(fp_buf + i, &w, 4u);
    }
}

static void assert_pair_gpu_bytes(int parity, u32 cb_slot,
                                  const uint8_t* vp, const uint8_t* tail, const uint8_t* fp,
                                  const uint8_t* cb_mem, const uint8_t* fpcb_mem)
{
    size_t vp_off = ((size_t)parity * MAX_DRAWS + cb_slot) * VP_CB_STRIDE;
    size_t fp_off = ((size_t)parity * MAX_DRAWS + cb_slot) * VP_FPCB_STRIDE;
    assert(memcmp(cb_mem + vp_off, vp, RSX_MAX_VERTEX_CONSTANTS * 16u) == 0);
    assert(memcmp(cb_mem + vp_off + RSX_MAX_VERTEX_CONSTANTS * 16u, tail, 32u) == 0);
    assert(memcmp(fpcb_mem + fp_off, fp, D2_PAIR_FP_BYTES) == 0);
    const float* tf = (const float*)tail;
    assert(d2_vp_cb_xform[parity][cb_slot][0] == tf[0]);
    assert(d2_vp_cb_xform[parity][cb_slot][1] == tf[1]);
    assert(d2_vp_cb_xform[parity][cb_slot][2] == tf[4]);
    assert(d2_vp_cb_xform[parity][cb_slot][3] == tf[5]);
}

static void test_constant_pair_cache(void)
{
    printf("[TEST] d2_constant_pair 16-entry multi-slot LRU + discard + resets...\n");
    setenv("D2_CONSTANT_PAIR", "1", 1);
    unsetenv("D2_UI_DIAG");
    unsetenv("D2_UI_RAW");
    unsetenv("VP_MVP");
    unsetenv("RSX_DBG_CB");

    uint8_t* cb_mem = (uint8_t*)malloc((size_t)(2u * MAX_DRAWS) * VP_CB_STRIDE);
    uint8_t* fpcb_mem = (uint8_t*)malloc((size_t)(2u * MAX_DRAWS) * VP_FPCB_STRIDE);
    assert(cb_mem && fpcb_mem);
    memset(cb_mem, 0xA5, (size_t)(2u * MAX_DRAWS) * VP_CB_STRIDE);
    memset(fpcb_mem, 0xA5, (size_t)(2u * MAX_DRAWS) * VP_FPCB_STRIDE);

    d2_pair_reset_for_test();
    s_d3d.vp_parity = 0;
    s_d3d.vp_cb_mapped = cb_mem;
    s_d3d.vp_fpcb_mapped = fpcb_mem;

    uint8_t (*vps)[RSX_MAX_VERTEX_CONSTANTS * 16u] = malloc(40u * RSX_MAX_VERTEX_CONSTANTS * 16u);
    uint8_t (*tails)[32] = malloc(40u * 32u);
    uint8_t (*fps)[D2_PAIR_FP_BYTES] = malloc(40u * D2_PAIR_FP_BYTES);
    uint8_t* contig_vp = malloc(D2_PAIR_VP_BYTES);
    assert(vps && tails && fps && contig_vp);
    for (uint32_t i = 0u; i < 40u; i++)
        make_pair_payload(vps[i], tails[i], fps[i], i + 1u);

    /* 1. Multi-slot alternating reuse (5 pairs across slots 0..24) */
    for (u32 s = 0u; s < 5u; s++) {
        d2_upload_constant_pair_parts(s, vps[s], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                      tails[s], 32u, fps[s], D2_PAIR_FP_BYTES);
        assert(s_d3d.draws[s].cb_slot == s);
        assert_pair_gpu_bytes(0, s, vps[s], tails[s], fps[s], cb_mem, fpcb_mem);
    }
    assert(d2_pair_hits == 0u);

    static const u32 alt_order[20] = {4,0,3,1,2, 1,4,2,0,3, 3,2,1,4,0, 2,3,4,1,0};
    for (u32 k = 0u; k < 20u; k++) {
        u32 slot = 5u + k;
        u32 id = alt_order[k];
        d2_upload_constant_pair_parts(slot, vps[id], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                      tails[id], 32u, fps[id], D2_PAIR_FP_BYTES);
        assert(s_d3d.draws[slot].cb_slot == id);
        /* Verify GPU slot bytes for `slot` were NOT overwritten */
        assert(cb_mem[((size_t)0 * MAX_DRAWS + slot) * VP_CB_STRIDE] == 0xA5u);
        assert(fpcb_mem[((size_t)0 * MAX_DRAWS + slot) * VP_FPCB_STRIDE] == 0xA5u);
        assert_pair_gpu_bytes(0, s_d3d.draws[slot].cb_slot, vps[id], tails[id], fps[id], cb_mem, fpcb_mem);
    }
    assert(d2_pair_hits == 20u);

    /* Contiguous d2_upload_constant_pair matches earlier split upload */
    memcpy(contig_vp, vps[2], RSX_MAX_VERTEX_CONSTANTS * 16u);
    memcpy(contig_vp + RSX_MAX_VERTEX_CONSTANTS * 16u, tails[2], 32u);
    d2_upload_constant_pair(25u, contig_vp, D2_PAIR_VP_BYTES, fps[2], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == 21u && s_d3d.draws[25u].cb_slot == 2u);

    /* 2. Parity switch -> invalidates and fills 16 entries for LRU stress */
    s_d3d.vp_parity = 1;
    uint64_t hits_before = d2_pair_hits;
    for (u32 i = 0u; i < 16u; i++) {
        d2_upload_constant_pair_parts(i, vps[i], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                      tails[i], 32u, fps[i], D2_PAIR_FP_BYTES);
        assert(s_d3d.draws[i].cb_slot == i);
    }
    assert(d2_pair_hits == hits_before);

    /* Touch pair 0 at slot 16 -> hits & moves pair 0 to MRU (pair 1 becomes LRU tail) */
    d2_upload_constant_pair_parts(16u, vps[0], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[0], 32u, fps[0], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before + 1u && s_d3d.draws[16u].cb_slot == 0u);

    /* Insert 15 more distinct pairs (slots 17..31 -> pairs 16..30): evicts pairs 1..15,
     * keeping pair 0 alive at LRU tail! */
    hits_before = d2_pair_hits;
    for (u32 k = 0u; k < 15u; k++) {
        u32 slot = 17u + k;
        u32 id = 16u + k;
        d2_upload_constant_pair_parts(slot, vps[id], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                      tails[id], 32u, fps[id], D2_PAIR_FP_BYTES);
        assert(s_d3d.draws[slot].cb_slot == slot);
    }
    assert(d2_pair_hits == hits_before);

    /* Pair 0 MUST still hit; pair 1 MUST have been evicted (miss) */
    d2_upload_constant_pair_parts(32u, vps[0], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[0], 32u, fps[0], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before + 1u && s_d3d.draws[32u].cb_slot == 0u);

    hits_before = d2_pair_hits;
    d2_upload_constant_pair_parts(33u, vps[1], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[1], 32u, fps[1], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before && s_d3d.draws[33u].cb_slot == 33u);

    /* 3. 1-byte mutation in VP head, VP tail, and FP MUST miss */
    vps[29][123] ^= 1u;
    hits_before = d2_pair_hits;
    d2_upload_constant_pair_parts(34u, vps[29], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[29], 32u, fps[29], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before && s_d3d.draws[34u].cb_slot == 34u);

    tails[28][31] ^= 1u;
    d2_upload_constant_pair_parts(35u, vps[28], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[28], 32u, fps[28], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before && s_d3d.draws[35u].cb_slot == 35u);

    fps[27][D2_PAIR_FP_BYTES - 1u] ^= 1u;
    d2_upload_constant_pair_parts(36u, vps[27], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[27], 32u, fps[27], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before && s_d3d.draws[36u].cb_slot == 36u);

    /* 4. Discard slot invalidation: owner >= slot discarded, owner < slot preserved */
    d2_upload_constant_pair_parts(37u, vps[31], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[31], 32u, fps[31], D2_PAIR_FP_BYTES);
    d2_pair_discard_slot(37u);
    /* Re-upload pair 31 at discarded slot 37 -> MUST miss (owner 37 >= 37 discarded) */
    hits_before = d2_pair_hits;
    d2_upload_constant_pair_parts(37u, vps[31], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[31], 32u, fps[31], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before && s_d3d.draws[37u].cb_slot == 37u);

    d2_pair_discard_slot(37u);
    /* Re-upload surviving pair 0 (owner 0 < 37) at discarded slot 37 -> MUST hit owner 0 */
    hits_before = d2_pair_hits;
    d2_upload_constant_pair_parts(37u, vps[0], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[0], 32u, fps[0], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before + 1u && s_d3d.draws[37u].cb_slot == 0u);

    /* 5. New frame (slot <= last_record without discard) + mapping pointer change */
    hits_before = d2_pair_hits;
    d2_upload_constant_pair_parts(0u, vps[0], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[0], 32u, fps[0], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before && s_d3d.draws[0u].cb_slot == 0u);

    s_d3d.vp_fpcb_mapped = fpcb_mem + VP_FPCB_STRIDE;
    d2_upload_constant_pair_parts(1u, vps[0], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[0], 32u, fps[0], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before && s_d3d.draws[1u].cb_slot == 1u);

    s_d3d.vp_cb_mapped = cb_mem + VP_CB_STRIDE;
    d2_upload_constant_pair_parts(2u, vps[0], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tails[0], 32u, fps[0], D2_PAIR_FP_BYTES);
    assert(d2_pair_hits == hits_before && s_d3d.draws[2u].cb_slot == 2u);

    /* 6. Contiguous-vs-split hash invariance check across tail boundaries */
    for (u32 i = 0u; i < 8u; i++) {
        memcpy(contig_vp, vps[i], RSX_MAX_VERTEX_CONSTANTS * 16u);
        memcpy(contig_vp + RSX_MAX_VERTEX_CONSTANTS * 16u, tails[i], 32u);
        uint64_t h_split = d2_pair_hash_all(vps[i], RSX_MAX_VERTEX_CONSTANTS * 16u,
                                            tails[i], 32u, fps[i], D2_PAIR_FP_BYTES);
        uint64_t h_contig = d2_pair_hash_all(contig_vp, D2_PAIR_VP_BYTES,
                                             NULL, 0u, fps[i], D2_PAIR_FP_BYTES);
        assert(h_split == h_contig);
        /* Also test odd chunking via streaming hasher */
        D2PairHasher hs;
        d2_pair_hash_init(&hs);
        d2_pair_hash_feed_bytes(&hs, contig_vp, 777u);
        d2_pair_hash_feed_bytes(&hs, contig_vp + 777u, D2_PAIR_VP_BYTES - 777u);
        d2_pair_hash_feed_bytes(&hs, fps[i], 131u);
        d2_pair_hash_feed_bytes(&hs, fps[i] + 131u, D2_PAIR_FP_BYTES - 131u);
        assert(d2_pair_hash_finish(&hs) == h_contig);
    }

    d2_pair_reset_for_test();
    free(contig_vp);
    free(fps);
    free(tails);
    free(vps);
    free(fpcb_mem);
    free(cb_mem);
    unsetenv("D2_CONSTANT_PAIR");
    printf("  PASS\n");
}

static void test_constant_pair_vp_dirty_fastpath(void)
{
    printf("[TEST] d2_constant_pair vertex_constants_dirty VP hash/gen fast-path...\n");
    setenv("D2_CONSTANT_PAIR", "1", 1);
    unsetenv("D2_UI_DIAG");
    unsetenv("D2_UI_RAW");
    unsetenv("VP_MVP");
    unsetenv("RSX_DBG_CB");

    uint8_t* cb_mem = (uint8_t*)calloc((size_t)(2u * MAX_DRAWS), VP_CB_STRIDE);
    uint8_t* fpcb_mem = (uint8_t*)calloc((size_t)(2u * MAX_DRAWS), VP_FPCB_STRIDE);
    rsx_state* st_a = (rsx_state*)calloc(1u, sizeof(rsx_state));
    rsx_state* st_b = (rsx_state*)calloc(1u, sizeof(rsx_state));
    assert(cb_mem && fpcb_mem && st_a && st_b);

    d2_pair_reset_for_test();
    s_d3d.vp_parity = 0;
    s_d3d.vp_cb_mapped = cb_mem;
    s_d3d.vp_fpcb_mapped = fpcb_mem;
    s_d3d.current_rsx_state = st_a;

    uint8_t tail_a[32], tail_b[32], fp_a[D2_PAIR_FP_BYTES], fp_b[D2_PAIR_FP_BYTES];
    uint8_t dummy_vp[RSX_MAX_VERTEX_CONSTANTS * 16u];
    make_pair_payload((uint8_t*)st_a->vertex_constants, tail_a, fp_a, 11u);
    make_pair_payload(dummy_vp, tail_b, fp_b, 22u);
    memcpy(st_b->vertex_constants, st_a->vertex_constants, sizeof(st_a->vertex_constants));
    st_a->vertex_constants_dirty = 1;
    st_b->vertex_constants_dirty = 1;

    /* Draw 0 (miss): hashes VP A, caches vp_saved, clears st_a->vertex_constants_dirty */
    d2_upload_constant_pair_parts(0u, st_a->vertex_constants, RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tail_a, sizeof(tail_a), fp_a, sizeof(fp_a));
    assert(s_d3d.draws[0].cb_slot == 0u);
    assert(st_a->vertex_constants_dirty == 0);
    assert(s_d2_pair_cache.vp_state_ptr == st_a);
    uint64_t gen0 = s_d2_pair_cache.vp_gen;
    assert(gen0 != 0u);

    /* Draw 1: clean VP A + FP B (miss on FP) -> must REUSE vp_saved & keep gen0 */
    d2_upload_constant_pair_parts(1u, st_a->vertex_constants, RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tail_a, sizeof(tail_a), fp_b, sizeof(fp_b));
    assert(s_d3d.draws[1].cb_slot == 1u);
    assert(s_d2_pair_cache.vp_gen == gen0);

    /* Draw 2: clean VP A + FP A (hit!) -> skips VP hash & skips VP memeq */
    uint64_t hits_before = d2_pair_hits;
    d2_upload_constant_pair_parts(2u, st_a->vertex_constants, RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tail_a, sizeof(tail_a), fp_a, sizeof(fp_a));
    assert(d2_pair_hits == hits_before + 1u);
    assert(s_d3d.draws[2].cb_slot == 0u);
    assert(s_d2_pair_cache.vp_gen == gen0);

    /* Mutate 1 float inside VP A with dirty=1 (State B) -> must miss & bump vp_gen */
    ((uint8_t*)st_a->vertex_constants)[5123] ^= 0x01u;
    st_a->vertex_constants_dirty = 1;
    hits_before = d2_pair_hits;
    d2_upload_constant_pair_parts(3u, st_a->vertex_constants, RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tail_a, sizeof(tail_a), fp_a, sizeof(fp_a));
    assert(d2_pair_hits == hits_before);
    assert(s_d3d.draws[3].cb_slot == 3u);
    assert(st_a->vertex_constants_dirty == 0);
    uint64_t gen1 = s_d2_pair_cache.vp_gen;
    assert(gen1 > gen0);

    /* Mutate back to original VP A with dirty=1 (toggle A -> B -> A):
     * vp_gen changes again, yet d2_pair_memeq fallback must still hit slot 0! */
    ((uint8_t*)st_a->vertex_constants)[5123] ^= 0x01u;
    st_a->vertex_constants_dirty = 1;
    hits_before = d2_pair_hits;
    d2_upload_constant_pair_parts(4u, st_a->vertex_constants, RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tail_a, sizeof(tail_a), fp_a, sizeof(fp_a));
    assert(d2_pair_hits == hits_before + 1u);
    assert(s_d3d.draws[4].cb_slot == 0u);
    assert(s_d2_pair_cache.vp_gen > gen1);

    /* Switching current_rsx_state pointer forces fresh hash even if st_b->dirty == 0 */
    st_b->vertex_constants_dirty = 0;
    ((uint8_t*)st_b->vertex_constants)[123] ^= 0x40u;
    s_d3d.current_rsx_state = st_b;
    hits_before = d2_pair_hits;
    d2_upload_constant_pair_parts(5u, st_b->vertex_constants, RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tail_a, sizeof(tail_a), fp_a, sizeof(fp_a));
    assert(d2_pair_hits == hits_before);
    assert(s_d3d.draws[5].cb_slot == 5u);
    assert(s_d2_pair_cache.vp_state_ptr == st_b);

    /* Frame reset invalidates vp_state_ptr even when st_b->dirty == 0 */
    s_d3d.vp_parity = 1;
    st_b->vertex_constants_dirty = 0;
    d2_upload_constant_pair_parts(0u, st_b->vertex_constants, RSX_MAX_VERTEX_CONSTANTS * 16u,
                                  tail_a, sizeof(tail_a), fp_a, sizeof(fp_a));
    assert(s_d3d.draws[0].cb_slot == 0u);
    assert(s_d2_pair_cache.vp_state_ptr == st_b);

    d2_pair_reset_for_test();
    s_d3d.current_rsx_state = NULL;
    free(st_b);
    free(st_a);
    free(fpcb_mem);
    free(cb_mem);
    unsetenv("D2_CONSTANT_PAIR");
    printf("  PASS\n");
}

int main(void)
{
    test_fp_single_pass_and_memo();
    test_fp_cache_index();
    test_tex_cache_index();
    test_fp_index_eviction_epoch();
    test_morton_swizzle_lut();
    test_fp_const_cache();
    test_constant_pair_cache();
    test_constant_pair_vp_dirty_fastpath();
    printf("ALL RSX CACHE TESTS PASSED\n");
    return 0;
}
