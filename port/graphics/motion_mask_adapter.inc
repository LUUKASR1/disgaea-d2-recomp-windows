#include "motion_gap_adapter.inc"
/* Original shader masks are selected by exact frozen draw/frame/size, and then
   by the winning raster draw owner at each pixel. Never union candidate masks. */
static void d2_alpha_depth_dump(u32 index,const D2TemporalFrame* current,const D2MotionPixel* pixels,
    const D2MotionPixel* depth_filtered,const unsigned* draw_samples,unsigned count,size_t size,float epsilon){
    D2MotionMask masks[D2_TEMP_SAMPLES]={0};unsigned matched=0,world_captured=0;
    for(unsigned i=0;i<current->count;i++)world_captured+=current->samples[i].shader==0x99362655u;
    for(unsigned d=0;d<count;d++){
        const D2TemporalSample* sample=&current->samples[draw_samples[d]];
        const D2AlphaSnapshot* alpha=&d2_alpha_snapshots[draw_samples[d]];
        if(alpha->available && alpha->mask && alpha->depth && alpha->frame==current->frame && alpha->draw==sample->draw &&
           alpha->width==s_d3d.width && alpha->height==s_d3d.height){
            masks[d].coverage=alpha->mask;masks[d].depth=alpha->depth;masks[d].pixel_count=size;matched++;
        }
    }
    D2MotionPixel* output=depth_filtered && pixels && count?(D2MotionPixel*)calloc(size,sizeof(D2MotionPixel)):NULL;
    unsigned long long outcomes[D2_MASK_RESULT_COUNT]={0},per_draw[D2_TEMP_SAMPLES][D2_MASK_RESULT_COUNT]={{0}};
    unsigned long long depth_rejected=0,depth_kept=0;
    if(output)for(size_t p=0;p<size;p++){
        if(pixels[p].coverage<=.5f)continue;
        if(depth_filtered[p].coverage<=.5f){depth_rejected++;continue;}
        depth_kept++;
        int code=d2_motion_mask_filter_pixel(&pixels[p],masks,count,p,epsilon,&output[p]);outcomes[code]++;
        float tag=pixels[p].coverage;
        if(isfinite(tag) && tag>=1 && tag<=count && floorf(tag)==tag)per_draw[(unsigned)tag-1][code]++;
    }
    int images=0;char path[600];
    if(output){
        unsigned char* image=(unsigned char*)calloc(size,4);
        if(image){
            for(size_t p=0;p<size;p++){
                unsigned char v=output[p].coverage>.5f?255:0;
                image[p*4]=image[p*4+1]=image[p*4+2]=v;image[p*4+3]=255;
            }
            snprintf(path,sizeof(path),"%s/motion-alpha-depth-filtered-coverage.bmp",d2_temporal_directory);
            cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
            images=d2_hud_bmp_complete(path);memset(image,0,size*4);
            for(size_t p=0;p<size;p++)if(output[p].coverage>.5f){
                double x=.5+(double)output[p].delta_x/512.,y=.5+(double)output[p].delta_y/512.;
                image[p*4]=(u8)(255*(x<0?0:x>1?1:x));image[p*4+1]=(u8)(255*(y<0?0:y>1?1:y));
                image[p*4+2]=128;image[p*4+3]=255;
            }
            snprintf(path,sizeof(path),"%s/motion-alpha-depth-filtered-vectors.bmp",d2_temporal_directory);
            cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
            images=images && d2_hud_bmp_complete(path);free(image);
        }
    }
    /* Learn only from a matched original replay and valid pre-HUD filtering.
       Aggregate once per group so multiple similar draws cannot overweight it. */
    D2SceneryGroup learned[D2_TEMP_SAMPLES];u64 kept[D2_TEMP_SAMPLES]={0},potential[D2_TEMP_SAMPLES]={0};
    unsigned learned_count=0,updated=0;
    if(output && !outcomes[D2_MASK_INVALID_OWNER] && !outcomes[D2_MASK_INVALID_PIXEL] && !outcomes[D2_MASK_DEPTH_MISMATCH]){
        for(unsigned d=0;d<count;d++)if(masks[d].coverage){
            const D2TemporalSample* sample=&current->samples[draw_samples[d]];
            D2SceneryGroup group=d2_scenery_group(sample->shader,sample->fp,sample->texture,sample->geometry_count);
            unsigned g=0;while(g<learned_count && !d2_scenery_group_equal(&learned[g],&group))g++;
            if(g==learned_count)learned[learned_count++]=group;
            kept[g]+=per_draw[d][D2_MASK_KEEP];potential[g]+=d2_alpha_snapshots[draw_samples[d]].surviving_pixels;
        }
        if(learned_count){
            if(d2_scenery_feedback_age==UINT32_MAX){for(unsigned i=0;i<d2_scenery_feedback_count;i++)d2_scenery_feedback[i].age=0;d2_scenery_feedback_age=0;}
            d2_scenery_feedback_age++;
            for(unsigned g=0;g<learned_count;g++)updated+=d2_scenery_feedback_update(d2_scenery_feedback,
                &d2_scenery_feedback_count,&learned[g],kept[g],potential[g],d2_scenery_feedback_age);
            if(updated)d2_scenery_feedback_save();
        }
    }
    snprintf(path,sizeof(path),"%s/motion-alpha-depth-check.json",d2_temporal_directory);
    FILE* json=d2_capture_fopen(path,"wb");
    if(json){
        fprintf(json,"{\"schema\":1,\"purpose\":\"owning-draw-alpha-pre-hud-depth-filter\",\"capture_index\":%u,"
            "\"current_frame\":%llu,\"render_width\":%u,\"render_height\":%u,\"reference_matches_frame\":%s,"
            "\"world_sample_limit\":%u,\"world_records_seen\":%u,\"world_samples_captured\":%u,"
            "\"geometry_vertex_limit\":%u,\"shader_snapshot_budget_bytes\":%u,"
            "\"selection_policy\":\"material-proxy-clusters-with-useful-density-feedback\","
            "\"inventory_scope\":\"eligible-main-target-world-records\","
            "\"inventory_current_complete\":%s,\"inventory_previous_complete\":%s,"
            "\"inventory_current_records\":%u,\"inventory_previous_records\":%u,"
            "\"inventory_ambiguous_fingerprint_records\":%u,\"survey_vertex_budget\":%u,"
            "\"survey_vertices_scanned\":%u,\"survey_records_skipped\":%u,"
            "\"pair_uniqueness_scope\":\"eligible-inventory-fingerprint-and-full-captured-geometry\","
            "\"selected_candidate_screen_coverage_percent\":%.6f,"
            "\"status\":\"%s\",\"depth_filter_source\":\"%s\",\"depth_filter_available\":%s,"
            "\"alpha_depth_filter_applied\":%s,\"matched_original_shader_masks\":%u,\"candidate_draws\":%u,"
            "\"all_candidate_masks_available\":%s,\"owner_scope\":\"this-capture-only\",\"mask_union_used\":false,"
            "\"original_shader_depth_compared\":%s,\"depth_epsilon\":%.9g,\"depth_filtered_pixels\":%llu,"
            "\"depth_rejected_pixels\":%llu,\"alpha_depth_filtered_pixels\":%llu,\"alpha_rejected_pixels\":%llu,"
            "\"mask_unavailable_pixels\":%llu,\"original_depth_mismatch_pixels\":%llu,\"invalid_owner_pixels\":%llu,"
            "\"invalid_motion_pixels\":%llu,\"images_written\":%s,\"candidate_field_available\":%s,"
            "\"coverage_scope\":\"selected-static-candidates-only\",\"full_scene_visibility_validated\":false,"
            "\"blend_contribution_validated\":false,\"motion_vectors_available\":false,\"object_identity_available\":false,"
            "\"units\":\"render-pixels\",\"direction\":\"previous-minus-current-uv\","
            "\"normal_rendering_changed\":false,",
            index,(unsigned long long)current->frame,s_d3d.width,s_d3d.height,
            d2_reference_available && d2_reference_frame==current->frame?"true":"false",
            D2_WORLD_SAMPLES,current->world_records,world_captured,D2_GEOMETRY_LIMIT,D2_ALPHA_SNAPSHOT_BUDGET,
            current->inventory_complete?"true":"false",d2_temporal_frames[d2_temporal_next].inventory_complete?"true":"false",
            current->inventory_count,d2_temporal_frames[d2_temporal_next].inventory_count,current->inventory_ambiguous,
            MAX_VERTICES,current->vertices_scanned,current->inventory_skipped,
            size?100.0*(double)outcomes[D2_MASK_KEEP]/(double)size:0.0,
            !count?"no-candidate-draws":!depth_filtered?"pre-hud-depth-filter-unavailable":!output?"allocation-failed":
                !matched?"no-matched-shader-masks":outcomes[D2_MASK_KEEP]?"candidate-alpha-depth-ready":"no-surviving-candidate-pixels",
            depth_filtered?"before-recognized-hud":"unavailable",depth_filtered?"true":"false",output?"true":"false",
            matched,count,count && matched==count?"true":"false",
            outcomes[D2_MASK_KEEP]+outcomes[D2_MASK_DEPTH_MISMATCH]>0?"true":"false",(double)epsilon,
            depth_kept,depth_rejected,outcomes[D2_MASK_KEEP],outcomes[D2_MASK_SHADER_REJECTED],outcomes[D2_MASK_UNAVAILABLE],
            outcomes[D2_MASK_DEPTH_MISMATCH],outcomes[D2_MASK_INVALID_OWNER],outcomes[D2_MASK_INVALID_PIXEL],images?"true":"false",
            output && outcomes[D2_MASK_KEEP]?"true":"false");
        fprintf(json,"\"eligible_cluster_groups\":%u,\"feedback_records_before_capture\":%u,"
            "\"feedback_cache_loaded_valid\":%s,\"feedback_groups_updated\":%u,"
            "\"feedback_density_denominator\":\"original-shader-surviving-pixels\","
            "\"clustering_identifies_objects\":false,\"samples\":[",
            current->scenery_groups,current->history_records,current->history_cache_valid?"true":"false",updated);
        for(unsigned d=0;d<count;d++){
            fprintf(json,"%s{\"draw\":%u,\"owner_tag\":%u,\"original_shader_mask_available\":%s,"
                "\"estimated_projected_area_pixels\":%.6f,\"utility_score\":%.6f,"
                "\"prior_useful_rate\":%.6f,\"prior_history_observations\":%u,"
                "\"current_inventory_fingerprint_matches\":%u,\"previous_inventory_fingerprint_matches\":%u,"
                "\"kept_pixels\":%llu,\"alpha_rejected_pixels\":%llu,\"mask_unavailable_pixels\":%llu,"
                "\"original_depth_mismatch_pixels\":%llu,\"invalid_motion_pixels\":%llu}",
                d?",":"",current->samples[draw_samples[d]].draw,d+1,masks[d].coverage?"true":"false",
                current->samples[draw_samples[d]].estimated_area,current->samples[draw_samples[d]].utility_score,
                current->samples[draw_samples[d]].history_rate,current->samples[draw_samples[d]].history_count,
                d2_temporal_inventory_matches(current,&current->samples[draw_samples[d]]),
                d2_temporal_inventory_matches(&d2_temporal_frames[d2_temporal_next],&current->samples[draw_samples[d]]),
                per_draw[d][D2_MASK_KEEP],per_draw[d][D2_MASK_SHADER_REJECTED],per_draw[d][D2_MASK_UNAVAILABLE],
                per_draw[d][D2_MASK_DEPTH_MISMATCH],per_draw[d][D2_MASK_INVALID_PIXEL]);
        }
        fprintf(json,"]}\n");fclose(json);
    }
    d2_motion_gap_dump(index,current,pixels,depth_filtered,masks,draw_samples,count,size,epsilon);
    free(output);
}
