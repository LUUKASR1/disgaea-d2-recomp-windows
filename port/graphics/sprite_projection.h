#pragma once
#include "projection.h"
/* Exact position formula observed in captured VP 2003CB89 assembly. */
static inline int d2_project_sprite(const float* scale,const float* offset,const float* position,float* clip){
    if(!scale||!offset||!position||!clip || !isfinite(position[3]) || position[3]<=1e-8f)return 0;
    for(unsigned i=0;i<3;i++){
        clip[i]=position[i]*scale[i]+position[3]*offset[i];
        if(!isfinite(clip[i]))return 0;
    }
    clip[3]=position[3];return 1;
}
typedef struct {float rows[16],scale[4],offset[4],bias;} D2CameraHypothesis;
static inline int d2_camera_matrix(const D2CameraHypothesis* input,double* matrix){
    if(!input||!matrix||!isfinite(input->bias)||fabsf(input->bias)>1e-8f)return 0;
    for(unsigned row=0;row<4;row++)for(unsigned col=0;col<4;col++){
        double v=row==3?input->rows[12+col]:
            (double)input->rows[row*4+col]*input->scale[row]+(double)input->rows[12+col]*input->offset[row];
        if(!isfinite(v))return 0;
        matrix[row*4+col]=v;
    }
    return 1;
}
static inline int d2_matrix_inverse(const double* matrix,double* inverse){
    double a[4][8];
    for(unsigned r=0;r<4;r++)for(unsigned c=0;c<8;c++)a[r][c]=c<4?matrix[r*4+c]:(double)(c-4==r);
    for(unsigned c=0;c<4;c++){
        unsigned pivot=c;for(unsigned r=c+1;r<4;r++)if(fabs(a[r][c])>fabs(a[pivot][c]))pivot=r;
        if(!isfinite(a[pivot][c])||fabs(a[pivot][c])<1e-12)return 0;
        if(pivot!=c)for(unsigned k=0;k<8;k++){double t=a[c][k];a[c][k]=a[pivot][k];a[pivot][k]=t;}
        double divisor=a[c][c];for(unsigned k=0;k<8;k++)a[c][k]/=divisor;
        for(unsigned r=0;r<4;r++)if(r!=c){double factor=a[r][c];for(unsigned k=0;k<8;k++)a[r][k]-=factor*a[c][k];}
    }
    for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++){
        if(!isfinite(a[r][c+4]))return 0;
        inverse[r*4+c]=a[r][c+4];
    }
    return 1;
}
/* Hypothesis only: direct-path sprite depth is assumed compatible with the
   world reference projection. Does not establish object identity or visibility. */
static inline int d2_sprite_camera_delta(const D2CameraHypothesis* current,const D2CameraHypothesis* previous,
    const float* clip,unsigned width,unsigned height,float* delta){
    double now[16],before[16],inv[16],world[4]={0},projected[4]={0};float old_clip[4];
    if(!clip || !isfinite(clip[3]) || clip[3]<=1e-8f || !d2_camera_matrix(current,now) ||
       !d2_camera_matrix(previous,before) || !d2_matrix_inverse(now,inv))return 0;
    for(unsigned i=0;i<4;i++)if(!isfinite(clip[i]))return 0;
    for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++)world[r]+=inv[r*4+c]*clip[c];
    for(unsigned r=0;r<4;r++){
        for(unsigned c=0;c<4;c++)projected[r]+=before[r*4+c]*world[c];
        old_clip[r]=(float)projected[r];
    }
    return d2_same_point_delta(old_clip,clip,width,height,delta);
}
