#pragma once
#include <math.h>
/* Position rule observed for VP 99362655; not a complete camera/object model. */
static inline int d2_project_world(const float rows[16],const float scale[4],const float offset[4],
    float bias,const float point[4],float clip[4]){
    float q[4];
    for(unsigned row=0;row<4;row++){
        q[row]=rows[row*4]*point[0]+rows[row*4+1]*point[1];
        q[row]+=rows[row*4+2]*point[2];q[row]+=rows[row*4+3]*point[3];
        if(!isfinite(q[row]))return 0;
    }
    if(!isfinite(bias)||q[3]<=1e-8f || q[3]+bias<=1e-8f)return 0;
    float correction=bias/q[3],w=q[3]+bias;
    q[0]+=q[0]*correction;q[1]+=q[1]*correction;q[2]+=bias;
    for(unsigned axis=0;axis<3;axis++){
        clip[axis]=q[axis]*scale[axis]+w*offset[axis];
        if(!isfinite(clip[axis]))return 0;
    }
    clip[3]=w;return 1;
}
static inline int d2_same_point_delta(const float previous[4],const float current[4],
    unsigned width,unsigned height,float delta[2]){
    if(!width||!height||previous[3]<=1e-8f||current[3]<=1e-8f)return 0;
    for(unsigned i=0;i<4;i++)if(!isfinite(previous[i])||!isfinite(current[i]))return 0;
    delta[0]=(previous[0]/previous[3]-current[0]/current[3])*.5f*(float)width;
    delta[1]=(current[1]/current[3]-previous[1]/previous[3])*.5f*(float)height;
    return isfinite(delta[0])&&isfinite(delta[1]);
}
