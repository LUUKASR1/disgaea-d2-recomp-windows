#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
typedef struct {uint32_t shader,fragment,texture,vertices,cull;uint64_t positions_hash;} D2SceneryKey;
typedef struct {uint32_t draw;double area;} D2SceneryRank;
/* Every position byte participates. A hash collision can only make inventory
   uniqueness more conservative; selected pairs still require full memcmp. */
static inline uint64_t d2_scenery_positions_hash(const void* data,uint32_t count,size_t stride){
    if(!data || !count || count>4096 || stride<16)return 0;
    uint64_t hash=UINT64_C(14695981039346656037);
    for(uint32_t v=0;v<count;v++)for(unsigned b=0;b<16;b++){
        hash^=((const unsigned char*)data)[(size_t)v*stride+b];hash*=UINT64_C(1099511628211);
    }
    return hash;
}
static inline uint32_t d2_scenery_key_matches(const D2SceneryKey* keys,uint32_t count,const D2SceneryKey* key){
    uint32_t matches=0;if(!keys || !key || !key->vertices)return 0;
    for(uint32_t i=0;i<count;i++)matches+=keys[i].shader==key->shader && keys[i].fragment==key->fragment &&
        keys[i].texture==key->texture && keys[i].vertices==key->vertices && keys[i].cull==key->cull && keys[i].positions_hash==key->positions_hash;
    return matches;
}
static inline void d2_scenery_rank_insert(D2SceneryRank* out,uint32_t* count,uint32_t limit,uint32_t draw,double area){
    if(!out||!count||!limit||!isfinite(area)||area<=0)return;
    uint32_t n=*count;if(n>limit)return;uint32_t pos=0;
    while(pos<n && (out[pos].area>area || (out[pos].area==area && out[pos].draw<draw)))pos++;
    if(pos>=limit)return;
    uint32_t last=n<limit?n:limit-1;
    for(uint32_t i=last;i>pos;i--)out[i]=out[i-1];
    out[pos].draw=draw;out[pos].area=area;if(n<limit)*count=n+1;
}
/* Homogeneous six-plane clipping, then projected polygon area. This estimates
   geometry footprint, not occlusion/alpha/culling or union of overlapping tris. */
static inline double d2_scenery_triangle_area(const float* a,const float* b,const float* c){
    float polygon[16][4],clipped[16][4];const float* input[3]={a,b,c};unsigned n=3;
    for(unsigned i=0;i<3;i++)for(unsigned k=0;k<4;k++){
        if(!isfinite(input[i][k]))return 0;
        polygon[i][k]=input[i][k];
    }
    for(unsigned i=0;i<3;i++)if(polygon[i][3]<=1e-8f)return 0;
    for(unsigned plane=0;plane<6 && n;plane++){
        unsigned m=0;
        for(unsigned i=0;i<n;i++){
            const float* p=polygon[i];const float* q=polygon[(i+1)%n];
            float dp=plane==0?p[0]+p[3]:plane==1?p[3]-p[0]:plane==2?p[1]+p[3]:plane==3?p[3]-p[1]:plane==4?p[2]:p[3]-p[2];
            float dq=plane==0?q[0]+q[3]:plane==1?q[3]-q[0]:plane==2?q[1]+q[3]:plane==3?q[3]-q[1]:plane==4?q[2]:q[3]-q[2];
            if(dp>=0){if(m>=16)return 0;memcpy(clipped[m++],p,16);}
            if((dp>=0)!=(dq>=0)){
                float t=dp/(dp-dq);if(m>=16 || !isfinite(t))return 0;
                for(unsigned k=0;k<4;k++)clipped[m][k]=p[k]+t*(q[k]-p[k]);
                m++;
            }
        }
        n=m;memcpy(polygon,clipped,n*16);
    }
    if(n<3)return 0;
    double sum=0;
    for(unsigned i=0;i<n;i++){
        const float* p=polygon[i];const float* q=polygon[(i+1)%n];if(p[3]<=1e-8f||q[3]<=1e-8f)return 0;
        sum+=(double)p[0]/p[3]*((double)q[1]/q[3])-(double)q[0]/q[3]*((double)p[1]/p[3]);
    }
    return isfinite(sum)?fabs(sum)*.5:0;
}
