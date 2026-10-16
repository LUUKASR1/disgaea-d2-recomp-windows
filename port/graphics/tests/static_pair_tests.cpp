#include "static_pair.h"
#include <cstdio>
#include <stdexcept>
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    float a[12]={0,0,0,1,1,0,0,1,0,1,0,1},b[12]={2,0,0,1,3,0,0,1,2,1,0,1};
    D2StaticGeometry now[3]={{1,2,3,3,1,a},{1,2,4,3,1,b},{1,2,3,3,1,a}};
    D2StaticGeometry old[3]={now[1],now[0],now[0]};
    auto pair=d2_static_pair_select(now,2,old,2,0);
    require(pair.previous_index==1 && pair.current_matches==1 && pair.previous_matches==1,"Reordered captures pair by complete geometry, not draw order");
    pair=d2_static_pair_select(now,3,old,2,0);
    require(pair.previous_index<0 && pair.current_matches==2,"Two current instances cannot borrow one previous sample");
    pair=d2_static_pair_select(now,2,old,3,0);
    require(pair.previous_index<0 && pair.previous_matches==2,"Duplicated previous geometry rejected");
    float changed[12];for(unsigned i=0;i<12;i++)changed[i]=a[i];changed[8]=.1f;old[1].positions=changed;
    require(d2_static_pair_select(now,2,old,2,0).previous_index<0,"Full geometry changes rejected even when first point is unchanged");
    old[1]=now[0];old[1].texture++;
    require(d2_static_pair_select(now,2,old,2,0).previous_index<0,"Different texture rejected");
    old[1]=now[0];old[1].cull++;
    require(d2_static_pair_select(now,2,old,2,0).previous_index<0,"Different culling rejected");
    old[1]=now[0];old[1].vertices=0;
    require(d2_static_pair_select(now,2,old,2,0).previous_index<0,"Ineligible geometry rejected");
    require(d2_static_pair_select(now,2,old,2,2).previous_index<0,"Out-of-range selection rejected");
    puts("PASS bounded static geometry pairs: reorder, symmetric uniqueness, full geometry, texture, culling and eligibility");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
