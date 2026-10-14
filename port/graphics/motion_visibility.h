#pragma once
#include <math.h>
/* Conservative homogeneous-frustum test; does not prove raster coverage. */
static inline int d2_triangle_frustum_candidate(const float a[4],const float b[4],const float c[4]){
    const float* v[3]={a,b,c};
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++)if(!isfinite(v[i][j]))return 0;
    for(unsigned i=0;i<3;i++)if(v[i][3]<=1e-8f)return 0;
    for(unsigned plane=0;plane<6;plane++){
        unsigned outside=0;
        for(unsigned i=0;i<3;i++){
            const float* p=v[i];float distance=plane==0?p[0]+p[3]:plane==1?p[3]-p[0]:
                plane==2?p[1]+p[3]:plane==3?p[3]-p[1]:plane==4?p[2]:p[3]-p[2];
            if(distance<0)outside++;
        }
        if(outside==3)return 0;
    }
    double ax=a[0]/a[3],ay=a[1]/a[3],bx=b[0]/b[3],by=b[1]/b[3],cx=c[0]/c[3],cy=c[1]/c[3];
    return fabs((bx-ax)*(cy-ay)-(by-ay)*(cx-ax))>1e-10;
}
