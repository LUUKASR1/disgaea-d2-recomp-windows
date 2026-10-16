#include "sprite_correspondence.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <limits>
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    float uv[6][2]={{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
    float clip[6][4]={{0,0,.5f,1},{1,0,.5f,1},{0,1,.5f,1},{0,1,.5f,1},{1,0,.5f,1},{1,1,.5f,1}};
    float old_uv[6][2],old_clip[6][4];unsigned indices[6];
    memcpy(old_uv,uv,sizeof(uv));memcpy(old_clip,clip,sizeof(clip));
    require(d2_sprite_correspondence(uv,old_uv,clip,old_clip,indices)==D2_SPRITE_UV_MATCH,"Stationary quad correspondence available");
    for(unsigned v=0;v<6;v++)old_clip[v][0]-=.1f;
    require(d2_sprite_correspondence(uv,old_uv,clip,old_clip,indices)==D2_SPRITE_UV_MATCH,"Translation keeps texture-corner correspondence");
    unsigned order[6]={5,3,4,2,0,1};float reordered_uv[6][2],reordered_clip[6][4];
    for(unsigned v=0;v<6;v++){memcpy(reordered_uv[v],old_uv[order[v]],8);memcpy(reordered_clip[v],old_clip[order[v]],16);}
    require(d2_sprite_correspondence(uv,reordered_uv,clip,reordered_clip,indices)==D2_SPRITE_UV_MATCH && indices[0]==4,"Match UV corners after vertex reorder, not index ordinal");
    for(unsigned v=0;v<6;v++)old_uv[v][0]+=2;
    require(d2_sprite_correspondence(uv,old_uv,clip,old_clip,indices)==D2_SPRITE_UV_CHANGED,"Changed atlas image has unknown pixel correspondence");
    memcpy(old_uv,uv,sizeof(uv));for(unsigned v=0;v<6;v++)old_clip[v][0]=-old_clip[v][0];
    require(d2_sprite_correspondence(uv,old_uv,clip,old_clip,indices)==D2_SPRITE_UV_FLIPPED,"Orientation flip rejected");
    memcpy(old_clip,clip,sizeof(clip));old_clip[3][0]+=.1f;
    require(d2_sprite_correspondence(uv,old_uv,clip,old_clip,indices)==D2_SPRITE_UV_NOT_QUAD,"Duplicated UV with different geometry is ambiguous");
    memcpy(old_clip,clip,sizeof(clip));old_uv[0][0]=std::numeric_limits<float>::quiet_NaN();
    require(d2_sprite_correspondence(uv,old_uv,clip,old_clip,indices)==D2_SPRITE_UV_INVALID,"Invalid UV cannot be matched");
    puts("PASS sprite UV correspondence: stationary, translation, reordered corners, animation, orientation and duplicated geometry");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
