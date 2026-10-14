#include "motion_layers.h"
#include <cstdio>
#include <stdexcept>
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){try{
    float epsilon=8.f/16777215.f;unsigned char yes[1]={255},no[1]={0};float z[1]={.8f};
    D2MotionMask mask{yes,z,1},owners[2]={{no,z,1},{yes,z,1}};
    D2MotionPixel behind{0,0,1,.8f},front{7,8,1,.2f},empty{},out{};D2MotionLayer layer{};
    require(d2_motion_layer_add(&layer,&behind,&mask,0,.8f,epsilon,9)==1,"Own shader and pre-HUD depth validate rear candidate");
    require(d2_motion_layer_recover(&layer,&front,&empty,owners,2,0,&out)==1 && out.coverage==1 && out.delta_x==0,"Rejected front can recover validated stationary rear vector");
    owners[0].coverage=yes;require(!d2_motion_layer_recover(&layer,&front,&empty,owners,2,0,&out)&&out.coverage==0,"Depth-rejected surviving front shader is not a recovery permission");
    owners[0].coverage=nullptr;require(!d2_motion_layer_recover(&layer,&front,&empty,owners,2,0,&out),"Unavailable front mask fails closed");owners[0].coverage=no;
    behind.delta_x=2;require(d2_motion_layer_add(&layer,&behind,&mask,0,.8f,epsilon,10)&&layer.ambiguous,"Overlapping candidates with conflicting motion are ambiguous");
    require(!d2_motion_layer_recover(&layer,&front,&empty,owners,2,0,&out),"Conflict cannot fill hole");
    D2MotionLayer rejected{};require(!d2_motion_layer_add(&rejected,&behind,&mask,0,.2f,epsilon,10),"Rear layer occluded by scene depth is rejected");
    z[0]=.6f;require(!d2_motion_layer_add(&rejected,&behind,&mask,0,.8f,epsilon,10),"Geometry/original-shader depth mismatch rejected");
    D2MotionPixel baseline{1,2,1,.4f};require(!d2_motion_layer_recover(&layer,&front,&baseline,owners,2,0,&out)&&out.delta_x==1&&out.delta_y==2,"Baseline retained unchanged even if layer experiment is ambiguous");
    front.coverage=0;require(!d2_motion_layer_eligible(&front,owners,2,0),"No selected front owner cannot be inferred");
    puts("PASS layers: own shader/depth, rejected front, rear visibility, unknown mask, conflicting vectors and baseline preservation");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
