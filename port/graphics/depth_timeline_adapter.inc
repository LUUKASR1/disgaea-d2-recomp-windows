#include "depth_steps.h"
static void* d2_depth_steps;
static ID3D12Resource* d2_steps_source;
static u64 d2_steps_frame;
static u32 d2_steps_draw[2],d2_steps_hud_boundary;
static unsigned d2_steps_count;
static int d2_steps_active,d2_steps_recorded[3],d2_steps_result[3],d2_steps_available[3];
static float* d2_steps_pixels[3];
static void d2_depth_timeline_free_pixels(void){
    for(unsigned i=0;i<3;i++){free(d2_steps_pixels[i]);d2_steps_pixels[i]=NULL;d2_steps_available[i]=0;}
}
/* render_frame has just completed wait_for_gpu; no previous copy is in flight. */
static void d2_depth_timeline_prepare(void){
    if(d2_auto_probe.phase==D2_AUTO_READY || d2_auto_probe.phase==D2_AUTO_CAPTURING)return;
    d2_steps_active=0;
    if(!d2_temporal_enabled() || d2_auto_probe.phase!=D2_AUTO_WAITING || !s_present_this_frame)return;
    d2_depth_timeline_free_pixels();
    if(d2_steps_source!=s_d3d.depth_buffer){
        d2_depth_steps_destroy_after_gpu_idle(d2_depth_steps);d2_depth_steps=NULL;d2_steps_source=NULL;
    }
    if(!d2_depth_steps){
        d2_depth_steps=d2_depth_steps_create(s_d3d.device,s_d3d.depth_buffer);
        if(!d2_depth_steps)return;d2_steps_source=s_d3d.depth_buffer;
    }
    d2_depth_steps_reset_after_gpu_idle(d2_depth_steps);
    memset(d2_steps_recorded,0,sizeof(d2_steps_recorded));memset(d2_steps_result,0,sizeof(d2_steps_result));
    d2_steps_frame=s_d3d.frame_count;d2_steps_count=0;
    const char* old_reason=d2_hud_boundary_reason;
    d2_steps_hud_boundary=d2_hud_find_boundary(s_d3d.draw_count);d2_hud_boundary_reason=old_reason;
    u32 parity=(u32)s_d3d.vp_parity;
    /* Same preferred/fallback world geometry policy as temporal_observe.
       Reports require an exact selected-frame/draw match, never draw order alone. */
    for(unsigned pass=0;pass<2;pass++)for(u32 d=0;d<s_d3d.draw_count && d<MAX_DRAWS &&
        d<d2_steps_hud_boundary && d2_steps_count<2;d++){
        const D3D12DrawRecord* r=&s_d3d.draws[d];
        if(!r->is_vp||r->is_clear||r->rt_off||r->rt_mrt[0]||r->rt_mrt[1]||r->rt_mrt[2]||
           r->d2_capture_vs_hash!=0x99362655u||r->cb_slot>=MAX_DRAWS||!s_d3d.vp_cb_mapped)continue;
        unsigned reject=d2_geometry_rejection(r);
        if(!(reject & ~(64u|128u)))reject|=d2_geometry_view_rejection(r,parity);
        unsigned structural=reject & ~(64u|128u);
        if((pass==0 && structural)||(pass==1 && !structural))continue;
        d2_steps_draw[d2_steps_count++]=d;
    }
    d2_steps_active=1;
}
static void d2_depth_timeline_before_draw(u32 draw){
    if(d2_steps_active && d2_steps_frame==s_d3d.frame_count && draw==d2_steps_hud_boundary && draw<s_d3d.draw_count){
        d2_steps_result[2]=d2_depth_steps_record(d2_depth_steps,s_d3d.cmd_list,2);
        d2_steps_recorded[2]=d2_steps_result[2]==0;
    }
}
static void d2_depth_timeline_after_draw(u32 draw){
    if(!d2_steps_active || d2_steps_frame!=s_d3d.frame_count)return;
    for(unsigned i=0;i<d2_steps_count;i++)if(draw==d2_steps_draw[i]){
        d2_steps_result[i]=d2_depth_steps_record(d2_depth_steps,s_d3d.cmd_list,i);
        d2_steps_recorded[i]=d2_steps_result[i]==0;
    }
}
static void d2_depth_timeline_read_selected(void){
    if(!d2_steps_active || d2_steps_frame!=s_d3d.frame_count)return;
    size_t size=(size_t)s_d3d.width*s_d3d.height;
    for(unsigned i=0;i<3;i++)if(d2_steps_recorded[i]){
        d2_steps_pixels[i]=size<=8388608?(float*)calloc(size,sizeof(float)):NULL;
        d2_steps_result[i]=d2_steps_pixels[i]?d2_depth_steps_read(d2_depth_steps,s_d3d.cmd_queue,i,
            s_d3d.width,s_d3d.height,d2_steps_pixels[i]):(int)E_OUTOFMEMORY;
        d2_steps_available[i]=d2_steps_result[i]==0;
    }
}
static void d2_depth_timeline_dump(u32 index,const D2TemporalFrame* current){
    char path[600];snprintf(path,sizeof(path),"%s/depth-timeline.json",d2_temporal_directory);
    FILE* json=d2_capture_fopen(path,"wb");if(!json)return;
    int frame_match=d2_steps_active && d2_steps_frame==current->frame;
    int before_available=frame_match && d2_steps_available[2];
    int final_available=d2_scene_depth_available && d2_scene_depth_frame==current->frame && d2_scene_depth_data;
    const float epsilon=8.f/16777215.f;size_t size=(size_t)s_d3d.width*s_d3d.height;
    fprintf(json,"{\"schema\":1,\"purpose\":\"depth-writes-within-selected-frame\",\"capture_index\":%u,"
        "\"current_frame\":%llu,\"render_width\":%u,\"render_height\":%u,"
        "\"reference_matches_frame\":%s,\"timeline_matches_frame\":%s,"
        "\"before_hud_boundary_verified\":%s,\"hud_boundary_draw\":%u,\"before_hud_depth_available\":%s,"
        "\"before_hud_gpu_result\":%d,\"final_depth_available\":%s,\"depth_epsilon\":%.9g,"
        "\"normal_rendering_changed\":false,\"full_scene_visibility_validated\":false,\"motion_vectors_available\":false,\"samples\":[",
        index,(unsigned long long)current->frame,s_d3d.width,s_d3d.height,
        d2_reference_available && d2_reference_frame==current->frame?"true":"false",frame_match?"true":"false",
        frame_match && d2_steps_recorded[2]?"true":"false",d2_steps_hud_boundary,before_available?"true":"false",
        d2_steps_result[2],final_available?"true":"false",(double)epsilon);
    unsigned shown=0;
    for(unsigned i=0;i<current->count;i++){
        const D2TemporalSample* sample=&current->samples[i];if(sample->shader!=0x99362655u)continue;
        int slot=-1;if(frame_match)for(unsigned k=0;k<d2_steps_count;k++)if(d2_steps_draw[k]==sample->draw)slot=(int)k;
        int after_available=slot>=0 && d2_steps_available[slot];
        const D2AlphaSnapshot* alpha=&d2_alpha_snapshots[i];
        int original_available=alpha->available && alpha->frame==current->frame && alpha->draw==sample->draw && alpha->mask && alpha->depth;
        unsigned long long after[4]={0},before[4]={0},final[4]={0},pixels=0,after_before_changed=0,before_final_changed=0,after_final_changed=0;
        if(original_available)for(size_t p=0;p<size;p++)if(alpha->mask[p]){
            pixels++;
            if(after_available)after[d2_depth_relation(alpha->depth[p],d2_steps_pixels[slot][p],epsilon)]++;
            if(before_available)before[d2_depth_relation(alpha->depth[p],d2_steps_pixels[2][p],epsilon)]++;
            if(final_available)final[d2_depth_relation(alpha->depth[p],d2_scene_depth_data[p],epsilon)]++;
            if(after_available && before_available)after_before_changed+=d2_depth_relation(d2_steps_pixels[slot][p],d2_steps_pixels[2][p],epsilon)>1;
            if(before_available && final_available)before_final_changed+=d2_depth_relation(d2_steps_pixels[2][p],d2_scene_depth_data[p],epsilon)>1;
            if(after_available && final_available)after_final_changed+=d2_depth_relation(d2_steps_pixels[slot][p],d2_scene_depth_data[p],epsilon)>1;
        }
        fprintf(json,"%s{\"draw\":%u,\"original_shader_available\":%s,\"original_shader_pixels\":%llu,"
            "\"after_draw_depth_available\":%s,\"after_draw_gpu_result\":%d,"
            "\"original_vs_after_draw\":{\"invalid\":%llu,\"equal\":%llu,\"deeper\":%llu,\"nearer\":%llu},"
            "\"original_vs_before_hud\":{\"invalid\":%llu,\"equal\":%llu,\"deeper\":%llu,\"nearer\":%llu},"
            "\"original_vs_final\":{\"invalid\":%llu,\"equal\":%llu,\"deeper\":%llu,\"nearer\":%llu},"
            "\"after_draw_to_before_hud_comparison_available\":%s,\"after_draw_to_before_hud_changed_pixels\":%llu,"
            "\"before_hud_to_final_comparison_available\":%s,\"before_hud_to_final_changed_pixels\":%llu,"
            "\"after_draw_to_final_comparison_available\":%s,\"after_draw_to_final_changed_pixels\":%llu}",
            shown++?",":"",sample->draw,original_available?"true":"false",pixels,after_available?"true":"false",
            slot>=0?d2_steps_result[slot]:-1,after[0],after[1],after[2],after[3],before[0],before[1],before[2],before[3],
            final[0],final[1],final[2],final[3],original_available && after_available && before_available?"true":"false",after_before_changed,
            original_available && before_available && final_available?"true":"false",before_final_changed,
            original_available && after_available && final_available?"true":"false",after_final_changed);
    }
    fprintf(json,"]}\n");fclose(json);
}
