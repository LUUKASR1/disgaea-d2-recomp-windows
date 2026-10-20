#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
typedef uint64_t u64;
#define GCM_FLIP_QUEUE_SIZE 8
#define CELL_GCM_DISPLAY_VSYNC 1
static SRWLOCK s_flip_lock=SRWLOCK_INIT;
static u32 s_completed_flip_count,s_presented_flip_count;
static int s_flip_pending,s_flip_mode,unlimited,immediate;
static u64 s_last_present_vblank,now;
static struct {int waiting_fifo;u64 earliest_vblank;} s_flip_queue[8];
static HANDLE kick_event;
static unsigned waits,starts;
static int lock_released=1;
static void gcm_start_put_watch(void){starts++;}
static HANDLE cellGcm_fifo_kick_event(void){return kick_event;}
static u64 gcm_vblank_elapsed_ns(void){return now;}
static u64 gcm_vblank_at(u64 x){return x;}
static int d2_gcm_immediate(void){return immediate;}
static int d2_gcm_unlimited(void){return unlimited;}
#include "flip_policy.h"
#include "d2_gcm_ready_wake.h"
static DWORD d2_profiled_fifo_wait(HANDLE event,DWORD ms)
{
    waits++;
    if(TryAcquireSRWLockExclusive(&s_flip_lock))ReleaseSRWLockExclusive(&s_flip_lock);
    else lock_released=0;
    return WaitForSingleObject(event,ms);
}
#include "ready_wait_function.inc"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"failed line %d\n",__LINE__);return 1;}}while(0)
int main(void)
{
    int enabled=d2_ready_wake_enabled();
    kick_event=CreateEventW(NULL,FALSE,FALSE,NULL);CHECK(kick_event);
    s_completed_flip_count=10;s_last_present_vblank=3;
    for(int pending=0;pending<2;pending++)for(int fifo=0;fifo<2;fifo++)
    for(int equal=0;equal<2;equal++)for(int vsync=0;vsync<2;vsync++)
    for(immediate=0;immediate<2;immediate++)for(unlimited=0;unlimited<2;unlimited++)
    for(now=0;now<8;now++) {
        s_flip_pending=pending;s_flip_mode=vsync;
        s_presented_flip_count=equal?10:9;
        s_flip_queue[2].waiting_fifo=fifo;s_flip_queue[2].earliest_vblank=6;
        int skip=(enabled || unlimited) && d2_flip_eligible(pending,fifo,
            s_presented_flip_count,10,vsync,now,6,3,immediate,unlimited);
        unsigned before=waits,watch_before=starts;
        CHECK(SetEvent(kick_event));cellGcm_fifo_kick_wait(0);
        CHECK(waits-before==(skip?0u:1u));CHECK(starts==watch_before+1);
        CHECK(lock_released);
        CHECK(WaitForSingleObject(kick_event,0)==(skip?WAIT_OBJECT_0:WAIT_TIMEOUT));
        CHECK(s_flip_pending==pending && s_completed_flip_count==10);
        CHECK(s_presented_flip_count==(equal?10u:9u) && s_last_present_vblank==3);
        CHECK(s_flip_queue[2].waiting_fifo==fifo && s_flip_queue[2].earliest_vblank==6);
        CHECK(TryAcquireSRWLockExclusive(&s_flip_lock));ReleaseSRWLockExclusive(&s_flip_lock);
    }
    CloseHandle(kick_event);return 0;
}
