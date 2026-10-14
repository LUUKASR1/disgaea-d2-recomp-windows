#pragma once
#include <stdint.h>
#include <string.h>
/* Unique only within these captured sets; not a persistent object identity. */
typedef struct {uint32_t shader,fragment,texture,vertices,cull;const float* positions;} D2StaticGeometry;
typedef struct {uint32_t current_matches,previous_matches;int previous_index;} D2StaticPair;
static inline int d2_static_geometry_equal(const D2StaticGeometry* a,const D2StaticGeometry* b){
    return a && b && a->positions && b->positions && a->vertices && a->vertices<=4096 &&
        a->shader==b->shader && a->fragment==b->fragment && a->texture==b->texture &&
        a->vertices==b->vertices && a->cull==b->cull &&
        memcmp(a->positions,b->positions,(size_t)a->vertices*4*sizeof(float))==0;
}
static inline D2StaticPair d2_static_pair_select(const D2StaticGeometry* current,uint32_t current_count,
    const D2StaticGeometry* previous,uint32_t previous_count,uint32_t index){
    D2StaticPair pair={0,0,-1};
    if(!current || !previous || current_count>64 || previous_count>64 || index>=current_count)return pair;
    for(uint32_t j=0;j<current_count;j++)pair.current_matches+=d2_static_geometry_equal(current+index,current+j);
    int found=-1;
    for(uint32_t j=0;j<previous_count;j++)if(d2_static_geometry_equal(current+index,previous+j)){
        pair.previous_matches++;found=(int)j;
    }
    if(pair.current_matches==1 && pair.previous_matches==1)pair.previous_index=found;
    return pair;
}
