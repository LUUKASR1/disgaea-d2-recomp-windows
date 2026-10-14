/* Diagnostic snapshots only: never treat draw order as persistent object identity. */
#include "temporal_probe.h"
#include "projection.h"
#include "auto_probe.h"
#include "motion_probe.h"
#include "motion_raster.h"
#include "motion_visibility.h"
#define D2_GEOMETRY_LIMIT 4096
#define D2_TEMP_SAMPLES 4
typedef struct {
    u32 draw,shader,fp,vertices,texture,cb_slot;
    int has_vertex;float attributes[64];unsigned char copied[D2_TEMP_ROWS];unsigned copied_rows;
    float constants[D2_TEMP_ROWS*4];
    unsigned geometry_count,cull,geometry_reject,topology,blend,alpha_ctl,stencil,viewport[4];float positions[D2_GEOMETRY_LIMIT][4];
} D2TemporalSample;
typedef struct {
    u64 frame;LARGE_INTEGER time;u32 count;int valid;
    D2TemporalSample samples[D2_TEMP_SAMPLES];
} D2TemporalFrame;
static D2TemporalFrame d2_temporal_frames[2];
static unsigned d2_temporal_next;
static D2AutoProbe d2_auto_probe;
static char d2_temporal_directory[512];
static int d2_reference_attempted,d2_reference_available;
static u64 d2_reference_frame;
static char d2_reference_frame_text[32]="null";
static int d2_temporal_enabled(void){
    static int enabled=-1;
    if(enabled<0){const char* e=getenv("D2_CAPTURE_MASK_STATE");enabled=e && strcmp(e,"1")==0;}
    return enabled;
}
static int d2_temporal_request(u32* index,HWND window){
    if(!d2_temporal_enabled())return d2_capture_next(index,window);
    if(d2_auto_probe.phase){*index=d2_auto_probe.index;return d2_auto_capture_frozen(&d2_auto_probe,GetTickCount64());}
    if(!d2_capture_next(index,window))return 0;
    d2_auto_arm(&d2_auto_probe,*index,GetTickCount64());d2_temporal_next=0;
    d2_reference_attempted=d2_reference_available=0;d2_reference_frame=0;
    snprintf(d2_reference_frame_text,sizeof(d2_reference_frame_text),"null");
    fprintf(stderr,"[D2TEMP] armed capture=%u; rotate camera within 10 seconds\n",*index);
    memset(d2_temporal_frames,0,sizeof(d2_temporal_frames));
    return 0; /* Observe until a stable scene sample changes transform or timeout. */
}

/* Bits describe capture eligibility; no render state is changed. */
static unsigned d2_geometry_rejection(const D3D12DrawRecord* r){
    unsigned flags=0;
    if(r->d2_capture_vs_hash!=0x99362655u)flags|=1u;
    if(!s_d3d.vp_vb_mapped || !r->vertex_count || r->vb_byte_offset%256u ||
       r->vb_byte_offset/256u>=MAX_VERTICES)flags|=2u;
    int full_view=r->vp_w<2 || r->vp_h<2 ||
        (!r->vp_x && !r->vp_y && r->vp_w==d2_graphics_guest_width() && r->vp_h==d2_graphics_guest_height());
    if(!full_view)flags|=4u;
    if(r->topology!=RSX_TOPOLOGY_TRIANGLES || r->vertex_count%3u)flags|=8u;
    if(r->vertex_count>D2_GEOMETRY_LIMIT)flags|=16u;
    if(r->vb_byte_offset/256u>=MAX_VERTICES ||
       r->vertex_count>MAX_VERTICES-r->vb_byte_offset/256u)flags|=32u;
    if(r->blend)flags|=64u;
    if(r->alpha_ctl>>16)flags|=128u;
    if(r->d2_zs.stencil)flags|=256u;
    if(getenv("CULL_OFF") || getenv("CULL_FLIP"))flags|=512u;
    return flags;
}


static unsigned d2_geometry_view_rejection(const D3D12DrawRecord* r,u32 parity){
    const float* bank=(const float*)((const char*)s_d3d.vp_cb_mapped+((u64)parity*MAX_DRAWS+r->cb_slot)*VP_CB_STRIDE);
    const char* vertices=(const char*)s_d3d.vp_vb_mapped+(u64)parity*MAX_VERTICES*256u+r->vb_byte_offset;
    unsigned candidates=0;float clip[3][4];
    for(unsigned v=0;v<r->vertex_count;v+=3){
        for(unsigned k=0;k<3;k++)if(!d2_project_world(bank+256*4,bank+512*4,bank+513*4,bank[466*4+2],
            (const float*)(vertices+(size_t)(v+k)*256u),clip[k]))return 2048u;
        if(d2_triangle_frustum_candidate(clip[0],clip[1],clip[2]))candidates++;
    }
    return candidates?0u:1024u;
}

static void d2_temporal_observe(u32 count,u32 hud_start){
    if(!d2_temporal_enabled() || d2_auto_probe.phase!=D2_AUTO_WAITING || !count)return;
    D2TemporalFrame* frame=&d2_temporal_frames[d2_temporal_next];
    frame->valid=0;frame->count=0;frame->frame=s_d3d.frame_count;QueryPerformanceCounter(&frame->time);
    if(s_d3d.vp_cb_mapped){
        u32 parity=(u32)(s_d3d.vp_parity^1);
        for(unsigned pass=0;pass<2;pass++)for(u32 d=0;d<count && d<MAX_DRAWS && d<hud_start && frame->count<D2_TEMP_SAMPLES;d++){
            const D3D12DrawRecord* r=&s_d3d.draws[d];
            if(!r->is_vp || r->is_clear || r->rt_off || r->rt_mrt[0] || r->rt_mrt[1] || r->rt_mrt[2] ||
               !r->d2_capture_vs_hash || r->cb_slot>=MAX_DRAWS)continue;
            if(d2_hud_record_candidate(r,parity))continue;
            unsigned same=0;
            for(u32 i=0;i<frame->count;i++)if(frame->samples[i].shader==r->d2_capture_vs_hash)same++;
            if(same>=2)continue;
            unsigned geometry_reject=d2_geometry_rejection(r);
            /* Alpha/blend remain reported but cannot block a geometry-only
               diagnostic. They MUST still block treating this as scene visibility. */
            if(!(geometry_reject & ~(64u|128u)))geometry_reject|=d2_geometry_view_rejection(r,parity);
            unsigned structural_reject=geometry_reject & ~(64u|128u);
            if((pass==0 && structural_reject) || (pass==1 && !structural_reject))continue;
            D2TemporalSample* sample=&frame->samples[frame->count++];
            sample->geometry_count=0;sample->cull=r->cull;
            sample->geometry_reject=geometry_reject;sample->topology=r->topology;
            sample->blend=(unsigned)r->blend;sample->alpha_ctl=r->alpha_ctl;sample->stencil=(unsigned)r->d2_zs.stencil;
            sample->viewport[0]=r->vp_x;sample->viewport[1]=r->vp_y;
            sample->viewport[2]=r->vp_w;sample->viewport[3]=r->vp_h;
            sample->draw=d;sample->shader=r->d2_capture_vs_hash;sample->fp=r->fp_addr;
            sample->vertices=r->vertex_count;sample->texture=r->tex[0].raw;sample->cb_slot=r->cb_slot;
            const void* bank=(const char*)s_d3d.vp_cb_mapped+((u64)parity*MAX_DRAWS+r->cb_slot)*VP_CB_STRIDE;
            memset(sample->copied,0,sizeof(sample->copied));sample->copied_rows=0;
            const unsigned rows[]={256,257,258,259,466,512,513};
            for(unsigned row_index=0;row_index<7;row_index++){
                unsigned row=rows[row_index];
                if(sample->shader!=0x99362655u && row<512)continue;
                memcpy(sample->constants+row*4,(const float*)bank+row*4,4*sizeof(float));
                sample->copied[row]=1;sample->copied_rows++;
            }
            sample->has_vertex=s_d3d.vp_vb_mapped && r->vertex_count>0 && r->vb_byte_offset%256u==0 &&
                r->vb_byte_offset/256u<MAX_VERTICES;
            if(sample->has_vertex){
                const void* vertex=(const char*)s_d3d.vp_vb_mapped+(u64)parity*MAX_VERTICES*256u+r->vb_byte_offset;
                memcpy(sample->attributes,vertex,sizeof(sample->attributes));
                if(!(geometry_reject & ~(64u|128u))){
                    sample->geometry_count=r->vertex_count;
                    for(unsigned v=0;v<sample->geometry_count;v++)
                        memcpy(sample->positions[v],(const char*)vertex+(size_t)v*256u,4*sizeof(float));
                }
            }
        }
        frame->valid=1;
    }
    const D2TemporalFrame* previous=&d2_temporal_frames[d2_temporal_next^1];
    int changed=0;
    if(previous->valid && frame->valid)for(u32 i=0;i<frame->count && !changed;i++){
        const D2TemporalSample* sample=&frame->samples[i];
        if(sample->shader!=0x99362655u || !sample->has_vertex)continue;
        for(u32 j=0;j<previous->count;j++){
            const D2TemporalSample* old=&previous->samples[j];
            if(old->shader!=sample->shader || old->fp!=sample->fp || old->vertices!=sample->vertices ||
               old->texture!=sample->texture || !old->has_vertex || memcmp(old->attributes,sample->attributes,4*sizeof(float)))continue;
            if(d2_transform_changed(old->constants+256*4,sample->constants+256*4)){changed=1;break;}
        }
    }
    d2_temporal_next^=1;
    d2_auto_observed(&d2_auto_probe,changed,GetTickCount64());
    if(d2_auto_probe.phase==D2_AUTO_READY)fprintf(stderr,"[D2TEMP] selection=%s capture=%u\n",
        d2_auto_probe.changed?"transform-changed":"timeout-no-transform-change",d2_auto_probe.index);
}

/* Called after frame submission/observation, before any subsequent game frame. */
static void d2_temporal_reference(u32 fi){
    if(!d2_temporal_enabled() || d2_auto_probe.phase!=D2_AUTO_READY || d2_reference_attempted)return;
    d2_reference_attempted=1;
    const D2TemporalFrame* current=&d2_temporal_frames[d2_temporal_next^1];
    if(!current->valid || current->frame!=s_d3d.frame_count || !s_d3d.render_targets[fi])return;
    const char* base=d2_capture_directory();char path[600];
    if(!base || !*base)base="rsxcap";
    d2_capture_mkdir(base);
    snprintf(d2_temporal_directory,sizeof(d2_temporal_directory),"%s/frame_%02u",base,d2_auto_probe.index);
    d2_capture_mkdir(d2_temporal_directory);
    snprintf(path,sizeof(path),"%s/selected-reference.bmp",d2_temporal_directory);
    /* Truncate first; a failed readback cannot validate an old file by its size. */
    FILE* truncate=d2_capture_fopen(path,"wb");if(!truncate)return;fclose(truncate);
    cap_readback_write(s_d3d.render_targets[fi],s_d3d.width,s_d3d.height,DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_PRESENT,fi,path);
    if(!d2_hud_bmp_complete(path))return;
    FILE* file=d2_capture_fopen(path,"rb");if(!file)return;
    unsigned char header[54];size_t bytes=fread(header,1,sizeof(header),file);fclose(file);
    int32_t width=0,height=0;
    if(bytes==sizeof(header)){memcpy(&width,header+18,4);memcpy(&height,header+22,4);}
    if(bytes!=sizeof(header) || header[0]!='B' || header[1]!='M' ||
       width!=(int32_t)s_d3d.width || height!=(int32_t)s_d3d.height)return;
    d2_reference_available=1;d2_reference_frame=current->frame;
    snprintf(d2_reference_frame_text,sizeof(d2_reference_frame_text),"%llu",(unsigned long long)d2_reference_frame);
    fprintf(stderr,"[D2MOTION] selected reference frame=%llu captured before next frame\n",
        (unsigned long long)d2_reference_frame);
}

static D2ConstantReads d2_temporal_reads(u32 hash,int* available){
    D2ConstantReads reads={0};*available=0;
    for(int i=0;i<s_d3d.vp_vs_n && i<VP_VS_CACHE;i++){
        VPVSEntry* entry=&s_d3d.vp_vs[i];if(entry->hash!=hash || !entry->vs)continue;
        ID3DBlob* assembly=NULL;
        HRESULT hr=D3DDisassemble(entry->vs->lpVtbl->GetBufferPointer(entry->vs),
            entry->vs->lpVtbl->GetBufferSize(entry->vs),0,NULL,&assembly);
        if(SUCCEEDED(hr) && assembly){
            reads=d2_constant_reads((const char*)assembly->lpVtbl->GetBufferPointer(assembly));*available=1;
            char path[600];snprintf(path,sizeof(path),"%s/vp_%08X.asm",d2_temporal_directory,hash);
            FILE* shader=d2_capture_fopen(path,"wb");
            if(shader){fwrite(assembly->lpVtbl->GetBufferPointer(assembly),1,strlen((const char*)assembly->lpVtbl->GetBufferPointer(assembly)),shader);fclose(shader);}
            assembly->lpVtbl->Release(assembly);
        }
        break;
    }
    return reads;
}
static void d2_temporal_json_frame(FILE* file,const D2TemporalFrame* frame){
    fprintf(file,"{\"valid\":%s,\"frame\":%llu,\"samples\":[",frame->valid?"true":"false",(unsigned long long)frame->frame);
    for(u32 i=0;i<frame->count;i++){
        const D2TemporalSample* sample=&frame->samples[i];int available=0;
        D2ConstantReads reads=d2_temporal_reads(sample->shader,&available);
        fprintf(file,"%s{\"draw\":%u,\"shader\":\"%08X\",\"fp\":\"%08X\",\"vertices\":%u,\"texture0\":\"%08X\","
            "\"constant_slot\":%u,\"shader_reads_available\":%s,\"indirect_reads\":%s,\"referenced_rows\":%u,\"captured_rows\":%u,\"capture_scope\":\"transform-only\",\"constants\":[",
            i?",":"",sample->draw,sample->shader,sample->fp,sample->vertices,sample->texture,sample->cb_slot,
            available?"true":"false",reads.indirect?"true":"false",reads.count,sample->copied_rows);
        unsigned shown=0;
        for(unsigned row=0;row<D2_TEMP_ROWS;row++)if(reads.used[row] && sample->copied[row]){
            const float* v=sample->constants+row*4;
            fprintf(file,"%s{\"row\":%u,\"values\":[",shown++?",":"",row);
            for(unsigned k=0;k<4;k++){if(isfinite(v[k]))fprintf(file,"%s%.9g",k?",":"",(double)v[k]);else fprintf(file,"%snull",k?",":"");}
            fprintf(file,"]}");
        }
        fprintf(file,"],\"first_vertex_attributes\":[");
        if(sample->has_vertex)for(unsigned attribute=0;attribute<16;attribute++){
            fprintf(file,"%s[",attribute?",":"");
            for(unsigned component=0;component<4;component++){
                float value=sample->attributes[attribute*4+component];
                if(isfinite(value))fprintf(file,"%s%.9g",component?",":"",(double)value);else fprintf(file,"%snull",component?",":"");
            }
            fprintf(file,"]");
        }
        fprintf(file,"]}");
    }
    fprintf(file,"]}");
}
static void d2_projection_dump(u32 index,const D2TemporalFrame* current,const D2TemporalFrame* previous){
    char path[600];snprintf(path,sizeof(path),"%s/projection-check.json",d2_temporal_directory);
    FILE* file=d2_capture_fopen(path,"wb");if(!file)return;
    fprintf(file,"{\"schema\":1,\"capture_index\":%u,\"purpose\":\"same-point-transform-validation\","
        "\"motion_vectors_available\":false,\"object_identity_available\":false,\"current_frame\":%llu,\"previous_frame\":%llu,"
        "\"render_width\":%u,\"render_height\":%u,\"automatic_selection\":true,\"selection_status\":\"%s\","
        "\"capture_image_frame\":%llu,\"samples\":[",index,
        (unsigned long long)current->frame,(unsigned long long)previous->frame,s_d3d.width,s_d3d.height,
        !d2_temporal_enabled()?"not-armed":d2_auto_probe.changed?"transform-changed":"timeout-no-transform-change",
        (unsigned long long)s_d3d.frame_count);
    unsigned shown=0;
    for(u32 i=0;i<current->count;i++){
        const D2TemporalSample* sample=&current->samples[i];
        if(sample->shader!=0x99362655u || !sample->has_vertex)continue;
        const D2TemporalSample* old=NULL;
        for(u32 j=0;j<previous->count;j++){
            const D2TemporalSample* candidate=&previous->samples[j];
            if(candidate->shader==sample->shader && candidate->fp==sample->fp &&
               candidate->vertices==sample->vertices && candidate->texture==sample->texture){old=candidate;break;}
        }
        float now[4]={0},before[4]={0},delta[2]={0};
        int valid=d2_project_world(sample->constants+256*4,sample->constants+512*4,sample->constants+513*4,
            sample->constants[466*4+2],sample->attributes,now);
        int paired=old && old->has_vertex && valid &&
            d2_project_world(old->constants+256*4,old->constants+512*4,old->constants+513*4,
                old->constants[466*4+2],sample->attributes,before) &&
            d2_same_point_delta(before,now,s_d3d.width,s_d3d.height,delta);
        fprintf(file,"%s{\"draw\":%u,\"shader\":\"99362655\",\"position_valid\":%s,\"signature_match_is_not_object_identity\":true,"
            "\"paired\":%s,\"transform_rows_changed\":%s,\"first_position_unchanged\":%s,"
            "\"clip\":[%.9g,%.9g,%.9g,%.9g],\"same_point_delta_pixels\":",
            shown++?",":"",sample->draw,valid?"true":"false",paired?"true":"false",
            old && memcmp(old->constants+256*4,sample->constants+256*4,16*sizeof(float))?"true":"false",
            old && old->has_vertex && !memcmp(old->attributes,sample->attributes,4*sizeof(float))?"true":"false",
            valid?(double)now[0]:0,valid?(double)now[1]:0,valid?(double)now[2]:0,valid?(double)now[3]:0);
        if(paired)fprintf(file,"[%.9g,%.9g]",(double)delta[0],(double)delta[1]);else fprintf(file,"null");
        fprintf(file,"}");
    }
    fprintf(file,"]}\n");fclose(file);
}

/* Compute validation of the frozen pair; never feeds gameplay or DLSS. */
static void d2_motion_dump(u32 index,const D2TemporalFrame* current,const D2TemporalFrame* previous){
    D2MotionProbeInput inputs[D2_TEMP_SAMPLES]={0};D2MotionProbeOutput outputs[D2_TEMP_SAMPLES]={0};
    float cpu_delta[D2_TEMP_SAMPLES][2]={{0}};u32 draws[D2_TEMP_SAMPLES]={0};unsigned count=0;
    if(current->valid && previous->valid)for(u32 i=0;i<current->count;i++){
        const D2TemporalSample* sample=&current->samples[i];
        if(sample->shader!=0x99362655u || !sample->has_vertex)continue;
        const D2TemporalSample* old=NULL;unsigned matches=0;
        for(u32 j=0;j<previous->count;j++){
            const D2TemporalSample* candidate=&previous->samples[j];
            if(candidate->shader==sample->shader && candidate->fp==sample->fp &&
               candidate->vertices==sample->vertices && candidate->texture==sample->texture &&
               candidate->has_vertex && !memcmp(candidate->attributes,sample->attributes,4*sizeof(float))){
                old=candidate;matches++;
            }
        }
        /* Ambiguous first-point signatures are rejected, not used as identities. */
        if(matches!=1)continue;
        float now[4],before[4],delta[2];
        if(!d2_project_world(sample->constants+256*4,sample->constants+512*4,sample->constants+513*4,
               sample->constants[466*4+2],sample->attributes,now) ||
           !d2_project_world(old->constants+256*4,old->constants+512*4,old->constants+513*4,
               old->constants[466*4+2],sample->attributes,before) ||
           !d2_same_point_delta(before,now,s_d3d.width,s_d3d.height,delta))continue;
        D2MotionProbeInput* input=&inputs[count];
        memcpy(input->previous_rows,old->constants+256*4,16*sizeof(float));
        memcpy(input->current_rows,sample->constants+256*4,16*sizeof(float));
        memcpy(input->previous_scale,old->constants+512*4,4*sizeof(float));
        memcpy(input->current_scale,sample->constants+512*4,4*sizeof(float));
        memcpy(input->previous_offset,old->constants+513*4,4*sizeof(float));
        memcpy(input->current_offset,sample->constants+513*4,4*sizeof(float));
        input->previous_bias[2]=old->constants[466*4+2];input->current_bias[2]=sample->constants[466*4+2];
        memcpy(input->point,sample->attributes,4*sizeof(float));
        input->dimensions[0]=(float)s_d3d.width;input->dimensions[1]=(float)s_d3d.height;
        memcpy(cpu_delta[count],delta,sizeof(delta));draws[count]=sample->draw;count++;
    }
    int result=count?d2_motion_probe_gpu(s_d3d.device,s_d3d.cmd_queue,inputs,count,outputs):0;
    double maximum_error=0;int agreed=count>0 && result==0;
    for(unsigned i=0;i<count && result==0;i++){
        if(outputs[i].delta_valid[2]!=1){agreed=0;continue;}
        for(unsigned axis=0;axis<2;axis++){
            double error=fabs((double)outputs[i].delta_valid[axis]-cpu_delta[i][axis]);
            if(!isfinite(error)){agreed=0;continue;}
            if(error>maximum_error)maximum_error=error;
            if(error>.25)agreed=0;
        }
    }
    char path[600];snprintf(path,sizeof(path),"%s/motion-gpu-check.json",d2_temporal_directory);
    FILE* file=d2_capture_fopen(path,"wb");if(!file)return;
    fprintf(file,"{\"schema\":1,\"purpose\":\"gpu-same-point-validation\",\"capture_index\":%u,"
        "\"current_frame\":%llu,\"previous_frame\":%llu,\"sample_count\":%u,"
        "\"motion_vectors_available\":false,\"raster_coverage_available\":false,\"object_identity_available\":false,"
        "\"units\":\"render-pixels\",\"direction\":\"previous-minus-current-uv\",\"tolerance_pixels\":0.25,"
        "\"status\":\"%s\",\"gpu_result\":%d,\"maximum_delta_error_pixels\":%.9g,\"samples\":[",
        index,(unsigned long long)current->frame,(unsigned long long)previous->frame,count,
        !count?"no-valid-pairs":result?"gpu-error":agreed?"cpu-gpu-match":"cpu-gpu-mismatch",result,maximum_error);
    for(unsigned i=0;i<count;i++){
        int valid=result==0 && outputs[i].delta_valid[2]==1 &&
            isfinite(outputs[i].delta_valid[0]) && isfinite(outputs[i].delta_valid[1]);
        fprintf(file,"%s{\"draw\":%u,\"gpu_valid\":%s,\"cpu_delta_pixels\":[%.9g,%.9g],\"gpu_delta_pixels\":",
            i?",":"",draws[i],valid?"true":"false",(double)cpu_delta[i][0],(double)cpu_delta[i][1]);
        if(valid)fprintf(file,"[%.9g,%.9g]",(double)outputs[i].delta_valid[0],(double)outputs[i].delta_valid[1]);
        else fprintf(file,"null");
        fprintf(file,"}");
    }
    fprintf(file,"]}\n");fclose(file);
    fprintf(stderr,"[D2MOTION] GPU samples=%u status=%s error=%.6g pixels; raster field unavailable\n",
        count,!count?"no-valid-pairs":result?"gpu-error":agreed?"cpu-gpu-match":"cpu-gpu-mismatch",maximum_error);
}


static void d2_raster_dump(u32 index,const D2TemporalFrame* current,const D2TemporalFrame* previous){
    D2MotionVertex* vertices=(D2MotionVertex*)calloc(D2_TEMP_SAMPLES*D2_GEOMETRY_LIMIT,sizeof(D2MotionVertex));
    D2MotionDraw draws[D2_TEMP_SAMPLES]={0};unsigned count=0,total_vertices=0,rejected=0,unreplayed_draws=0;
    const char* reasons[D2_TEMP_SAMPLES]={"not-world","not-world","not-world","not-world"};
    unsigned geometry_matches[D2_TEMP_SAMPLES]={0},frustum_triangles[D2_TEMP_SAMPLES]={0};
    float ndc_bounds[D2_TEMP_SAMPLES][6]={{0}};
    if(vertices && current->valid && previous->valid)for(u32 i=0;i<current->count;i++){
        const D2TemporalSample* sample=&current->samples[i];
        if(sample->shader!=0x99362655u)continue;
        if(!sample->geometry_count){reasons[i]="geometry-ineligible";rejected++;continue;}
        const D2TemporalSample* old=NULL;unsigned matches=0;
        for(u32 j=0;j<previous->count;j++){
            const D2TemporalSample* p=&previous->samples[j];
            if(p->shader==sample->shader && p->fp==sample->fp && p->texture==sample->texture &&
               p->geometry_count==sample->geometry_count && p->cull==sample->cull &&
               !memcmp(p->positions,sample->positions,sample->geometry_count*4*sizeof(float))){old=p;matches++;}
        }
        geometry_matches[i]=matches;
        if(matches!=1){reasons[i]="no-unique-unchanged-geometry";rejected++;continue;}
        D2MotionVertex* dst=vertices+count*D2_GEOMETRY_LIMIT;int valid=1;
        for(unsigned v=0;v<sample->geometry_count;v++){
            if(!d2_project_world(sample->constants+256*4,sample->constants+512*4,sample->constants+513*4,
                   sample->constants[466*4+2],sample->positions[v],dst[v].current_clip) ||
               !d2_project_world(old->constants+256*4,old->constants+512*4,old->constants+513*4,
                   old->constants[466*4+2],sample->positions[v],dst[v].previous_clip)){valid=0;break;}
        }
        if(!valid){reasons[i]="projection-invalid";rejected++;continue;}
        for(unsigned axis=0;axis<3;axis++)ndc_bounds[i][axis]=ndc_bounds[i][axis+3]=dst[0].current_clip[axis]/dst[0].current_clip[3];
        for(unsigned v=0;v<sample->geometry_count;v++){
            for(unsigned axis=0;axis<3;axis++){
                float value=dst[v].current_clip[axis]/dst[v].current_clip[3];
                if(value<ndc_bounds[i][axis])ndc_bounds[i][axis]=value;
                if(value>ndc_bounds[i][axis+3])ndc_bounds[i][axis+3]=value;
            }
            if(v%3u==0 && d2_triangle_frustum_candidate(dst[v].current_clip,dst[v+1].current_clip,dst[v+2].current_clip))frustum_triangles[i]++;
        }
        reasons[i]=(sample->geometry_reject & (64u|128u))?"accepted-geometry-only-alpha-blend-unreplayed":"accepted";
        if(sample->geometry_reject & (64u|128u))unreplayed_draws++;
        draws[count].vertices=dst;draws[count].count=sample->geometry_count;draws[count].cull=sample->cull;
        total_vertices+=sample->geometry_count;count++;
    }
    size_t pixels_count=(size_t)s_d3d.width*s_d3d.height;
    D2MotionPixel* pixels=pixels_count<=8388608?(D2MotionPixel*)calloc(pixels_count,sizeof(D2MotionPixel)):NULL;
    int result=!vertices || !pixels?(int)E_OUTOFMEMORY:count?
        d2_motion_raster_gpu(s_d3d.device,s_d3d.cmd_queue,draws,count,s_d3d.width,s_d3d.height,pixels):0;
    unsigned long long covered=0,moving=0,invalid=0;double max_x=0,max_y=0;int images=0;
    if(result==0 && count)for(size_t p=0;p<pixels_count;p++){
        if(pixels[p].coverage<.5f)continue;
        if(!isfinite(pixels[p].delta_x) || !isfinite(pixels[p].delta_y) ||
           !isfinite(pixels[p].depth) || pixels[p].depth<0 || pixels[p].depth>1){invalid++;continue;}
        covered++;
        double x=fabs((double)pixels[p].delta_x),y=fabs((double)pixels[p].delta_y);
        if(x>.01 || y>.01)moving++;if(x>max_x)max_x=x;if(y>max_y)max_y=y;
    }
    unsigned long long no_cull_covered=0;int no_cull_result=0,no_cull_ran=0;
    if(result==0 && count && !covered){
        int has_cull=0;D2MotionDraw uncull[D2_TEMP_SAMPLES];
        for(unsigned i=0;i<count;i++){uncull[i]=draws[i];has_cull|=(draws[i].cull & 1u)!=0;uncull[i].cull=0;}
        if(has_cull){
            D2MotionPixel* comparison=(D2MotionPixel*)calloc(pixels_count,sizeof(D2MotionPixel));
            if(comparison){
                no_cull_ran=1;
                no_cull_result=d2_motion_raster_gpu(s_d3d.device,s_d3d.cmd_queue,uncull,count,s_d3d.width,s_d3d.height,comparison);
                if(no_cull_result==0)for(size_t p=0;p<pixels_count;p++)no_cull_covered+=comparison[p].coverage>.5f;
                free(comparison);
            }
        }
    }
    char path[600];
    if(result==0 && count){
        u8* image=(u8*)calloc(pixels_count,4);
        if(image){
            for(size_t p=0;p<pixels_count;p++){
                u8 value=pixels[p].coverage>.5f?255:0;image[p*4]=image[p*4+1]=image[p*4+2]=value;image[p*4+3]=255;
            }
            snprintf(path,sizeof(path),"%s/motion-coverage.bmp",d2_temporal_directory);
            cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
            images=d2_hud_bmp_complete(path);
            memset(image,0,pixels_count*4);
            for(size_t p=0;p<pixels_count;p++)if(pixels[p].coverage>.5f &&
                isfinite(pixels[p].delta_x) && isfinite(pixels[p].delta_y)){
                double x=.5+(double)pixels[p].delta_x/512.0,y=.5+(double)pixels[p].delta_y/512.0;
                image[p*4]=(u8)(255*(x<0?0:x>1?1:x));image[p*4+1]=(u8)(255*(y<0?0:y>1?1:y));
                image[p*4+2]=128;image[p*4+3]=255;
            }
            snprintf(path,sizeof(path),"%s/motion-vectors.bmp",d2_temporal_directory);
            cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
            images=images && d2_hud_bmp_complete(path);free(image);
        }
    }
    snprintf(path,sizeof(path),"%s/motion-raster-check.json",d2_temporal_directory);
    FILE* file=d2_capture_fopen(path,"wb");
    if(file){
        fprintf(file,"{\"schema\":1,\"purpose\":\"candidate-scenery-motion-raster\",\"capture_index\":%u,"
            "\"current_frame\":%llu,\"previous_frame\":%llu,\"capture_image_frame\":%llu,"
            "\"reference_image\":\"selected-reference.bmp\",\"reference_image_frame\":%s,"
            "\"reference_image_available\":%s,\"reference_matches_current_frame\":%s,"
            "\"reference_space\":\"internal-full-frame\","
            "\"render_width\":%u,\"render_height\":%u,\"draw_count\":%u,\"vertices\":%u,\"rejected_samples\":%u,"
            "\"status\":\"%s\",\"gpu_result\":%d,\"covered_pixels\":%llu,\"moving_pixels\":%llu,\"invalid_pixels\":%llu,"
            "\"maximum_absolute_delta_pixels\":[%.9g,%.9g],\"images_written\":%s,"
            "\"candidate_raster_coverage_available\":%s,\"full_scene_visibility_validated\":false,"
            "\"motion_vectors_available\":false,\"object_identity_available\":false,"
            "\"coverage_scope\":\"candidate-geometry-only\",\"shader_alpha_test_replayed\":false,"
            "\"blend_replayed\":false,\"draws_with_unreplayed_alpha_or_blend\":%u,"
            "\"no_cull_comparison_ran\":%s,\"no_cull_gpu_result\":%d,\"no_cull_covered_pixels\":%llu,"
            "\"depth_test\":\"less-equal-among-candidates\",\"alpha_stencil_scene_occlusion_validated\":false,"
            "\"units\":\"render-pixels\",\"direction\":\"previous-minus-current-uv\",\"normal_rendering_changed\":false,\"sample_rejections\":[",
            index,(unsigned long long)current->frame,(unsigned long long)previous->frame,
            (unsigned long long)s_d3d.frame_count,d2_reference_frame_text,d2_reference_available?"true":"false",
            d2_reference_available && d2_reference_frame==current->frame?"true":"false",
            s_d3d.width,s_d3d.height,count,total_vertices,rejected,
            result?"gpu-error":!count?"no-static-geometry-pairs":invalid?"invalid-pixels":!covered?"no-visible-candidate-pixels":"candidate-raster-ready",
            result,covered,moving,invalid,max_x,max_y,images?"true":"false",
            result==0 && count && covered && !invalid?"true":"false",unreplayed_draws,no_cull_ran?"true":"false",no_cull_result,no_cull_covered);

        unsigned shown=0;
        for(unsigned i=0;i<current->count;i++){
            const D2TemporalSample* sample=&current->samples[i];
            if(sample->shader!=0x99362655u)continue;
            fprintf(file,"%s{\"draw\":%u,\"reason\":\"%s\",\"eligibility_mask\":%u,"
                "\"geometry_vertices\":%u,\"unchanged_geometry_matches\":%u,\"topology\":%u,"
                "\"blend\":%u,\"alpha_ctl\":%u,\"stencil\":%u,\"viewport\":[%u,%u,%u,%u],"
                "\"cull\":%u,\"frustum_candidate_triangles\":%u,\"current_ndc_bounds\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]}",
                shown++?",":"",sample->draw,reasons[i],sample->geometry_reject,sample->geometry_count,
                geometry_matches[i],sample->topology,sample->blend,sample->alpha_ctl,sample->stencil,
                sample->viewport[0],sample->viewport[1],sample->viewport[2],sample->viewport[3],
                sample->cull,frustum_triangles[i],(double)ndc_bounds[i][0],(double)ndc_bounds[i][1],(double)ndc_bounds[i][2],
                (double)ndc_bounds[i][3],(double)ndc_bounds[i][4],(double)ndc_bounds[i][5]);
        }
        fprintf(file,"]}\n");fclose(file);
    }
    fprintf(stderr,"[D2MOTION] candidate raster draws=%u covered=%llu moving=%llu; full scene visibility pending\n",count,covered,moving);
    free(pixels);free(vertices);
}

static void d2_temporal_dump(u32 index){
    const D2TemporalFrame* current=&d2_temporal_frames[d2_temporal_next^1];
    const D2TemporalFrame* previous=&d2_temporal_frames[d2_temporal_next];
    const char* base=d2_capture_directory();char path[512];
    snprintf(d2_temporal_directory,sizeof(d2_temporal_directory),"%s/frame_%02u",base?base:"rsxcap",index);
    snprintf(path,sizeof(path),"%s/temporal-probe.json",d2_temporal_directory);
    FILE* file=d2_capture_fopen(path,"wb");d2_auto_probe.phase=D2_AUTO_IDLE;if(!file)return;
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    double milliseconds=previous->valid && current->valid && frequency.QuadPart>0
        ?(double)(current->time.QuadPart-previous->time.QuadPart)*1000.0/(double)frequency.QuadPart:0;
    fprintf(file,"{\"schema\":1,\"purpose\":\"camera-constant-discovery\",\"motion_vectors_available\":false,"
        "\"camera_matrices_identified\":false,\"object_identity_available\":false,\"observation_delta_ms\":%.6f,\"current\":",milliseconds);
    d2_temporal_json_frame(file,current);fprintf(file,",\"previous\":");d2_temporal_json_frame(file,previous);
    fprintf(file,"}\n");fclose(file);
    d2_projection_dump(index,current,previous);
    d2_motion_dump(index,current,previous);
    d2_raster_dump(index,current,previous);
    fprintf(stderr,"[D2TEMP] capture frame=%llu previous=%llu samples=%u/%u; camera and motion not yet identified\n",
        (unsigned long long)current->frame,(unsigned long long)previous->frame,current->count,previous->count);
}
