#include "scenery_feedback.h"
#include <cstdio>
#include <stdexcept>
#include <limits>
#include <cstring>
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    auto a=d2_scenery_group(1,2,3,156),b=d2_scenery_group(1,2,4,156),c=d2_scenery_group(1,2,3,200);
    require(d2_scenery_group_equal(&a,&c)&&!d2_scenery_group_equal(&a,&b),"Material proxies and vertex buckets define groups");
    D2SceneryFeedback records[D2_SCENERY_HISTORY_LIMIT]{};uint32_t count=0;
    require(d2_scenery_feedback_update(records,&count,&a,90,100,1)&&count==1,"Observed useful density learned");
    require(d2_scenery_feedback_update(records,&count,&b,10,100,1)&&count==2,"Different group has independent feedback");
    require(d2_scenery_utility(100,&records[0])>d2_scenery_utility(200,&records[1]),"Useful-density history can outweigh projected area");
    d2_scenery_feedback_update(records,&count,&a,0,100,2);
    require(fabs(records[0].useful_rate-.675)<1e-9,"History adapts gradually to camera/occlusion changes");
    records[1].useful_rate=0;require(d2_scenery_utility(100,&records[1])==20,"Exploration floor prevents permanent starvation");
    require(!d2_scenery_feedback_update(records,&count,&b,101,100,3),"Inconsistent observations rejected");
    require(d2_scenery_feedback_valid(records,count),"Valid feedback accepted");
    auto checksum=d2_scenery_feedback_checksum(records,count*sizeof(records[0]));records[0].age++;
    require(checksum!=d2_scenery_feedback_checksum(records,count*sizeof(records[0])),"Cache corruption changes checksum");
    records[0].useful_rate=std::numeric_limits<double>::quiet_NaN();require(!d2_scenery_feedback_valid(records,count),"Nonfinite cache values rejected");
    records[0]=records[1];require(!d2_scenery_feedback_valid(records,count),"Duplicate cache groups rejected");
    D2SceneryChoice choices[]={{a,5,100},{a,4,90},{b,9,20},{c,2,100}};
    uint32_t selected[4],groups=0;
    require(d2_scenery_cluster_select(choices,4,selected,2,&groups)==2 && groups==2,"Two material groups represented");
    require(choices[selected[0]].draw==2 && choices[selected[1]].draw==9,"Diversity wins over second large draw of same group; tie uses draw id");
    require(d2_scenery_cluster_select(choices,4,selected,3,&groups)==3 && choices[selected[1]].draw==5 && choices[selected[2]].draw==9,"Spare budget fills additional draw without displacing representative");
    require(d2_scenery_cluster_select(choices,4,selected,1,&groups)==1 && choices[selected[0]].draw==2,"More groups than budget selects best representative");
    require(d2_scenery_cluster_select(choices,4,selected,0,&groups)==0,"Zero sample budget safe");
    puts("PASS scenery feedback: clustering proxies, independent useful density, adaptive history, exploration floor and cache validation");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
