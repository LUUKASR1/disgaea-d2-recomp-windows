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
static int s_flip_pending,s_flip_mode=1;
static u64 s_last_present_vblank=3;
static struct { int waiting_fifo;u64 earliest_vblank; } s_flip_queue[8];
static u64 gcm_vblank_elapsed_ns(void) { return 3; }
static u64 gcm_vblank_at(u64 x) { return x; }
static int d2_gcm_immediate(void) { return 0; }
static int d2_gcm_unlimited(void) { return 0; }
#include "fifo_wait_profile.inc"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed line %d\n",__LINE__);return 1;}}while(0)
int main(void)
{
    const char* flag=getenv("D2_LOOP_FRAME");int enabled=flag && strcmp(flag,"1")==0;
    for(int p=0;p<2;p++)for(int fifo=0;fifo<2;fifo++)for(int equal=0;equal<2;equal++)
    for(int vsync=0;vsync<2;vsync++)for(int immediate=0;immediate<2;immediate++)
    for(int unlimited=0;unlimited<2;unlimited++)for(u64 now=0;now<8;now++) {
        unsigned r=d2_wait_reason(p,fifo,equal?5u:4u,5,vsync,now,6,3,immediate,unlimited);
        CHECK(r<5);
        CHECK((r==4)==!!d2_flip_eligible(p,fifo,equal?5u:4u,5,vsync,now,6,3,immediate,unlimited));
        if(!p)CHECK(r==0);else if(fifo)CHECK(r==1);else if(!equal)CHECK(r==2);
    }
    HANDLE event=CreateEventW(NULL,FALSE,FALSE,NULL);CHECK(event);
    d2_fifo_wait_set_active(1);
    for(unsigned r=0;r<5;r++) {
        s_flip_pending=r!=0;s_flip_queue[0].waiting_fifo=r==1;
        s_presented_flip_count=r==2?1:0;s_flip_mode=r==4?0:1;
        SetEvent(event);CHECK(d2_profiled_fifo_wait(event,0)==WAIT_OBJECT_0);
        CHECK(d2_profiled_fifo_wait(event,0)==WAIT_TIMEOUT);
    }
    CHECK(d2_profiled_fifo_wait(NULL,0)==WAIT_FAILED);
    double ms[5];unsigned calls[5],outcomes[3];d2_fifo_wait_take(ms,calls,outcomes);
    for(unsigned i=0;i<5;i++){CHECK(ms[i]>=0);CHECK(calls[i]==(enabled?(i==4?3u:2u):0u));}
    CHECK(outcomes[0]==(enabled?5u:0u) && outcomes[1]==(enabled?5u:0u) && outcomes[2]==(enabled?1u:0u));
    d2_fifo_wait_set_active(0);CHECK(d2_profiled_fifo_wait(event,0)==WAIT_TIMEOUT);
    d2_fifo_wait_take(ms,calls,outcomes);
    for(unsigned i=0;i<5;i++)CHECK(calls[i]==0 && ms[i]==0);
    CloseHandle(event);return 0;
}
