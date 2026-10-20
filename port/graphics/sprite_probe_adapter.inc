#define D2_SPRITE_VERTICES 96u
#define D2_SPRITE_SAVED_VERTICES 6u
typedef struct {u32 draw;u64 geometry_hash,uv_hash;float center[3],positions[6][4],uv[6][2],transform[16],clip[6][4],scale[4],offset[4];int projected;} D2SpriteRecord;
typedef struct {u64 frame;u32 count,legacy,large,invalid;int valid,reference_unchanged;u32 reference_pairs;D2SpriteKey keys[MAX_DRAWS];D2SpriteRecord records[MAX_DRAWS];} D2SpriteFrame;
static D2SpriteFrame d2_sprite_frames[2];
typedef struct {unsigned char* mask;float* depth;u64 frame;u32 draw;int available,result;const char* reason;} D2SpriteAlpha;
static D2SpriteAlpha d2_sprite_alpha[MAX_DRAWS];
static void d2_sprite_clear_snapshots(void){
    for(unsigned i=0;i<MAX_DRAWS;i++){free(d2_sprite_alpha[i].mask);free(d2_sprite_alpha[i].depth);}
    memset(d2_sprite_alpha,0,sizeof(d2_sprite_alpha));
}
static int d2_sprite_goal(void){const char* goal=getenv("D2_CAPTURE_GOAL");return goal && !strcmp(goal,"sprites");}
static int d2_sprite_observe(u32 count,u32 hud,u32 parity,unsigned index){
    if(!d2_sprite_goal())return 0;
    D2SpriteFrame* frame=&d2_sprite_frames[index];memset(frame,0,sizeof(*frame));frame->frame=s_d3d.frame_count;
    if(!d2_sprite_goal() || !s_d3d.vp_vb_mapped || !s_d3d.vp_cb_mapped)return 0;
    frame->valid=count<=MAX_DRAWS;
    for(u32 d=0;d<count && d<MAX_DRAWS && d<hud;d++){
        const D3D12DrawRecord* r=&s_d3d.draws[d];
        if(r->is_clear||r->rt_off||r->rt_mrt[0]||r->rt_mrt[1]||r->rt_mrt[2])continue;
        if(!r->is_vp){frame->legacy++;continue;}
        if(d2_geometry_rejection(r)&4u){frame->invalid++;continue;}
        if(r->vertex_count>D2_SPRITE_VERTICES){frame->large++;continue;}
        if(!r->vertex_count || r->vertex_count<6 || r->cb_slot>=MAX_DRAWS || r->vb_byte_offset%256u ||
            r->vb_byte_offset/256u>=MAX_VERTICES || r->vertex_count>MAX_VERTICES-r->vb_byte_offset/256u){frame->invalid++;continue;}
        D2SpriteRecord* dst=&frame->records[frame->count];D2SpriteKey* key=&frame->keys[frame->count];
        key->shader=r->d2_capture_vs_hash;key->fragment=r->fp_addr;key->texture=r->tex[0].raw;
        key->vertices=r->vertex_count;key->topology=r->topology;dst->draw=d;
        const char* vb=(const char*)s_d3d.vp_vb_mapped+(u64)parity*MAX_VERTICES*256u+r->vb_byte_offset;
        dst->geometry_hash=d2_scenery_positions_hash(vb,r->vertex_count,256u);
        u64 uv_hash=UINT64_C(14695981039346656037);int finite=1;
        for(unsigned v=0;v<r->vertex_count;v++){
            const float* position=(const float*)(vb+(size_t)v*256u);const float* uv=(const float*)(vb+(size_t)v*256u+8u*16u);
            for(unsigned axis=0;axis<3;axis++){finite=finite && isfinite(position[axis]);dst->center[axis]+=position[axis]/r->vertex_count;}
            if(v<6){memcpy(dst->positions[v],position,4*sizeof(float));memcpy(dst->uv[v],uv,2*sizeof(float));}
            for(unsigned b=0;b<8;b++){uv_hash^=((const unsigned char*)uv)[b];uv_hash*=UINT64_C(1099511628211);}
        }
        for(unsigned axis=0;axis<3;axis++)finite=finite && isfinite(dst->center[axis]);
        if(!finite){memset(dst,0,sizeof(*dst));frame->invalid++;continue;}
        dst->uv_hash=uv_hash;
        const float* bank=(const float*)((const char*)s_d3d.vp_cb_mapped+((u64)parity*MAX_DRAWS+r->cb_slot)*VP_CB_STRIDE);
        memcpy(dst->transform,bank+256*4,16*sizeof(float));
        memcpy(dst->scale,bank+512*4,4*sizeof(float));memcpy(dst->offset,bank+513*4,4*sizeof(float));
        dst->projected=key->shader==0x99362655u;
        for(unsigned v=0;v<6 && dst->projected;v++)dst->projected=d2_project_world(bank+256*4,bank+512*4,bank+513*4,bank[466*4+2],dst->positions[v],dst->clip[v]);
        if(key->shader==0x2003CB89u){
            dst->projected=1;for(unsigned v=0;v<6 && dst->projected;v++)dst->projected=d2_project_sprite(dst->scale,dst->offset,dst->positions[v],dst->clip[v]);
        }
        frame->count++;
    }
    const D2SpriteFrame* previous=&d2_sprite_frames[index^1];
    if(!frame->valid || !previous->valid || previous->frame+1!=frame->frame)return 0;
    for(unsigned i=0;i<frame->count;i++){
        int pair=d2_sprite_unique_pair(frame->keys,frame->count,previous->keys,previous->count,i);
        if(pair>=0 && frame->keys[i].shader==0x2003CB89u && frame->records[i].projected && previous->records[pair].projected &&
           frame->records[i].geometry_hash!=previous->records[pair].geometry_hash &&
           d2_sprite_center_changed(frame->records[i].center,previous->records[pair].center))return 1;
    }
    return 0;
}
static void d2_sprite_json_values(FILE* file,const float* values,unsigned count){
    fprintf(file,"[");for(unsigned i=0;i<count;i++){if(i)fprintf(file,",");if(isfinite(values[i]))fprintf(file,"%.9g",(double)values[i]);else fprintf(file,"null");}fprintf(file,"]");
}
#include "sprite_projection_adapter.inc"
static void d2_sprite_dump(u32 capture,const D2TemporalFrame* temporal){
    if(!d2_sprite_goal())return;
    const D2SpriteFrame* current=&d2_sprite_frames[d2_temporal_next^1];const D2SpriteFrame* previous=&d2_sprite_frames[d2_temporal_next];
    int pair_frame=current->valid && previous->valid && current->frame==temporal->frame && previous->frame+1==current->frame;
    char path[600];snprintf(path,sizeof(path),"%s/sprite-discovery-check.json",d2_temporal_directory);FILE* file=d2_capture_fopen(path,"wb");if(!file)return;
    fprintf(file,"{\"schema\":1,\"purpose\":\"small-draw-sprite-discovery\",\"capture_index\":%u,\"current_frame\":%llu,"
        "\"previous_frame\":%llu,\"consecutive_frame_inventory_available\":%s,\"reference_matches_frame\":%s,"
        "\"current_records\":%u,\"previous_records\":%u,\"vertex_limit\":%u,\"skipped_large_draws\":%u,"
        "\"skipped_legacy_draws\":%u,\"invalid_geometry_records\":%u,\"position_space\":\"vertex-attribute-0\","
        "\"selection\":\"%s\",\"object_identity_available\":false,\"sprite_classification_validated\":false,"
        "\"motion_vectors_available\":false,\"applied_to_game\":false,",
        capture,(unsigned long long)current->frame,(unsigned long long)previous->frame,pair_frame?"true":"false",
        d2_reference_available && d2_reference_frame==current->frame?"true":"false",current->count,previous->count,D2_SPRITE_VERTICES,
        current->large,current->legacy,current->invalid,d2_auto_probe.changed?"projected-candidate-geometry-changed":"timeout-no-eligible-movement");
    fprintf(file,"\"static_reference_pairs\":%u,\"static_reference_transform_rows_unchanged\":%s,\"samples\":[",
        current->reference_pairs,current->reference_unchanged?"true":"false");
    for(unsigned i=0;i<current->count;i++){
        int paired=pair_frame?d2_sprite_unique_pair(current->keys,current->count,previous->keys,previous->count,i):-1;
        const D2SpriteRecord* now=&current->records[i];const D2SpriteRecord* old=paired>=0?&previous->records[paired]:NULL;
        fprintf(file,"%s{\"draw\":%u,\"shader\":\"%08X\",\"fragment\":\"%08X\",\"texture\":\"%08X\",\"vertices\":%u,"
            "\"unique_pipeline_signature_pair\":%s,\"signature_is_not_identity\":true,\"previous_draw\":",i?",":"",now->draw,
            current->keys[i].shader,current->keys[i].fragment,current->keys[i].texture,current->keys[i].vertices,old?"true":"false");
        if(old)fprintf(file,"%u",old->draw);else fprintf(file,"null");
        fprintf(file,",\"geometry_changed\":%s,\"uv_changed\":%s,\"transform_rows_unchanged\":%s,\"center\":",
            old && now->geometry_hash!=old->geometry_hash?"true":"false",old && now->uv_hash!=old->uv_hash?"true":"false",
            old && !d2_transform_changed(old->transform,now->transform)?"true":"false");
        d2_sprite_json_values(file,now->center,3);fprintf(file,",\"previous_center\":");if(old)d2_sprite_json_values(file,old->center,3);else fprintf(file,"null");
        fprintf(file,",\"first_six_positions\":");d2_sprite_json_values(file,&now->positions[0][0],24);
        fprintf(file,",\"first_six_uv\":");d2_sprite_json_values(file,&now->uv[0][0],12);
        fprintf(file,",\"previous_first_six_positions\":");if(old)d2_sprite_json_values(file,&old->positions[0][0],24);else fprintf(file,"null");
        fprintf(file,",\"world_transform_rows_relevant\":%s,\"position_scale\":",current->keys[i].shader==0x99362655u?"true":"false");
        d2_sprite_json_values(file,now->scale,4);fprintf(file,",\"position_offset\":");d2_sprite_json_values(file,now->offset,4);
        fprintf(file,",\"previous_first_six_uv\":");if(old)d2_sprite_json_values(file,&old->uv[0][0],12);else fprintf(file,"null");
        fprintf(file,",\"current_clip_projection_available\":%s,\"first_six_clip_positions\":",now->projected?"true":"false");
        if(now->projected)d2_sprite_json_values(file,&now->clip[0][0],24);else fprintf(file,"null");
        fprintf(file,",\"previous_first_six_clip_positions\":");if(old && old->projected)d2_sprite_json_values(file,&old->clip[0][0],24);else fprintf(file,"null");
        fprintf(file,"}");
    }
    fprintf(file,"]}\n");fclose(file);
    d2_sprite_projection_dump(capture,temporal,current,previous);
}
