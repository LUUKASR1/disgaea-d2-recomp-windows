#pragma once
#include <stdint.h>
#include <math.h>
#define D2_SCENERY_HISTORY_LIMIT 256
typedef struct {uint32_t shader,fragment,texture,vertex_bucket;} D2SceneryGroup;
typedef struct {D2SceneryGroup group;double useful_rate;uint32_t observations,age;} D2SceneryFeedback;
static inline D2SceneryGroup d2_scenery_group(uint32_t shader,uint32_t fragment,uint32_t texture,uint32_t vertices){
    D2SceneryGroup group={shader,fragment,texture,0};while(vertices>1){group.vertex_bucket++;vertices>>=1;}return group;
}
static inline int d2_scenery_group_equal(const D2SceneryGroup* a,const D2SceneryGroup* b){
    return a->shader==b->shader && a->fragment==b->fragment && a->texture==b->texture && a->vertex_bucket==b->vertex_bucket;
}
static inline int d2_scenery_feedback_find(const D2SceneryFeedback* records,uint32_t count,const D2SceneryGroup* group){
    if(count>D2_SCENERY_HISTORY_LIMIT)return -1;
    for(uint32_t i=0;i<count;i++)if(d2_scenery_group_equal(&records[i].group,group))return (int)i;
    return -1;
}
static inline double d2_scenery_utility(double area,const D2SceneryFeedback* record){
    if(!isfinite(area)||area<=0)return 0;
    double rate=record?record->useful_rate:.5;
    if(!isfinite(rate)||rate<0||rate>1)rate=.5;
    return area*(.2+.8*rate); // Exploration floor avoids permanent starvation after an occluded capture.
}
static inline int d2_scenery_feedback_update(D2SceneryFeedback* records,uint32_t* count,const D2SceneryGroup* group,
    uint64_t kept,uint64_t potential,uint32_t age){
    if(!records||!count||*count>D2_SCENERY_HISTORY_LIMIT||kept>potential)return 0;
    int index=d2_scenery_feedback_find(records,*count,group);
    double observed=potential?(double)kept/(double)potential:0;
    if(index<0){
        if(*count<D2_SCENERY_HISTORY_LIMIT)index=(int)(*count)++;
        else {index=0;for(unsigned i=1;i<*count;i++)if(records[i].age<records[index].age)index=(int)i;}
        records[index].group=*group;records[index].useful_rate=observed;records[index].observations=1;
    }else{
        records[index].useful_rate=records[index].useful_rate*.75+observed*.25;
        if(records[index].observations<1000000)records[index].observations++;
    }
    records[index].age=age;return 1;
}
static inline int d2_scenery_feedback_valid(const D2SceneryFeedback* records,uint32_t count){
    if(count>D2_SCENERY_HISTORY_LIMIT)return 0;
    for(unsigned i=0;i<count;i++){
        if(!isfinite(records[i].useful_rate)||records[i].useful_rate<0||records[i].useful_rate>1||
           records[i].group.vertex_bucket>12||!records[i].observations||records[i].observations>1000000)return 0;
        for(unsigned j=0;j<i;j++)if(d2_scenery_group_equal(&records[i].group,&records[j].group))return 0;
    }
    return 1;
}
static inline uint64_t d2_scenery_feedback_checksum(const void* data,uint32_t bytes){
    uint64_t hash=UINT64_C(14695981039346656037);for(uint32_t i=0;i<bytes;i++){
        hash^=((const unsigned char*)data)[i];hash*=UINT64_C(1099511628211);
    }return hash;
}

/* Bounded selection: one best draw per material-proxy group first, then fill
   spare slots. Scores need not estimate true visible area to enforce diversity. */
typedef struct {D2SceneryGroup group;uint32_t draw;double score;} D2SceneryChoice;
static inline int d2_scenery_choice_better(const D2SceneryChoice* a,const D2SceneryChoice* b){
    return a->score>b->score || (a->score==b->score && a->draw<b->draw);
}
static inline uint32_t d2_scenery_cluster_select(const D2SceneryChoice* choices,uint32_t count,
    uint32_t* selected,uint32_t limit,uint32_t* groups){
    uint32_t used=0;*groups=0;
    for(uint32_t i=0;i<count;i++)if(isfinite(choices[i].score)&&choices[i].score>0){
        int representative=1;
        for(uint32_t j=0;j<count;j++)if(j!=i && d2_scenery_group_equal(&choices[i].group,&choices[j].group) &&
            d2_scenery_choice_better(&choices[j],&choices[i])){representative=0;break;}
        if(!representative)continue;
        (*groups)++;
        uint32_t pos=0;while(pos<used && d2_scenery_choice_better(&choices[selected[pos]],&choices[i]))pos++;
        if(pos>=limit)continue;
        if(used<limit)used++;
        for(uint32_t k=used-1;k>pos;k--)selected[k]=selected[k-1];
        selected[pos]=i;
    }
    while(used<limit){
        int best=-1;
        for(uint32_t i=0;i<count;i++)if(isfinite(choices[i].score)&&choices[i].score>0){
            int exists=0;for(uint32_t j=0;j<used;j++)exists|=selected[j]==i;
            if(!exists && (best<0 || d2_scenery_choice_better(&choices[i],&choices[best])))best=(int)i;
        }
        if(best<0)break;
        selected[used++]=(uint32_t)best;
    }
    /* Ordering only; retain all representatives selected above. */
    for(uint32_t i=1;i<used;i++){
        uint32_t key=selected[i],j=i;
        while(j && d2_scenery_choice_better(&choices[key],&choices[selected[j-1]])){selected[j]=selected[j-1];j--;}
        selected[j]=key;
    }
    return used;
}
