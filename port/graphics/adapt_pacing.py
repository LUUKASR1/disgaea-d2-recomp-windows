"""Remove only the audited presentation gate in an isolated SDK build copy."""
from pathlib import Path
import hashlib
import re
import sys

EXPECTED = 'b1f3fe82a77330e262b3e9a6f9ed263ff89a4442111b7e36ba7797c422027ff7'

def replace(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('GCM timing anchor changed: '+old[:80])
    return text.replace(old,new)

def generate(source, output):
    source=Path(source).resolve()
    data=source.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXPECTED:
        raise RuntimeError('Pinned GCM differs; original SDK preserved')
    text=data.decode()
    text=replace(text,'int cellGcm_take_flip_pending(void)', '''static int d2_gcm_unlimited(void)
{
    static int enabled=-1;
    if(enabled<0){
        const char* value=getenv("D2_GCM_UNLIMITED");
        enabled=value && strcmp(value,"1")==0;
        fprintf(stderr,"[D2GCM] unlimited-presentation=%d vblank-clock=60Hz\\n",enabled);
    }
    return enabled;
}

int cellGcm_take_flip_pending(void)''')
    text=replace(text,'''    if (s_flip_pending && !s_flip_queue[slot].waiting_fifo &&
        s_presented_flip_count == completed &&
        (s_flip_mode != CELL_GCM_DISPLAY_VSYNC ||
         (d2_gcm_immediate() && vblank > s_last_present_vblank) ||
         (vblank >= s_flip_queue[slot].earliest_vblank && vblank > s_last_present_vblank))) {''',
        '''    if (d2_flip_eligible(s_flip_pending,s_flip_queue[slot].waiting_fifo,
        s_presented_flip_count,completed,s_flip_mode==CELL_GCM_DISPLAY_VSYNC,
        vblank,s_flip_queue[slot].earliest_vblank,s_last_present_vblank,
        d2_gcm_immediate(),d2_gcm_unlimited())) {''')
    text=replace(text,'if(d2_ready_wake_enabled()) {','if(d2_ready_wake_enabled() || d2_gcm_unlimited()) {')
    text=replace(text,'''int d2_ready=d2_ready_wake_predicate(s_flip_pending,
            s_flip_queue[d2_slot].waiting_fifo,s_presented_flip_count,d2_completed,
            s_flip_mode==CELL_GCM_DISPLAY_VSYNC,gcm_vblank_at(gcm_vblank_elapsed_ns()),
            s_flip_queue[d2_slot].earliest_vblank,s_last_present_vblank,d2_gcm_immediate());''',
        '''int d2_ready=d2_flip_eligible(s_flip_pending,
            s_flip_queue[d2_slot].waiting_fifo,s_presented_flip_count,d2_completed,
            s_flip_mode==CELL_GCM_DISPLAY_VSYNC,gcm_vblank_at(gcm_vblank_elapsed_ns()),
            s_flip_queue[d2_slot].earliest_vblank,s_last_present_vblank,d2_gcm_immediate(),d2_gcm_unlimited());''')
    text=replace(text,'''    u32 last_put = vm_read32(GCM_CONTROL_GUEST_ADDR);
    while (s_gcm_initialized) {
        u32 raw;
        memcpy(&raw, vm_base + GCM_CONTROL_GUEST_ADDR, sizeof(raw));
        u32 put = ps3_bswap32(raw);
        if (put != last_put) {
            last_put = put;
            cellGcm_fifo_kick();
        }
        u32 wait_ea = s_fifo_wait_label_ea;
        if (wait_ea) {
            memcpy(&raw, vm_base + wait_ea, sizeof(raw));
            if (ps3_bswap32(raw) == s_fifo_wait_label_value)
                cellGcm_fifo_kick();
        } else {
            wait_ea = GCM_CONTROL_GUEST_ADDR;
        }
        if (ps3_poll_enabled) {
            /* Label equality waits may observe transient reference values. */
            if (wait_ea == GCM_CONTROL_GUEST_ADDR)
                ps3_poll_backoff_idle_fixed(40, wait_ea, &raw, sizeof(raw));
            else
                ps3_poll_backoff(40, wait_ea, &raw, sizeof(raw));
        } else
            Sleep(1); /* explicit diagnostic opt-out of notified waits */
    }''',
        '''    u32 last_put = vm_read32(GCM_CONTROL_GUEST_ADDR);
    u32 label_unchanged = 0;
    while (s_gcm_initialized) {
        u32 raw;
        memcpy(&raw, vm_base + GCM_CONTROL_GUEST_ADDR, sizeof(raw));
        u32 put = ps3_bswap32(raw);
        if (put != last_put) {
            last_put = put;
            label_unchanged = 0;
            cellGcm_fifo_kick();
        }
        u32 wait_ea = s_fifo_wait_label_ea;
        if (wait_ea) {
            memcpy(&raw, vm_base + wait_ea, sizeof(raw));
            if (ps3_bswap32(raw) == s_fifo_wait_label_value) {
                label_unchanged = 0;
                cellGcm_fifo_kick();
            } else if (label_unchanged < 64) {
                label_unchanged++;
            }
        } else {
            wait_ea = GCM_CONTROL_GUEST_ADDR;
            label_unchanged = 0;
        }
        if (ps3_poll_enabled) {
            /* Label equality waits may observe transient reference values. */
            if (wait_ea == GCM_CONTROL_GUEST_ADDR || label_unchanged >= 64)
                ps3_poll_backoff_idle_fixed(40, wait_ea, &raw, sizeof(raw));
            else
                ps3_poll_backoff(40, wait_ea, &raw, sizeof(raw));
        } else
            Sleep(1); /* explicit diagnostic opt-out of notified waits */
    }''')
    text=replace(text,'''            if (!(s_host_enabled && subch==0 && !engine_only &&
                  ((method>=NV4097_SET_TRANSFORM_PROGRAM && method<NV4097_SET_TRANSFORM_PROGRAM+128u) ||
                   (method>=NV4097_SET_TRANSFORM_CONSTANT && method<NV4097_SET_TRANSFORM_CONSTANT+256u)) &&
                  d2_vp_packet_enabled() && s_host_packet->size>=4u &&
                  (d2_vp_packet_copy(&s_state,method,count,type,
                      (const u8*)(s_host_packet+1)+4u,s_host_packet->size-4u) ||
                   (d2_constant_packet_enabled() && d2_constant_packet_copy(&s_state,method,count,type,
                      (const u8*)(s_host_packet+1)+4u,s_host_packet->size-4u)))))
            for (u32 i = 0; i < count; i++) {''','''            if (!(s_host_enabled && subch==0 && !engine_only &&
                  ((method>=NV4097_SET_TRANSFORM_PROGRAM && method<NV4097_SET_TRANSFORM_PROGRAM+128u) ||
                   (method>=NV4097_SET_TRANSFORM_CONSTANT && method<NV4097_SET_TRANSFORM_CONSTANT+256u)) &&
                  d2_vp_packet_enabled() && s_host_packet->size>=4u &&
                  (d2_vp_packet_copy(&s_state,method,count,type,
                      (const u8*)(s_host_packet+1)+4u,s_host_packet->size-4u) ||
                   (d2_constant_packet_enabled() && d2_constant_packet_copy(&s_state,method,count,type,
                      (const u8*)(s_host_packet+1)+4u,s_host_packet->size-4u))))) {
                static int d2_diag_init = 0, d2_diag_on = 0, d2_live = 0, d2_eng = 0;
                if (__builtin_expect(!d2_diag_init, 0)) {
                    d2_diag_init = 1;
                    d2_live = rsx_live_draw_enabled();
                    d2_eng = rsx_draw_engine_enabled();
                    d2_diag_on = d2_live || d2_eng || getenv("GCM_OBJDBG") ||
                        getenv("GCM_SUBCH_GATE") || getenv("GCM_VAL_WATCH") ||
                        getenv("RSX_LIVE_FEED_DBG");
                }
                if (!d2_diag_on && s_host_enabled && subch == 0 && !engine_only && ea &&
                    s_host_packet && s_host_packet->size >= 4u + count * 4u) {
                    const u8* fptr = (const u8*)(s_host_packet + 1) + 4u;
                    const int inc = (type == 0);
                    for (u32 i = 0; i < count; i++) {
                        u32 raw;
                        memcpy(&raw, fptr + i * 4u, 4);
                        const u32 data = ps3_bswap32(raw);
                        const u32 m = inc ? (method + i * 4u) : method;
                        rsx_process_method(&s_state, m, data);
                        if (__builtin_expect(m == 0x50u, 0))
                            gcm_ref_push_at(g_rsx_last_reference, s_fifo_getoff);
                        if (__builtin_expect(m == 0x1800u, 0)) {
                            const u32 v = data, idx = (v & 0xFFFFFFu) / 16u;
                            { static int rn = 0; if (rn++ < 6) printf("[GET_REPORT] type=%u idx=%u%c", v >> 24, idx, 10); }
                            if (idx < CELL_GCM_MAX_REPORT_COUNT) {
                                s_report_data[idx].timestamp = get_timestamp_ns();
                                s_report_data[idx].value = (v >> 24) == 1u ? 0xFFFFu : 0u;
                            }
                        }
                    }
                } else
            for (u32 i = 0; i < count; i++) {''')
    text=replace(text,'''                } else
                    gcm_2d_method(subch, m, data);
                }
            }
            if (rsx_draw_engine_enabled() && (subch == 4 || subch == 5) &&''','''                } else
                    gcm_2d_method(subch, m, data);
                }
            } }
            if (rsx_draw_engine_enabled() && (subch == 4 || subch == 5) &&''')
    text=replace(text,'#include "d2_gcm_ready_wake.h"\nvoid cellGcm_fifo_kick_wait(unsigned ms)', '''#include "d2_gcm_ready_wake.h"
#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002u
#endif
static INIT_ONCE s_d2_gcm_fls_once = INIT_ONCE_STATIC_INIT;
static DWORD s_d2_gcm_fls = FLS_OUT_OF_INDEXES;
static volatile LONG s_d2_gcm_hr_state = 0;

static VOID WINAPI d2_gcm_timer_fls_free(PVOID p)
{
    if (p) CloseHandle((HANDLE)p);
}

static BOOL CALLBACK d2_gcm_timer_fls_init(PINIT_ONCE o, PVOID p, PVOID* c)
{
    (void)o; (void)p; (void)c;
    s_d2_gcm_fls = FlsAlloc(d2_gcm_timer_fls_free);
    return TRUE;
}

static HANDLE d2_gcm_get_highres_timer(void)
{
    if (InterlockedCompareExchange(&s_d2_gcm_hr_state, 0, 0) < 0) return NULL;
    InitOnceExecuteOnce(&s_d2_gcm_fls_once, d2_gcm_timer_fls_init, NULL, NULL);
    if (s_d2_gcm_fls == FLS_OUT_OF_INDEXES) return NULL;
    HANDLE t = (HANDLE)FlsGetValue(s_d2_gcm_fls);
    if (!t) {
        t = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (t) {
            if (!FlsSetValue(s_d2_gcm_fls, t)) {
                CloseHandle(t);
                return NULL;
            }
            InterlockedExchange(&s_d2_gcm_hr_state, 1);
        } else {
            InterlockedExchange(&s_d2_gcm_hr_state, -1);
        }
    }
    return t;
}
#endif
void cellGcm_fifo_kick_wait(unsigned ms)''')
    text=replace(text,'''#ifndef _WIN32
    /* The POSIX waitable timer has a nanosecond deadline. Rounding the 60 Hz
     * phase up to milliseconds gives the producer up to 1 ms less preparation
     * time and can turn an otherwise complete frame into a two-vblank frame.
     * Both the event and the timer interrupt this single-consumer render wait. */
    static HANDLE timer;
    if (!timer) timer = CreateWaitableTimerA(NULL, FALSE, NULL);
    u64 remaining = gcm_vblank_remaining_ns();
    if (timer && ms != INFINITE && remaining <= (u64)ms * 1000000u) {
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((remaining + 99u) / 100u);
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
            HANDLE events[2] = {kick, timer};
            WaitForMultipleObjects(2, events, FALSE, ms);
            return;
        }
    }
#endif''','''#ifdef _WIN32
    if (ms != INFINITE && ms > 0u) {
        HANDLE timer = d2_gcm_get_highres_timer();
        u64 remaining = gcm_vblank_remaining_ns();
        if (timer && remaining <= (u64)ms * 1000000ULL) {
            u64 ticks100 = (remaining + 99u) / 100u;
            if (ticks100 == 0u) ticks100 = 1u;
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)ticks100;
            if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
                HANDLE events[2] = {kick, timer};
                WaitForMultipleObjects(2, events, FALSE, ms);
                return;
            }
        }
    }
#else
    /* The POSIX waitable timer has a nanosecond deadline. Rounding the 60 Hz
     * phase up to milliseconds gives the producer up to 1 ms less preparation
     * time and can turn an otherwise complete frame into a two-vblank frame.
     * Both the event and the timer interrupt this single-consumer render wait. */
    static HANDLE timer;
    if (!timer) timer = CreateWaitableTimerA(NULL, FALSE, NULL);
    u64 remaining = gcm_vblank_remaining_ns();
    if (timer && ms != INFINITE && remaining <= (u64)ms * 1000000u) {
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((remaining + 99u) / 100u);
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
            HANDLE events[2] = {kick, timer};
            WaitForMultipleObjects(2, events, FALSE, ms);
            return;
        }
    }
#endif''')
    text=replace(text,'''/* D2_VP_PACKET_COPY_V1: consume only immutable, all-VP-code packets.''','''#if defined(__SSSE3__)
#include <tmmintrin.h>
#endif
static inline void d2_bswap32_copy(u8* dst, const u8* src, u32 words)
{
    u32 i = 0u;
#ifdef __SSSE3__
    if (words >= 4u) {
        const __m128i mask128 = _mm_setr_epi8(
            3, 2, 1, 0,  7, 6, 5, 4,  11, 10, 9, 8,  15, 14, 13, 12);
        for (; i + 8u <= words; i += 8u) {
            __m128i v0 = _mm_loadu_si128((const __m128i*)(src + i * 4u));
            __m128i v1 = _mm_loadu_si128((const __m128i*)(src + i * 4u + 16u));
            _mm_storeu_si128((__m128i*)(dst + i * 4u), _mm_shuffle_epi8(v0, mask128));
            _mm_storeu_si128((__m128i*)(dst + i * 4u + 16u), _mm_shuffle_epi8(v1, mask128));
        }
        if (i + 4u <= words) {
            __m128i v0 = _mm_loadu_si128((const __m128i*)(src + i * 4u));
            _mm_storeu_si128((__m128i*)(dst + i * 4u), _mm_shuffle_epi8(v0, mask128));
            i += 4u;
        }
    }
#endif
#if defined(__clang__)
#pragma clang loop vectorize(disable)
#pragma clang loop interleave(disable)
#endif
    for (; i < words; i++) {
        u32 w;
        memcpy(&w, src + i * 4u, 4u);
        w = __builtin_bswap32(w);
        memcpy(dst + i * 4u, &w, 4u);
    }
}

/* D2_VP_PACKET_COPY_V1: consume only immutable, all-VP-code packets.''')
    text=replace(text,'''    for(u32 i=0;i<writes;i++) {
        const u8* src=payload+i*4u;
        u8* dst=state->vp_ucode+cursor+i*4u;
        dst[0]=src[3];dst[1]=src[2];dst[2]=src[1];dst[3]=src[0];
    }''','''    if (writes) d2_bswap32_copy(state->vp_ucode + cursor, payload, writes);''')
    text=replace(text,'''    const u32 reg0=(method-first)/4u;
    for(u32 i=0;i<count;i++) {
        u32 reg=type==0?reg0+i:reg0;
        u32 slot=state->transform_constant_load+(reg>>2);
        if(slot<RSX_MAX_VERTEX_CONSTANTS) {
            const u8* src=payload+i*4u;
            u32 bits=(u32)src[0]<<24|(u32)src[1]<<16|(u32)src[2]<<8|src[3];
            memcpy(&state->vertex_constants[slot][reg&3u],&bits,4);
            if(!state->vertex_constants_dirty) {
                state->vertex_constants_lo=slot;state->vertex_constants_hi=slot;
                state->vertex_constants_dirty=1;
            } else {
                if(slot<state->vertex_constants_lo) state->vertex_constants_lo=slot;
                if(slot>state->vertex_constants_hi) state->vertex_constants_hi=slot;
            }
        }
    }
    return 1;''','''    const u32 reg0 = (method - first) / 4u;
    if (type == 0u) {
        u64 first_slot = (u64)state->transform_constant_load + (u64)(reg0 >> 2);
        if (first_slot < RSX_MAX_VERTEX_CONSTANTS) {
            u32 lane0 = reg0 & 3u;
            u32 max_words = (u32)(RSX_MAX_VERTEX_CONSTANTS - first_slot) * 4u - lane0;
            u32 writes = count < max_words ? count : max_words;
            if (writes > 0u) {
                d2_bswap32_copy((u8*)&state->vertex_constants[first_slot][lane0], payload, writes);
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
    return 1;''')
    def include(match):
        path=source.parent/match[1]
        return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else match[0]
    text=re.sub(r'#\s*include\s*"([^"\n]+)"',include,text)
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text('#include "flip_policy.h"\n'+text,encoding='utf-8',newline='\n')

if __name__=='__main__':
    generate(sys.argv[1],sys.argv[2])
