/* Diagnostic snapshots only: never treat draw order as persistent object identity. */
#include "temporal_probe.h"
#include "projection.h"
#define D2_TEMP_SAMPLES 4
typedef struct {
    u32 draw,shader,fp,vertices,texture,cb_slot;
    int has_vertex;float attributes[64];
    float constants[D2_TEMP_ROWS*4];
} D2TemporalSample;
typedef struct {
    u64 frame;LARGE_INTEGER time;u32 count;int valid;
    D2TemporalSample samples[D2_TEMP_SAMPLES];
} D2TemporalFrame;
static D2TemporalFrame d2_temporal_frames[2];
static unsigned d2_temporal_next;
static unsigned d2_temporal_phase,d2_temporal_index;
static char d2_temporal_directory[512];
static int d2_temporal_enabled(void){
    static int enabled=-1;
    if(enabled<0){const char* e=getenv("D2_CAPTURE_MASK_STATE");enabled=e && strcmp(e,"1")==0;}
    return enabled;
}
static int d2_temporal_request(u32* index,HWND window){
    if(!d2_temporal_enabled())return d2_capture_next(index,window);
    if(d2_temporal_phase){*index=d2_temporal_index;return d2_temporal_phase==2;}
    if(!d2_capture_next(index,window))return 0;
    d2_temporal_index=*index;d2_temporal_phase=1;d2_temporal_next=0;
    memset(d2_temporal_frames,0,sizeof(d2_temporal_frames));
    return 0; /* First frame samples only; the next presented frame owns the capture. */
}
static void d2_temporal_observe(u32 count,u32 hud_start){
    if(!d2_temporal_enabled() || !d2_temporal_phase || !count)return;
    D2TemporalFrame* frame=&d2_temporal_frames[d2_temporal_next];
    frame->valid=0;frame->count=0;frame->frame=s_d3d.frame_count;QueryPerformanceCounter(&frame->time);
    if(s_d3d.vp_cb_mapped){
        u32 parity=(u32)(s_d3d.vp_parity^1);
        for(u32 d=0;d<count && d<MAX_DRAWS && d<hud_start && frame->count<D2_TEMP_SAMPLES;d++){
            const D3D12DrawRecord* r=&s_d3d.draws[d];
            if(!r->is_vp || r->is_clear || r->rt_off || r->rt_mrt[0] || r->rt_mrt[1] || r->rt_mrt[2] ||
               !r->d2_capture_vs_hash || r->cb_slot>=MAX_DRAWS)continue;
            if(d2_hud_record_candidate(r,parity))continue;
            unsigned same=0;
            for(u32 i=0;i<frame->count;i++)if(frame->samples[i].shader==r->d2_capture_vs_hash)same++;
            if(same>=2)continue;
            D2TemporalSample* sample=&frame->samples[frame->count++];
            sample->draw=d;sample->shader=r->d2_capture_vs_hash;sample->fp=r->fp_addr;
            sample->vertices=r->vertex_count;sample->texture=r->tex[0].raw;sample->cb_slot=r->cb_slot;
            const void* bank=(const char*)s_d3d.vp_cb_mapped+((u64)parity*MAX_DRAWS+r->cb_slot)*VP_CB_STRIDE;
            memcpy(sample->constants,bank,sizeof(sample->constants));
            sample->has_vertex=s_d3d.vp_vb_mapped && r->vertex_count>0 && r->vb_byte_offset%256u==0 &&
                r->vb_byte_offset/256u<MAX_VERTICES;
            if(sample->has_vertex){
                const void* vertex=(const char*)s_d3d.vp_vb_mapped+(u64)parity*MAX_VERTICES*256u+r->vb_byte_offset;
                memcpy(sample->attributes,vertex,sizeof(sample->attributes));
            }
        }
        frame->valid=1;
    }
    d2_temporal_next^=1;
    d2_temporal_phase++;
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
            "\"constant_slot\":%u,\"shader_reads_available\":%s,\"indirect_reads\":%s,\"referenced_rows\":%u,\"constants\":[",
            i?",":"",sample->draw,sample->shader,sample->fp,sample->vertices,sample->texture,sample->cb_slot,
            available?"true":"false",reads.indirect?"true":"false",reads.count);
        unsigned shown=0;
        for(unsigned row=0;row<D2_TEMP_ROWS;row++)if(reads.used[row]){
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
        "\"render_width\":%u,\"render_height\":%u,\"samples\":[",index,
        (unsigned long long)current->frame,(unsigned long long)previous->frame,s_d3d.width,s_d3d.height);
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
static void d2_temporal_dump(u32 index){
    const D2TemporalFrame* current=&d2_temporal_frames[d2_temporal_next^1];
    const D2TemporalFrame* previous=&d2_temporal_frames[d2_temporal_next];
    const char* base=d2_capture_directory();char path[512];
    snprintf(d2_temporal_directory,sizeof(d2_temporal_directory),"%s/frame_%02u",base?base:"rsxcap",index);
    snprintf(path,sizeof(path),"%s/temporal-probe.json",d2_temporal_directory);
    FILE* file=d2_capture_fopen(path,"wb");d2_temporal_phase=0;if(!file)return;
    LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    double milliseconds=previous->valid && current->valid && frequency.QuadPart>0
        ?(double)(current->time.QuadPart-previous->time.QuadPart)*1000.0/(double)frequency.QuadPart:0;
    fprintf(file,"{\"schema\":1,\"purpose\":\"camera-constant-discovery\",\"motion_vectors_available\":false,"
        "\"camera_matrices_identified\":false,\"object_identity_available\":false,\"observation_delta_ms\":%.6f,\"current\":",milliseconds);
    d2_temporal_json_frame(file,current);fprintf(file,",\"previous\":");d2_temporal_json_frame(file,previous);
    fprintf(file,"}\n");fclose(file);
    d2_projection_dump(index,current,previous);
    fprintf(stderr,"[D2TEMP] capture frame=%llu previous=%llu samples=%u/%u; camera and motion not yet identified\n",
        (unsigned long long)current->frame,(unsigned long long)previous->frame,current->count,previous->count);
}
