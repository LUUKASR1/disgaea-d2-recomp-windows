#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../d2_loop_frame.h"
static int collecting;
extern "C" void d2_fifo_wait_set_active(int active) { collecting=active; }
extern "C" void d2_fifo_wait_take(double* ms,unsigned* calls,unsigned* outcomes)
{ memset(ms,0,D2_WAIT_REASONS*sizeof *ms);memset(calls,0,D2_WAIT_REASONS*sizeof *calls);memset(outcomes,0,3*sizeof *outcomes); }
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed line %d\n",__LINE__);return 1;}}while(0)
int main()
{
    CHECK(d2_loop_enabled());
    CHECK(collecting);
    d2_loop_step(5);
    d2_loop_step(0,1);
    CHECK(d2_loop_wait_calls==1 && d2_loop_sections[0]>=0);
    d2_loop_finished=1;
    double checkpoint=d2_loop_checkpoint;
    d2_loop_step(3);
    CHECK(d2_loop_checkpoint==checkpoint);
    SetEnvironmentVariableW(L"D2_PERF_CSV",L"loop-\x00e1rea-test.csv");
    d2_loop_n=1;
    D2LoopFrame& r=d2_loop_samples[0];
    r.interval=33;r.stage=3;r.wait_calls=2;r.late_waits=1;r.vblank=99;
    const double sections[]={10,2,12,5,1,3};
    memcpy(r.section,sections,sizeof sections);r.max_wait=9;r.max_wait_target=7;
    r.reason_ms[0]=3;r.reason_calls[0]=2;r.outcomes[0]=1;r.outcomes[1]=1;
    d2_loop_save();CHECK(d2_loop_saved==1);
    CHECK(!collecting);
    FILE* f=d2_capture_fopen("loop-\xc3\xa1rea-test.csv.loop.csv","r");CHECK(f);
    char text[4096]={0};size_t n=fread(text,1,sizeof text-1,f);fclose(f);
    CHECK(n && strstr(text,"wait_ms,tick_ms,present_ms,fifo_ms,pump_ms,other_ms"));
    CHECK(strstr(text,"wait_no_pending_flip_ms") && strstr(text,"wait_other_results"));
    CHECK(strstr(text,",99,3.000000,0.000000,0.000000,0.000000,0.000000,2,0,0,0,0,1,1,0\n"));
    CHECK(strstr(text,"0,mothman,33.000000,10.000000,2.000000,12.000000,5.000000,1.000000,3.000000,9.000000,7.000000,2,1,99"));
    SetEnvironmentVariableW(L"D2_PERF_CSV",L"missing-loop-parent/frames.csv");
    d2_loop_n=2;d2_loop_save();CHECK(d2_loop_saved==1);
    return 0;
}
