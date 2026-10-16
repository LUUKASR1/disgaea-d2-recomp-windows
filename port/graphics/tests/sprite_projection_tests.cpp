#include "sprite_projection.h"
#include <cstdio>
#include <stdexcept>
#include <limits>
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    float scale[4]={1,1,.5f,1},offset[4]={0,0,.5f,0},point[4]={.2f,.4f,.9f,2},clip[4];
    require(d2_project_sprite(scale,offset,point,clip)&&fabsf(clip[2]-1.45f)<1e-6f&&clip[3]==2,"Observed shader scale/offset/W preserved");
    D2CameraHypothesis current{},previous{};for(unsigned i=0;i<4;i++){current.rows[i*4+i]=1;current.scale[i]=1;}previous=current;
    float delta[2];require(d2_sprite_camera_delta(&current,&previous,clip,1280,720,delta)&&fabsf(delta[0])<.001f&&fabsf(delta[1])<.001f,"Identity camera produces zero hypothesis motion");
    current.rows[3]=.2f;require(d2_sprite_camera_delta(&current,&previous,clip,1280,720,delta)&&fabsf(delta[0]+128)<.001f,"Current camera translation reprojects stationary point to previous frame");
    current.bias=.5f;require(!d2_sprite_camera_delta(&current,&previous,clip,1280,720,delta),"Nonlinear world bias cannot be silently inverted");
    current.bias=0;current.rows[15]=0;require(!d2_sprite_camera_delta(&current,&previous,clip,1280,720,delta),"Singular matrix rejected");
    point[3]=0;require(!d2_project_sprite(scale,offset,point,clip),"Invalid W rejected");
    point[3]=1;point[0]=std::numeric_limits<float>::quiet_NaN();require(!d2_project_sprite(scale,offset,point,clip),"Nonfinite position rejected");
    puts("PASS sprite projection: captured formula, homogeneous W, camera hypothesis sign, singular/bias rejection and nonfinite input");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
