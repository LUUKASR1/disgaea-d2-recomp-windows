#include "scenery_survey.h"
#include <cstdio>
#include <stdexcept>
#include <limits>
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    float positions[2][8]={{0,1,2,1,9,9,9,9},{3,4,5,1,8,8,8,8}};
    auto hash=d2_scenery_positions_hash(positions,2,sizeof(positions[0]));positions[0][7]=42;
    require(hash==d2_scenery_positions_hash(positions,2,sizeof(positions[0])),"Only position attributes affect inventory fingerprint");
    positions[1][0]=7;require(hash!=d2_scenery_positions_hash(positions,2,sizeof(positions[0])),"All positions, not only first vertex, affect fingerprint");
    D2SceneryKey keys[3]={{1,2,3,2,1,hash},{1,2,4,2,1,hash},{1,2,3,2,1,hash}};
    require(d2_scenery_key_matches(keys,3,keys)==2,"Duplicate outside sampled subset is detected by eligible inventory");
    require(d2_scenery_key_matches(keys,2,keys)==1,"Unique inventory signature");
    D2SceneryRank top[2]{};uint32_t count=0;
    d2_scenery_rank_insert(top,&count,2,0,1);d2_scenery_rank_insert(top,&count,2,1,8);d2_scenery_rank_insert(top,&count,2,2,5);
    require(count==2 && top[0].draw==1 && top[1].draw==2,"Large later draw replaces tiny earlier draw without increasing budget");
    d2_scenery_rank_insert(top,&count,2,3,8);require(top[0].draw==1 && top[1].draw==3,"Equal area order deterministic");
    d2_scenery_rank_insert(top,&count,2,4,std::numeric_limits<double>::quiet_NaN());require(top[1].draw==3,"Nonfinite scores rejected");
    float a[4]={-1,-1,.5f,1},b[4]={1,-1,.5f,1},c[4]={-1,1,.5f,1};
    require(fabs(d2_scenery_triangle_area(a,b,c)-2)<1e-6,"Known projected triangle area");
    a[0]=-3;require(d2_scenery_triangle_area(a,b,c)>0 && d2_scenery_triangle_area(a,b,c)<=4,"Crossing triangle clipped to viewport");
    a[2]=b[2]=c[2]=2;require(d2_scenery_triangle_area(a,b,c)==0,"Far plane rejected");
    a[2]=b[2]=c[2]=-.2f;require(d2_scenery_triangle_area(a,b,c)==0,"Near plane rejected");
    a[2]=b[2]=c[2]=.5f;a[3]=0;require(d2_scenery_triangle_area(a,b,c)==0,"Invalid homogeneous position rejected");
    puts("PASS scenery survey: full position hash, inventory duplicates, fixed-budget ranking, deterministic ties and clipped projected area");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
