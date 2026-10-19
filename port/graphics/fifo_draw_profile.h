/* Bounded, approximately 1/256-call sampling on the existing FIFO callbacks.
 * Categories are inclusive and nested: never add their times together. */
#ifndef D2_FIFO_DRAW_PROFILE_H
#define D2_FIFO_DRAW_PROFILE_H
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "capture.h"
static uint64_t d2_sample_calls[6], d2_sample_n[6];
static uint32_t d2_sample_rng[6];
static double d2_sample_time[6];
static int d2_sample_active;
static double d2_sample_now(void)
{
    static double inv;
    LARGE_INTEGER q;
    if (!inv) { QueryPerformanceFrequency(&q); inv=1.0/(double)q.QuadPart; }
    QueryPerformanceCounter(&q);
    return (double)q.QuadPart*inv;
}
static double d2_sample_begin(unsigned category)
{
    if (!d2_sample_active || category>=6) return 0;
    d2_sample_calls[category]++;
    uint32_t r=d2_sample_rng[category]*1664525u+1013904223u+category*17u;
    d2_sample_rng[category]=r;
    return (r>>24)==0 ? d2_sample_now() : 0;
}
static void d2_sample_end(unsigned category,double start)
{
    if (start<=0 || category>=6) return;
    d2_sample_time[category]+=d2_sample_now()-start;
    d2_sample_n[category]++;
}
static void d2_sample_reset(void)
{
    memset(d2_sample_calls,0,sizeof d2_sample_calls);
    memset(d2_sample_n,0,sizeof d2_sample_n);
    memset(d2_sample_rng,0,sizeof d2_sample_rng);
    memset(d2_sample_time,0,sizeof d2_sample_time);
    d2_sample_active=1;
}
static void d2_sample_save(const char* csv)
{
    char path[32768];
    if (!csv || strlen(csv)+sizeof ".fifo.json">sizeof path) return;
    strcpy(path,csv);strcat(path,".fifo.json");
    FILE* f=d2_capture_fopen(path,"w");
    if (!f) return;
    static const char* names[]={"vertex_shader","constants","bind_texture",
                               "draw_arrays","draw_indexed","vertex_fetch"};
    fprintf(f,"{\"schema\":1,\"scope\":\"all-marked-stages-combined\","
              "\"sampling\":\"lcg-high-byte-zero\",\"nominal_sample_rate\":256,"
              "\"inclusive_categories\":true,\"categories\":[");
    for (unsigned i=0;i<6;i++)
        fprintf(f,"%s{\"name\":\"%s\",\"calls\":%llu,\"samples\":%llu,\"sampled_ms\":%.9f}",
                i?",":"",names[i],(unsigned long long)d2_sample_calls[i],
                (unsigned long long)d2_sample_n[i],d2_sample_time[i]*1000.0);
    fprintf(f,"]}");
    fclose(f);
}
/* Called only when the lightweight measurement starts and saves. No log spam,
 * periodic sorting, images, or timestamps on unsampled callbacks. The outer
 * callback in progress at F10 is not a completed sample in that saved snapshot. */
#define D2_LIGHT_EXTRA_RESET d2_sample_reset
#define D2_DRAW_PROFILE_STOP() (d2_sample_active=0)
#define D2_DRAW_PROFILE_SAVE(csv) d2_sample_save(csv)
#endif
