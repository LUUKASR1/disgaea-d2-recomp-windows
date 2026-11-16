#pragma once
#include <stdint.h>
#include <math.h>
enum { D2_AUTO_IDLE=0,D2_AUTO_WAITING=1,D2_AUTO_READY=2,D2_AUTO_CAPTURING=3 };
typedef struct { unsigned phase,index;uint64_t deadline;int changed,timed_out; } D2AutoProbe;
static inline void d2_auto_arm(D2AutoProbe* probe,unsigned index,uint64_t now){
    probe->phase=D2_AUTO_WAITING;probe->index=index;probe->deadline=now+10000;
    probe->changed=0;probe->timed_out=0;
}
static inline void d2_auto_observed(D2AutoProbe* probe,int changed,uint64_t now){
    if(probe->phase!=D2_AUTO_WAITING)return;
    if(now>=probe->deadline){probe->timed_out=1;probe->phase=D2_AUTO_READY;}
    else if(changed){probe->changed=1;probe->phase=D2_AUTO_READY;}
}
static inline int d2_auto_capture(D2AutoProbe* probe,uint64_t now){
    d2_auto_observed(probe,0,now);
    if(probe->phase!=D2_AUTO_READY)return 0;
    probe->phase=D2_AUTO_CAPTURING;return 1;
}
/* Timeout must be observed after rendering, so reference and geometry agree. */
static inline int d2_auto_capture_frozen(D2AutoProbe* probe,uint64_t now){
    if(probe->phase!=D2_AUTO_READY)return 0;
    return d2_auto_capture(probe,now);
}
static inline int d2_transform_changed(const float* previous,const float* current){
    int changed=0;
    for(unsigned i=0;i<16;i++){
        if(!isfinite(previous[i])||!isfinite(current[i]))return 0;
        float a=fabsf(previous[i]),b=fabsf(current[i]),scale=a>b?a:b;
        if(fabsf(previous[i]-current[i])>1e-5f+1e-6f*scale)changed=1;
    }
    return changed;
}
