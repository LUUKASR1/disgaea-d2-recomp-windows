#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "capture.h"
#include "frame_profile_state.h"
typedef uint32_t u32;
static struct { HWND hwnd; } s_d3d;
double d2_fifo_frame_take_ms(void) { return 0.25; }
#include "d2_frame_profile.h"

int main(void)
{
    const char* name="performance-\xc3\xa1rea-test.csv";
    SetEnvironmentVariableW(L"D2_PERF_CSV",L"performance-\x00e1rea-test.csv");
    d2_lite_n=1;
    D2LiteSample* p=&d2_lite_samples[0];
    p->stage=3; p->interval_ms=20; p->render_ms=9; p->compile_ms=2;
    p->misses=1; p->cache=4; p->draws=17; p->fifo_ms=3;
    p->wait_ms=1.5; p->prepare_ms=4; p->present_ms=.5; p->wait_calls=2;
    p->prepare_setup_ms=1; p->prepare_targets_ms=2; p->prepare_draws_ms=3;
    p->resource_create_host_ms=4; p->shader_compile_host_ms=5; p->pso_create_host_ms=6;
    p->texture_create_calls=7; p->buffer_create_calls=8; p->resource_create_failures=9;
    p->shader_compile_calls=10; p->pso_create_calls=11; p->pso_create_failures=12;
    d2_lite_save();
    if(d2_lite_saved!=1) return 1;
    FILE* f=d2_capture_fopen(name,"r");
    if(!f) return 2;
    char text[4096]={0}; size_t n=fread(text,1,sizeof(text)-1,f); fclose(f);
    if(!n || !strstr(text,"wait_fence_host_ms,prepare_host_ms,present_host_ms,wait_fence_calls,prepare_setup_ms") ||
        !strstr(text,"0,mothman,20.000000,9.000000,2.000000,1,4,17,3.000000,1.500000,4.000000,0.500000,2,1.000000,2.000000,3.000000,4.000000,5.000000,6.000000,7,8,9,10,11,12\n")) return 3;
    /* Failed output must not mark unflushed samples as saved. */
    SetEnvironmentVariableW(L"D2_PERF_CSV",L"missing-parent-for-profile-test/frames.csv");
    d2_lite_n=2;
    d2_lite_save();
    if(d2_lite_saved!=1) return 4;
    SetEnvironmentVariableW(L"D2_PERF_CSV",L"performance-\x00e1rea-test.csv");
    puts("PASS: bounded host sampler CSV fields, UTF-8 output and failed-write recovery");
    return 0;
}
