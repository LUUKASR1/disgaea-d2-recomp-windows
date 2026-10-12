/* D2_LOOP_FRAME_V1: opt-in section timing, RAM until F10/exit.
 * Host wall sections; not PPU CPU usage or GPU execution time. */
#ifndef D2_LOOP_FRAME_H
#define D2_LOOP_FRAME_H
#define D2_LOOP_FRAME_MAX 18000u
struct D2LoopFrame {
    double interval,section[6],max_wait,max_wait_target;
    unsigned stage,wait_calls,late_waits;
    unsigned long long vblank;
};
static D2LoopFrame d2_loop_samples[D2_LOOP_FRAME_MAX];
static unsigned d2_loop_n,d2_loop_saved,d2_loop_stage,d2_loop_keys,d2_loop_finished;
static double d2_loop_previous,d2_loop_checkpoint,d2_loop_sections[6],d2_loop_max_wait,d2_loop_max_target;
static unsigned d2_loop_wait_calls,d2_loop_late_waits;
static const char* d2_loop_names[]={"inicio","castle","batalha","save"};
static void d2_loop_save(void)
{
    if(!d2_loop_n || d2_loop_saved==d2_loop_n)return;
    FILE* f=fopen("tempos-loop.csv","w");
    if(!f){fprintf(stderr,"[D2LOOP] could not open tempos-loop.csv\n");return;}
    fprintf(f,"frame,stage,interval_ms,wait_ms,tick_ms,present_ms,fifo_ms,pump_ms,other_ms,max_wait_ms,max_wait_target_ms,wait_calls,late_waits,vblank\n");
    for(unsigned i=0;i<d2_loop_n;i++){
        const D2LoopFrame& r=d2_loop_samples[i];
        fprintf(f,"%u,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%llu\n",
            i,d2_loop_names[r.stage],r.interval,r.section[0],r.section[1],r.section[2],
            r.section[3],r.section[4],r.section[5],r.max_wait,r.max_wait_target,
            r.wait_calls,r.late_waits,r.vblank);
    }
    if(fclose(f)==0){d2_loop_saved=d2_loop_n;fprintf(stderr,"[D2LOOP] saved %u frames\n",d2_loop_n);}
}
static bool d2_loop_enabled(void)
{
    static int enabled=-1;
    if(enabled<0){const char* e=getenv("D2_LOOP_FRAME");enabled=e && strcmp(e,"1")==0;
        if(enabled){atexit(d2_loop_save);fprintf(stderr,"[D2LOOP] F6 castle, F7 battle, F9 save, F10 finish\n");}}
    return enabled && !d2_loop_finished;
}
static double d2_loop_now(void)
{
    static double inv;LARGE_INTEGER q;
    if(!inv){QueryPerformanceFrequency(&q);inv=1000.0/(double)q.QuadPart;}
    QueryPerformanceCounter(&q);return (double)q.QuadPart*inv;
}
static void d2_loop_step(unsigned section,unsigned requested_wait=0)
{
    if(!d2_loop_enabled())return;
    double now=d2_loop_now();
    if(d2_loop_checkpoint>0){
        double dt=now-d2_loop_checkpoint;d2_loop_sections[section]+=dt;
        if(section==0){
            d2_loop_wait_calls++;
            if(dt>d2_loop_max_wait){d2_loop_max_wait=dt;d2_loop_max_target=(double)requested_wait;}
            if(dt>(double)requested_wait+1.0)d2_loop_late_waits++;
        }
    }
    d2_loop_checkpoint=now;
}
static void d2_loop_complete(unsigned long long vblank)
{
    if(!d2_loop_enabled())return;
    double now=d2_loop_checkpoint;
    unsigned keys=0;const int vk[]={0x75,0x76,0x78,0x79};
    for(unsigned i=0;i<4;i++)if(GetAsyncKeyState(vk[i])&0x8000)keys|=1u<<i;
    unsigned edges=keys & ~d2_loop_keys;d2_loop_keys=keys;
    if(edges){
        if(edges&8u){d2_loop_save();d2_loop_finished=1;return;}
        d2_loop_stage=(edges&1u)?1u:(edges&2u)?2u:3u;
        d2_loop_previous=0;fprintf(stderr,"[D2LOOP] stage=%s\n",d2_loop_names[d2_loop_stage]);
    }
    if(d2_loop_stage && d2_loop_previous>0 && d2_loop_n<D2_LOOP_FRAME_MAX){
        D2LoopFrame& r=d2_loop_samples[d2_loop_n++];r.interval=now-d2_loop_previous;
        memcpy(r.section,d2_loop_sections,sizeof r.section);r.stage=d2_loop_stage;
        r.max_wait=d2_loop_max_wait;r.max_wait_target=d2_loop_max_target;
        r.wait_calls=d2_loop_wait_calls;r.late_waits=d2_loop_late_waits;r.vblank=vblank;
        if(d2_loop_n==D2_LOOP_FRAME_MAX){d2_loop_save();d2_loop_finished=1;}
    }
    memset(d2_loop_sections,0,sizeof d2_loop_sections);
    d2_loop_wait_calls=d2_loop_late_waits=0;d2_loop_max_wait=d2_loop_max_target=0;
    d2_loop_previous=now;
}
#endif
