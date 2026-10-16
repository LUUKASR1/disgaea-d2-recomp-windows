/* Coarse diagnostic counters only. Included once by the adapted GCM unit.
 * No change to lock ownership, flip publication, or command execution. */
#ifndef D2_FIFO_LOCK_PROFILE_H
#define D2_FIFO_LOCK_PROFILE_H
#include <windows.h>
#include <stdlib.h>
#include <string.h>
static volatile LONG64 d2_fifo_lock_ticks, d2_fifo_lock_calls, d2_fifo_held_calls;
static LONG64 d2_fifo_lock_begin(void)
{
    /* Environment is fixed for the process. Thread-local initialization avoids
     * a race between concurrent callers before acquiring the FIFO lock. */
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char* value = getenv("D2_FRAME_LIGHT");
        enabled = value && strcmp(value, "1") == 0;
    }
    if (!enabled) return 0;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}
static void d2_fifo_lock_acquired(LONG64 start, int held)
{
    if (!start) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    InterlockedAdd64(&d2_fifo_lock_ticks, now.QuadPart - start);
    InterlockedIncrement64(&d2_fifo_lock_calls);
    if (held) InterlockedIncrement64(&d2_fifo_held_calls);
}
/* Taken by the visible-frame sampler, potentially on another thread.
 * Separate atomic exchanges are approximate frame attribution, not a coherent
 * snapshot of an individual drain. Timings overlap other host measurements. */
void d2_fifo_lock_take(double* milliseconds, unsigned* calls, unsigned* held)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    *milliseconds = (double)InterlockedExchange64(&d2_fifo_lock_ticks, 0) *
                    1000.0 / (double)frequency.QuadPart;
    *calls = (unsigned)InterlockedExchange64(&d2_fifo_lock_calls, 0);
    *held = (unsigned)InterlockedExchange64(&d2_fifo_held_calls, 0);
}
#endif
