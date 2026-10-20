#include <stdio.h>
#include "fifo_draw_profile.h"
FILE* d2_capture_fopen(const char* path,const char* mode) { return fopen(path,mode); }
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed line %d\n",__LINE__);return 1;}}while(0)
int main(void)
{
    for(unsigned c=0;c<6;c++) CHECK(d2_sample_begin(c)==0);
    for(unsigned c=0;c<6;c++) CHECK(d2_sample_calls[c]==0);
    d2_sample_reset();
    for(unsigned i=0;i<65536;i++) for(unsigned c=0;c<6;c++) {
        double start=d2_sample_begin(c);
        d2_sample_end(c,start);
    }
    CHECK(d2_sample_begin(6)==0);
    d2_sample_end(6,1);
    for(unsigned c=0;c<6;c++) {
        CHECK(d2_sample_calls[c]==65536);
        CHECK(d2_sample_n[c]>150 && d2_sample_n[c]<400);
        CHECK(d2_sample_time[c]>=0);
    }
    D2_DRAW_PROFILE_STOP();
    CHECK(d2_sample_begin(0)==0 && d2_sample_calls[0]==65536);
    D2_DRAW_PROFILE_SAVE("sample-test.csv");
    FILE* f=fopen("sample-test.csv.fifo.json","r");CHECK(f!=NULL);
    char json[4096]={0};size_t n=fread(json,1,sizeof json-1,f);fclose(f);
    CHECK(n>0 && strstr(json,"all-marked-stages-combined") && strstr(json,"vertex_fetch"));
    remove("sample-test.csv.fifo.json");
    d2_sample_reset();
    for(unsigned c=0;c<6;c++) CHECK(d2_sample_calls[c]==0 && d2_sample_n[c]==0 && d2_sample_time[c]==0);
    return 0;
}
