#include "auto_probe.h"
#include <cstdio>
#include <stdexcept>
#include <limits>
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){try{
    D2AutoProbe probe{};require(!d2_auto_capture(&probe,0),"Idle cannot capture");
    d2_auto_arm(&probe,3,100);
    require(probe.phase==D2_AUTO_WAITING && probe.index==3,"F8 arms one existing capture index");
    d2_auto_observed(&probe,0,101);
    require(!d2_auto_capture(&probe,102),"Static pair keeps waiting");
    d2_auto_observed(&probe,1,103);
    require(probe.phase==D2_AUTO_READY && probe.changed && !probe.timed_out,"Changed pair selected");
    d2_auto_observed(&probe,0,104);
    require(probe.changed,"Selected pair is frozen");
    require(d2_auto_capture(&probe,105) && probe.phase==D2_AUTO_CAPTURING,"Next frame owns capture");
    require(!d2_auto_capture(&probe,106),"No duplicate capture while finishing");
    d2_auto_arm(&probe,4,200);
    require(!d2_auto_capture(&probe,10199),"Wait until the exact deadline");
    require(d2_auto_capture(&probe,10200) && probe.timed_out && !probe.changed,"Timeout returns an explicit capture");
    d2_auto_arm(&probe,5,300);
    d2_auto_observed(&probe,1,10300);
    require(probe.timed_out && !probe.changed,"Change at expired deadline is not reported as success");

    float a[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},b[16];
    for(unsigned i=0;i<16;i++)b[i]=a[i];
    require(!d2_transform_changed(a,b),"Static transform");
    b[1]=1e-7f;require(!d2_transform_changed(a,b),"Tiny floating noise ignored");
    b[3]=.01f;require(d2_transform_changed(a,b),"Translation detected");
    b[4]=std::numeric_limits<float>::quiet_NaN();
    require(!d2_transform_changed(a,b),"Nonfinite input cannot select a pair");
    puts("PASS automatic temporal selection: static wait, change freeze, timeout, no duplicate and finite thresholds");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
