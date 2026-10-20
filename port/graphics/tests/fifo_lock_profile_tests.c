#include <stdio.h>
#include "fifo_lock_profile.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"failed line %d\n",__LINE__);return 1; } } while(0)
static DWORD WINAPI worker(void* ignored)
{
    (void)ignored;
    for (unsigned i=0;i<1000;i++) d2_fifo_lock_acquired(d2_fifo_lock_begin(),i&1u);
    return 0;
}
int main(void)
{
    const char* flag=getenv("D2_FRAME_LIGHT");
    int enabled=flag && strcmp(flag,"1")==0;
    HANDLE threads[4];
    for(unsigned i=0;i<4;i++) {
        threads[i]=CreateThread(NULL,0,worker,NULL,0,NULL);
        CHECK(threads[i]!=NULL);
    }
    CHECK(WaitForMultipleObjects(4,threads,TRUE,10000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<4;i++) CloseHandle(threads[i]);
    double ms;unsigned calls,held;
    d2_fifo_lock_take(&ms,&calls,&held);
    CHECK(ms>=0);
    CHECK(calls==(enabled?4000u:0u));
    CHECK(held==(enabled?2000u:0u));
    if(!enabled) CHECK(ms==0);
    d2_fifo_lock_take(&ms,&calls,&held);
    CHECK(ms==0 && calls==0 && held==0);
    return 0;
}
