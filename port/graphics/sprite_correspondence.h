#pragma once
#include <math.h>
enum {D2_SPRITE_UV_MATCH=0,D2_SPRITE_UV_INVALID,D2_SPRITE_UV_NOT_QUAD,D2_SPRITE_UV_CHANGED,D2_SPRITE_UV_FLIPPED};
static inline int d2_sprite_uv_equal(const float* a,const float* b){return fabsf(a[0]-b[0])<=1e-6f && fabsf(a[1]-b[1])<=1e-6f;}
static inline int d2_sprite_clip_equal(const float* a,const float* b){
    for(unsigned k=0;k<4;k++)if(fabsf(a[k]-b[k])>1e-6f)return 0;
    return 1;
}
static inline double d2_sprite_triangle_sign(const float* a,const float* b,const float* c){
    double ax=a[0]/a[3],ay=a[1]/a[3],bx=b[0]/b[3],by=b[1]/b[3],cx=c[0]/c[3],cy=c[1]/c[3];
    return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
}
/* Texture-coordinate correspondence within a signature-unique candidate pair.
   No object identity inferred; changed atlas regions and flips fail closed. */
static inline int d2_sprite_correspondence(const float current_uv[6][2],const float previous_uv[6][2],
    const float current_clip[6][4],const float previous_clip[6][4],unsigned previous_index[6]){
    for(unsigned side=0;side<2;side++){
        const float (*uv)[2]=side?previous_uv:current_uv;const float (*clip)[4]=side?previous_clip:current_clip;
        unsigned unique=0;
        for(unsigned v=0;v<6;v++){
            for(unsigned k=0;k<2;k++)if(!isfinite(uv[v][k]))return D2_SPRITE_UV_INVALID;
            for(unsigned k=0;k<4;k++)if(!isfinite(clip[v][k]))return D2_SPRITE_UV_INVALID;
            if(clip[v][3]<=1e-8f)return D2_SPRITE_UV_INVALID;
            int duplicate=0;
            for(unsigned j=0;j<v;j++)if(d2_sprite_uv_equal(uv[v],uv[j])){
                if(!d2_sprite_clip_equal(clip[v],clip[j]))return D2_SPRITE_UV_NOT_QUAD;
                duplicate=1;
            }
            unique+=!duplicate;
        }
        if(unique!=4)return D2_SPRITE_UV_NOT_QUAD;
    }
    for(unsigned v=0;v<6;v++){
        int found=-1;for(unsigned j=0;j<6;j++)if(d2_sprite_uv_equal(current_uv[v],previous_uv[j])){found=(int)j;break;}
        if(found<0)return D2_SPRITE_UV_CHANGED;
        previous_index[v]=(unsigned)found;
    }
    for(unsigned v=0;v<6;v+=3){
        double now=d2_sprite_triangle_sign(current_clip[v],current_clip[v+1],current_clip[v+2]);
        double old=d2_sprite_triangle_sign(previous_clip[previous_index[v]],previous_clip[previous_index[v+1]],previous_clip[previous_index[v+2]]);
        if(!isfinite(now)||!isfinite(old)||fabs(now)<1e-10||fabs(old)<1e-10)return D2_SPRITE_UV_NOT_QUAD;
        if(now*old<0)return D2_SPRITE_UV_FLIPPED;
    }
    return D2_SPRITE_UV_MATCH;
}
