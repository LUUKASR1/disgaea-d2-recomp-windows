/* Capture-only inventory/ranking, prepared once before render submission. */
typedef struct {
    u64 frame;int valid,complete;u32 seen,inventory_count,vertices_scanned,skipped,ranked_count,ambiguous_records;
    D2SceneryKey keys[MAX_DRAWS];u64 hashes[MAX_DRAWS];unsigned rejects[MAX_DRAWS];double areas[MAX_DRAWS],utilities[MAX_DRAWS],history_rates[MAX_DRAWS];u32 history_counts[MAX_DRAWS],groups;
    D2SceneryRank ranked[D2_WORLD_SAMPLES];
} D2ScenerySurvey;
static D2ScenerySurvey d2_scenery_survey;
static void d2_scenery_prepare(void){
    if(!d2_temporal_enabled() || d2_auto_probe.phase!=D2_AUTO_WAITING || !s_present_this_frame)return;
    d2_scenery_feedback_load();
    D2ScenerySurvey* survey=&d2_scenery_survey;memset(survey,0,sizeof(*survey));
    survey->frame=s_d3d.frame_count;survey->complete=s_d3d.draw_count<=MAX_DRAWS;
    if(!s_d3d.vp_vb_mapped || !s_d3d.vp_cb_mapped)return;
    const char* old_reason=d2_hud_boundary_reason;u32 boundary=d2_hud_find_boundary(s_d3d.draw_count);d2_hud_boundary_reason=old_reason;
    u32 parity=(u32)s_d3d.vp_parity;
    for(u32 d=0;d<s_d3d.draw_count && d<MAX_DRAWS && d<boundary;d++){
        const D3D12DrawRecord* r=&s_d3d.draws[d];
        if(!r->is_vp||r->is_clear||r->rt_off||r->rt_mrt[0]||r->rt_mrt[1]||r->rt_mrt[2]||
           r->d2_capture_vs_hash!=0x99362655u || r->cb_slot>=MAX_DRAWS)continue;
        survey->seen++;unsigned reject=d2_geometry_rejection(r);survey->rejects[d]=reject;
        if(reject & ~(64u|128u))continue;
        if(r->vertex_count>MAX_VERTICES-survey->vertices_scanned){survey->complete=0;survey->skipped++;continue;}
        survey->vertices_scanned+=r->vertex_count;
        const char* vertices=(const char*)s_d3d.vp_vb_mapped+(u64)parity*MAX_VERTICES*256u+r->vb_byte_offset;
        u64 hash=d2_scenery_positions_hash(vertices,r->vertex_count,256u);survey->hashes[d]=hash;
        D2SceneryKey* key=&survey->keys[survey->inventory_count++];
        key->shader=r->d2_capture_vs_hash;key->fragment=r->fp_addr;key->texture=r->tex[0].raw;
        key->vertices=r->vertex_count;key->cull=r->cull;key->positions_hash=hash;
        const float* bank=(const float*)((const char*)s_d3d.vp_cb_mapped+((u64)parity*MAX_DRAWS+r->cb_slot)*VP_CB_STRIDE);
        double area=0;float clip[3][4];int valid=1;
        for(unsigned v=0;v<r->vertex_count && valid;v+=3){
            for(unsigned k=0;k<3;k++)if(!d2_project_world(bank+256*4,bank+512*4,bank+513*4,bank[466*4+2],
                (const float*)(vertices+(size_t)(v+k)*256u),clip[k])){valid=0;break;}
            if(valid)area+=d2_scenery_triangle_area(clip[0],clip[1],clip[2]);
        }
        if(!valid){survey->rejects[d]|=2048u;continue;}
        double pixels=area*(double)s_d3d.width*s_d3d.height*.25;
        double cap=(double)s_d3d.width*s_d3d.height;survey->areas[d]=pixels>cap?cap:pixels;
        if(survey->areas[d]<=0)survey->rejects[d]|=1024u;
    }
    for(unsigned i=0;i<survey->inventory_count;i++)
        survey->ambiguous_records+=d2_scenery_key_matches(survey->keys,survey->inventory_count,&survey->keys[i])>1;
    D2SceneryChoice choices[MAX_DRAWS];u32 choices_count=0,selected[D2_WORLD_SAMPLES];
    if(survey->complete)for(u32 d=0;d<s_d3d.draw_count && d<MAX_DRAWS && d<boundary;d++)if(survey->areas[d]>0){
        const D3D12DrawRecord* r=&s_d3d.draws[d];
        D2SceneryKey key={r->d2_capture_vs_hash,r->fp_addr,r->tex[0].raw,r->vertex_count,r->cull,survey->hashes[d]};
        if(d2_scenery_key_matches(survey->keys,survey->inventory_count,&key)!=1)continue;
        D2SceneryGroup group=d2_scenery_group(key.shader,key.fragment,key.texture,key.vertices);
        int history=d2_scenery_feedback_find(d2_scenery_feedback,d2_scenery_feedback_count,&group);
        const D2SceneryFeedback* record=history>=0?&d2_scenery_feedback[history]:NULL;
        survey->history_rates[d]=record?record->useful_rate:.5;survey->history_counts[d]=record?record->observations:0;
        survey->utilities[d]=d2_scenery_utility(survey->areas[d],record);
        choices[choices_count].group=group;choices[choices_count].draw=d;choices[choices_count++].score=survey->utilities[d];
    }
    survey->ranked_count=d2_scenery_cluster_select(choices,choices_count,selected,D2_WORLD_SAMPLES,&survey->groups);
    for(unsigned i=0;i<survey->ranked_count;i++){
        survey->ranked[i].draw=choices[selected[i]].draw;survey->ranked[i].area=choices[selected[i]].score;
    }
    survey->valid=1;
}
