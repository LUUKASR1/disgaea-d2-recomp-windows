#define D2_SPRITE_PROJECTION_LIMIT 128u
static int d2_sprite_reference_camera(const D2TemporalFrame* frame,D2CameraHypothesis* now,D2CameraHypothesis* old,unsigned* references){
    const D2TemporalFrame* previous=&d2_temporal_frames[d2_temporal_next];int agree=1;*references=0;
    for(unsigned i=0;i<frame->count;i++)if(frame->samples[i].shader==0x99362655u){
        D2StaticPair pair=d2_temporal_static_pair(frame,previous,i);if(pair.previous_index<0)continue;
        const D2TemporalSample* samples[2]={&frame->samples[i],&previous->samples[pair.previous_index]};
        D2CameraHypothesis camera[2]={0};int valid=1;
        for(unsigned side=0;side<2;side++){
            const D2TemporalSample* s=samples[side];
            for(unsigned row=256;row<260;row++)valid=valid && s->copied[row];
            valid=valid && s->copied[466] && s->copied[512] && s->copied[513];
            memcpy(camera[side].rows,s->constants+256*4,16*sizeof(float));
            memcpy(camera[side].scale,s->constants+512*4,4*sizeof(float));
            memcpy(camera[side].offset,s->constants+513*4,4*sizeof(float));camera[side].bias=s->constants[466*4+2];
            double matrix[16],inverse[16];valid=valid && d2_camera_matrix(&camera[side],matrix) && d2_matrix_inverse(matrix,inverse);
        }
        if(!valid)continue;
        if(!*references){*now=camera[0];*old=camera[1];}
        else if(memcmp(now,&camera[0],sizeof(*now)) || memcmp(old,&camera[1],sizeof(*old)))agree=0;
        (*references)++;
    }
    return agree && *references>=2;
}
static unsigned char* d2_sprite_reference_image(size_t size){
    if(!d2_reference_available)return NULL;
    char path[600];snprintf(path,sizeof(path),"%s/selected-reference.bmp",d2_temporal_directory);
    FILE* file=d2_capture_fopen(path,"rb");if(!file)return NULL;
    unsigned char header[54]={0};int valid=fread(header,1,54,file)==54;u32 offset=0,w=0,h=0;unsigned short bits=0;
    memcpy(&offset,header+10,4);memcpy(&w,header+18,4);memcpy(&h,header+22,4);memcpy(&bits,header+28,2);
    valid=valid && header[0]=='B' && header[1]=='M' && offset==54 && bits==24 && w==s_d3d.width && h==s_d3d.height && size==(size_t)w*h;
    unsigned char* image=valid?(unsigned char*)calloc(size,4):NULL;u32 stride=(w*3u+3u)&~3u;
    unsigned char* line=image?(unsigned char*)malloc(stride):NULL;
    if(image && line)for(u32 y=0;y<h;y++){
        if(fread(line,1,stride,file)!=stride){free(image);image=NULL;break;}
        for(u32 x=0;x<w;x++){size_t p=((size_t)(h-1-y)*w+x)*4;image[p]=line[x*3+2];image[p+1]=line[x*3+1];image[p+2]=line[x*3];image[p+3]=255;}
    }
    else {free(image);image=NULL;}
    free(line);fclose(file);return image;
}
static void d2_sprite_overlay_pixel(unsigned char* image,int x,int y){
    if(!image || x<0 || y<0 || (u32)x>=s_d3d.width || (u32)y>=s_d3d.height)return;
    size_t p=((size_t)y*s_d3d.width+x)*4;image[p]=255;image[p+1]=210;image[p+2]=0;image[p+3]=255;
}
static void d2_sprite_overlay_box(unsigned char* image,const float bounds[4],u32 draw){
    for(unsigned i=0;i<4;i++)if(!isfinite(bounds[i]))return;
    if(!image || bounds[2]<0 || bounds[3]<0 || bounds[0]>=s_d3d.width || bounds[1]>=s_d3d.height)return;
    int x0=(int)fmaxf(0,bounds[0]),y0=(int)fmaxf(0,bounds[1]);
    int x1=(int)fminf(s_d3d.width-1,bounds[2]),y1=(int)fminf(s_d3d.height-1,bounds[3]);
    for(int x=x0;x<=x1;x++){d2_sprite_overlay_pixel(image,x,y0);d2_sprite_overlay_pixel(image,x,y1);}
    for(int y=y0;y<=y1;y++){d2_sprite_overlay_pixel(image,x0,y);d2_sprite_overlay_pixel(image,x1,y);}
    static const unsigned char digits[10][7]={{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
    char label[16];snprintf(label,sizeof(label),"%u",draw);
    for(unsigned k=0;label[k];k++)for(unsigned row=0;row<7;row++)for(unsigned col=0;col<5;col++)if(digits[label[k]-'0'][row]&(1u<<(4-col)))
        for(int a=0;a<2;a++)for(int b=0;b<2;b++)d2_sprite_overlay_pixel(image,x0+2+(int)k*12+(int)col*2+a,y0+2+(int)row*2+b);
}
static void d2_sprite_projection_dump(u32 index,const D2TemporalFrame* frame,const D2SpriteFrame* current,const D2SpriteFrame* previous){
    unsigned selected[D2_SPRITE_PROJECTION_LIMIT],paired[D2_SPRITE_PROJECTION_LIMIT],count=0;
    if(current->valid && previous->valid && current->frame==frame->frame && previous->frame+1==current->frame)
        for(unsigned i=0;i<current->count && count<D2_SPRITE_PROJECTION_LIMIT;i++)if(current->keys[i].shader==0x2003CB89u && current->keys[i].vertices==6){
            int old=d2_sprite_unique_pair(current->keys,current->count,previous->keys,previous->count,i);
            if(old>=0 && current->records[i].projected && previous->records[old].projected){selected[count]=i;paired[count++]=(unsigned)old;}
        }
    D2MotionProbeInput* input=count?(D2MotionProbeInput*)calloc(count*12,sizeof(*input)):NULL;
    D2MotionProbeOutput* output=count?(D2MotionProbeOutput*)calloc(count*12,sizeof(*output)):NULL;
    for(unsigned d=0;input && d<count;d++)for(unsigned v=0;v<6;v++)for(unsigned side=0;side<2;side++){
        const D2SpriteRecord* record=side?&previous->records[paired[d]]:&current->records[selected[d]];
        D2MotionProbeInput* in=&input[d*12+v*2+side];
        for(unsigned axis=0;axis<4;axis++)in->current_rows[axis*4+axis]=in->previous_rows[axis*4+axis]=1;
        memcpy(in->point,record->positions[v],4*sizeof(float));
        memcpy(in->current_scale,record->scale,4*sizeof(float));memcpy(in->previous_scale,record->scale,4*sizeof(float));
        memcpy(in->current_offset,record->offset,4*sizeof(float));memcpy(in->previous_offset,record->offset,4*sizeof(float));
        in->dimensions[0]=(float)s_d3d.width;in->dimensions[1]=(float)s_d3d.height;
    }
    int result=count?input && output?d2_motion_probe_gpu(s_d3d.device,s_d3d.cmd_queue,input,count*12,output):(int)E_OUTOFMEMORY:0;
    D2CameraHypothesis camera_now={0},camera_old={0};unsigned references=0;
    int camera=d2_sprite_reference_camera(frame,&camera_now,&camera_old,&references);
    size_t size=(size_t)s_d3d.width*s_d3d.height;unsigned char* image=d2_sprite_reference_image(size);
    char path[600];snprintf(path,sizeof(path),"%s/sprite-projection-check.json",d2_temporal_directory);FILE* file=d2_capture_fopen(path,"wb");
    if(file){
        fprintf(file,"{\"schema\":1,\"purpose\":\"direct-projection-and-camera-hypothesis\",\"capture_index\":%u,\"current_frame\":%llu,"
            "\"render_width\":%u,\"render_height\":%u,\"candidate_quads\":%u,\"candidate_limit\":%u,\"gpu_result\":%d,"
            "\"static_projection_reference_pairs\":%u,\"camera_reprojection_hypothesis_available\":%s,"
            "\"cross_shader_depth_compatibility_validated\":false,\"vertex_correspondence_validated\":false,"
            "\"object_identity_available\":false,\"motion_vectors_available\":false,\"applied_to_game\":false,"
            "\"units\":\"render-pixels\",\"direction\":\"previous-minus-current\",\"samples\":[",
            index,(unsigned long long)current->frame,s_d3d.width,s_d3d.height,count,D2_SPRITE_PROJECTION_LIMIT,result,references,camera?"true":"false");
        for(unsigned d=0;d<count;d++){
            const D2SpriteRecord* now=&current->records[selected[d]];const D2SpriteRecord* old=&previous->records[paired[d]];
            float actual[2]={0},estimate[2]={0},bounds[4]={0};double error=0;int valid=result==0;
            for(unsigned v=0;v<6;v++){
                float cpu[2],gpu[2];int ok=d2_same_point_delta(old->clip[v],now->clip[v],s_d3d.width,s_d3d.height,cpu);
                if(!v && ok){actual[0]=cpu[0];actual[1]=cpu[1];}
                if(result==0 && output){
                    ok=ok && output[d*12+v*2].delta_valid[2]>.5f && output[d*12+v*2+1].delta_valid[2]>.5f &&
                        d2_same_point_delta(output[d*12+v*2+1].previous_clip,output[d*12+v*2].current_clip,s_d3d.width,s_d3d.height,gpu);
                    if(ok)for(unsigned axis=0;axis<2;axis++){double e=fabs((double)cpu[axis]-gpu[axis]);if(e>error)error=e;}
                }
                valid=valid && ok;
                float x=(now->clip[v][0]/now->clip[v][3]+1)*.5f*s_d3d.width;
                float y=(1-now->clip[v][1]/now->clip[v][3])*.5f*s_d3d.height;
                if(!v){bounds[0]=bounds[2]=x;bounds[1]=bounds[3]=y;}
                else {bounds[0]=fminf(bounds[0],x);bounds[1]=fminf(bounds[1],y);bounds[2]=fmaxf(bounds[2],x);bounds[3]=fmaxf(bounds[3],y);}
            }
            int hypothesis=camera && d2_sprite_camera_delta(&camera_now,&camera_old,now->clip[0],s_d3d.width,s_d3d.height,estimate);
            d2_sprite_overlay_box(image,bounds,now->draw);
            fprintf(file,"%s{\"draw\":%u,\"previous_draw\":%u,\"texture\":\"%08X\",\"uv_changed\":%s,"
                "\"projection_cpu_gpu_match\":%s,\"maximum_delta_error_pixels\":%.9g,\"current_screen_bounds\":",d?",":"",now->draw,old->draw,
                current->keys[selected[d]].texture,now->uv_hash!=old->uv_hash?"true":"false",valid && error<=.25?"true":"false",error);
            d2_sprite_json_values(file,bounds,4);fprintf(file,",\"observed_first_vertex_delta_pixels\":");d2_sprite_json_values(file,actual,2);
            fprintf(file,",\"camera_hypothesis_delta_pixels\":");if(hypothesis)d2_sprite_json_values(file,estimate,2);else fprintf(file,"null");
            fprintf(file,",\"unvalidated_residual_pixels\":");if(hypothesis){float residual[2]={actual[0]-estimate[0],actual[1]-estimate[1]};d2_sprite_json_values(file,residual,2);}else fprintf(file,"null");
            fprintf(file,"}");
        }
    }
    int overlay=0;if(image){snprintf(path,sizeof(path),"%s/sprite-candidate-overlay.bmp",d2_temporal_directory);
        cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);overlay=d2_hud_bmp_complete(path);}
    if(file){fprintf(file,"],\"candidate_overlay_written\":%s}\n",overlay?"true":"false");fclose(file);}
    free(image);free(input);free(output);
}
