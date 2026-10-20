/* Included once at the audited GCM wait site. Snapshot, release lock, then
 * perform the original event wait. Never consume a flip or change eligibility. */
#include "fifo_wait_reason.h"
static volatile LONG d2_wait_active;
static volatile LONG64 d2_no_flip_ticks[D2_NO_FLIP_STATES],d2_no_flip_calls[D2_NO_FLIP_STATES];
void d2_fifo_no_flip_take(double ms[D2_NO_FLIP_STATES],unsigned calls[D2_NO_FLIP_STATES])
{
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    for(unsigned i=0;i<D2_NO_FLIP_STATES;i++) {
        ms[i]=(double)InterlockedExchange64(&d2_no_flip_ticks[i],0)*1000.0/(double)frequency.QuadPart;
        calls[i]=(unsigned)InterlockedExchange64(&d2_no_flip_calls[i],0);
    }
}
static volatile LONG64 d2_wait_ticks[D2_WAIT_REASONS],d2_wait_calls[D2_WAIT_REASONS],d2_wait_outcomes[3];
void d2_fifo_wait_set_active(int active) { InterlockedExchange(&d2_wait_active,active?1:0); }
void d2_fifo_wait_take(double ms[D2_WAIT_REASONS],unsigned calls[D2_WAIT_REASONS],unsigned outcomes[3])
{
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    for(unsigned i=0;i<D2_WAIT_REASONS;i++) {
        ms[i]=(double)InterlockedExchange64(&d2_wait_ticks[i],0)*1000.0/(double)frequency.QuadPart;
        calls[i]=(unsigned)InterlockedExchange64(&d2_wait_calls[i],0);
    }
    for(unsigned i=0;i<3;i++)outcomes[i]=(unsigned)InterlockedExchange64(&d2_wait_outcomes[i],0);
}
static DWORD d2_profiled_fifo_wait(HANDLE event,DWORD timeout)
{
    static _Thread_local int enabled=-1;
    if(enabled<0){const char* e=getenv("D2_LOOP_FRAME");enabled=e && strcmp(e,"1")==0;}
    if(!enabled || !InterlockedCompareExchange(&d2_wait_active,0,0))
        return WaitForSingleObject(event,timeout);
    AcquireSRWLockExclusive(&s_flip_lock);
    u32 completed=s_completed_flip_count,slot=completed%GCM_FLIP_QUEUE_SIZE;
    unsigned reason=d2_wait_reason(s_flip_pending,s_flip_queue[slot].waiting_fifo,
        s_presented_flip_count,completed,s_flip_mode==CELL_GCM_DISPLAY_VSYNC,
        gcm_vblank_at(gcm_vblank_elapsed_ns()),s_flip_queue[slot].earliest_vblank,
        s_last_present_vblank,d2_gcm_immediate(),d2_gcm_unlimited());
    ReleaseSRWLockExclusive(&s_flip_lock);
    unsigned no_flip=0;
    if(reason==0) {
        /* Atomic SDK state only; no host copy/render lock is acquired.
         * Separate loads do not constitute a coherent producer snapshot. */
        int host=s_host_enabled;
        int label=host && s_fifo_wait_label_ea!=0;
        int queued=host && s_host_head!=s_host_tail;
        int unpublished=host && vm_read32(GCM_CONTROL_GUEST_ADDR)!=vm_read32(GCM_CONTROL_GUEST_ADDR+4);
        no_flip=d2_no_flip_state(host,label,queued,unpublished);
    }
    LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);
    DWORD result=WaitForSingleObject(event,timeout);
    DWORD error=result==WAIT_FAILED?GetLastError():0;
    QueryPerformanceCounter(&end);
    InterlockedAdd64(&d2_wait_ticks[reason],end.QuadPart-begin.QuadPart);
    InterlockedIncrement64(&d2_wait_calls[reason]);
    if(reason==0) {
        InterlockedAdd64(&d2_no_flip_ticks[no_flip],end.QuadPart-begin.QuadPart);
        InterlockedIncrement64(&d2_no_flip_calls[no_flip]);
    }
    InterlockedIncrement64(&d2_wait_outcomes[result==WAIT_OBJECT_0?0:result==WAIT_TIMEOUT?1:2]);
    if(result==WAIT_FAILED)SetLastError(error);
    return result;
}
