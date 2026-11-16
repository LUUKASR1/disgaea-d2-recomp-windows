#include "motion_gap.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){try{
    unsigned char coverage[1]={255};float depth[1]={.4f};D2MotionMask masks[1]={{coverage,depth,1}};
    D2MotionPixel source{0,0,1,.4f},filtered=source;float epsilon=8.f/16777215.f;
    require(d2_motion_gap_class(&source,&filtered,masks,1,0,epsilon)==D2_GAP_RETAINED,"Stationary vector is retained, not a gap");
    source.coverage=0;require(d2_motion_gap_class(&source,nullptr,masks,1,0,epsilon)==D2_GAP_NO_CANDIDATE,"Unselected pixel remains unknown");
    source.coverage=1;require(d2_motion_gap_class(&source,nullptr,masks,1,0,epsilon)==D2_GAP_DEPTH_UNAVAILABLE,"Missing pre-HUD snapshot is distinct from rejection");
    filtered.coverage=0;require(d2_motion_gap_class(&source,&filtered,masks,1,0,epsilon)==D2_GAP_DEPTH_REJECTED,"Scene-depth rejection is classified before shader replay");
    filtered.coverage=1;require(d2_motion_gap_class(&source,&filtered,nullptr,1,0,epsilon)==D2_GAP_MASK_UNAVAILABLE,"Unavailable shader mask is not an alpha discard");
    coverage[0]=0;require(d2_motion_gap_class(&source,&filtered,masks,1,0,epsilon)==D2_GAP_SHADER_REJECTED,"Missing own shader coverage is separate");
    coverage[0]=255;depth[0]=.6f;require(d2_motion_gap_class(&source,&filtered,masks,1,0,epsilon)==D2_GAP_DEPTH_MISMATCH,"Original depth mismatch is invalid evidence");
    source.coverage=.25f;require(d2_motion_gap_class(&source,nullptr,masks,1,0,epsilon)==D2_GAP_INVALID,"Malformed fractional owner cannot be misclassified as unselected");
    source.coverage=1;source.delta_x=std::numeric_limits<float>::quiet_NaN();require(d2_motion_gap_class(&source,&filtered,masks,1,0,epsilon)==D2_GAP_INVALID,"Nonfinite motion is invalid");
    puts("PASS gap classification: stationary, no candidate, unavailable depth, rejected depth, own shader, mismatch and invalid evidence");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
