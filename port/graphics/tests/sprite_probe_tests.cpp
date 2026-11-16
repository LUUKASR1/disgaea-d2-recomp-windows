#include "sprite_probe.h"
#include <cstdio>
#include <stdexcept>
#include <limits>
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){try{
    D2SpriteKey a{1,2,3,6,4},b{1,2,4,6,4};D2SpriteKey now[]={a,b},old[]={b,a};
    require(d2_sprite_unique_pair(now,2,old,2,0)==1,"Reordering uses signature, never draw ordinal");
    old[0]=a;require(d2_sprite_unique_pair(now,2,old,2,0)<0,"Two sprites sharing signature are ambiguous");
    old[0]=b;now[1]=a;require(d2_sprite_unique_pair(now,2,old,2,0)<0,"Ambiguity in either inventory rejected");
    float p[3]={1,2,3},q[3]={1,2,3};require(!d2_sprite_center_changed(p,q),"Stationary center does not arm motion event");
    q[0]=2;require(d2_sprite_center_changed(p,q),"Object-attribute movement detected");
    q[1]=std::numeric_limits<float>::quiet_NaN();require(!d2_sprite_center_changed(p,q),"Invalid geometry cannot trigger capture");
    float matrix[16]={0};require(d2_sprite_transform_finite(matrix),"Finite reference rows available");
    matrix[0]=std::numeric_limits<float>::quiet_NaN();require(!d2_sprite_transform_finite(matrix),"Nonfinite reference cannot claim stationary camera evidence");
    puts("PASS sprite discovery: reordered signatures, shared-atlas ambiguity, stationary, moved and invalid centers");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
