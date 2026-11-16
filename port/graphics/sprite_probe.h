#pragma once
#include <stdint.h>
#include <math.h>
/* A pipeline signature is discovery evidence, never persistent identity. */
typedef struct {uint32_t shader,fragment,texture,vertices,topology;} D2SpriteKey;
static inline int d2_sprite_key_equal(const D2SpriteKey* a,const D2SpriteKey* b){
    return a->shader==b->shader && a->fragment==b->fragment && a->texture==b->texture &&
        a->vertices==b->vertices && a->topology==b->topology;
}
static inline int d2_sprite_unique_pair(const D2SpriteKey* now,uint32_t n,const D2SpriteKey* old,uint32_t m,uint32_t index){
    if(!now || !old || index>=n)return -1;
    unsigned a=0,b=0;int previous=-1;
    for(uint32_t i=0;i<n;i++)a+=d2_sprite_key_equal(&now[index],&now[i]);
    for(uint32_t i=0;i<m;i++)if(d2_sprite_key_equal(&now[index],&old[i])){b++;previous=(int)i;}
    return a==1 && b==1?previous:-1;
}
static inline int d2_sprite_center_changed(const float* a,const float* b){
    if(!a||!b)return 0;
    int moved=0;
    for(unsigned i=0;i<3;i++){
        if(!isfinite(a[i])||!isfinite(b[i]))return 0;
        moved|=fabsf(a[i]-b[i])>.001f;
    }
    return moved;
}

static inline int d2_sprite_transform_finite(const float* rows){
    if(!rows)return 0;
    for(unsigned i=0;i<16;i++)if(!isfinite(rows[i]))return 0;
    return 1;
}
