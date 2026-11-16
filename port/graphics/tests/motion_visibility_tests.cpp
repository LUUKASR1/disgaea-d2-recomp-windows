#include "motion_visibility.h"
#include <limits>
#include <cstdio>
#include <stdexcept>
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){try{
    float a[4]={-.5f,-.5f,.5f,1},b[4]={0,.5f,.5f,1},c[4]={.5f,-.5f,.5f,1};
    require(d2_triangle_frustum_candidate(a,b,c),"Inside triangle accepted");
    float x[4]={-3,-3,.5f,1},y[4]={0,3,.5f,1},z[4]={3,-3,.5f,1};
    require(d2_triangle_frustum_candidate(x,y,z),"Crossing triangle accepted even with all vertices outside");
    for(unsigned axis=0;axis<3;axis++)for(unsigned side=0;side<2;side++){
        float p[4]={-.5f,-.5f,.5f,1},q[4]={0,.5f,.5f,1},r[4]={.5f,-.5f,.5f,1};
        float shift=side?3.0f:-3.0f;p[axis]+=shift;q[axis]+=shift;r[axis]+=shift;
        require(!d2_triangle_frustum_candidate(p,q,r),"Common outside clipping plane rejected");
    }
    require(!d2_triangle_frustum_candidate(a,a,a),"Degenerate triangle rejected");
    a[0]=std::numeric_limits<float>::quiet_NaN();
    require(!d2_triangle_frustum_candidate(a,b,c),"NaN rejected");
    a[0]=-.5f;a[3]=-1;
    require(!d2_triangle_frustum_candidate(a,b,c),"Behind-camera input rejected");
    puts("PASS motion frustum candidates: inside, crossing, six planes, degenerate, finite and positive w");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
