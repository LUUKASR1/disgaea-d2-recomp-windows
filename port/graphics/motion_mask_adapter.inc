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
    snprintf(path,sizeof(path),"%s/motion-alpha-depth-check.json",d2_temporal_directory);
    FILE* json=d2_capture_fopen(path,"wb");
    if(json){
        fprintf(json,"{\"schema\":1,\"purpose\":\"owning-draw-alpha-pre-hud-depth-filter\",\"capture_index\":%u,"
            "\"current_frame\":%llu,\"render_width\":%u,\"render_height\":%u,\"reference_matches_frame\":%s,"
            "\"world_sample_limit\":%u,\"world_records_seen\":%u,\"world_samples_captured\":%u,"
            "\"geometry_vertex_limit\":%u,\"shader_snapshot_budget_bytes\":%u,"
            "\"pair_uniqueness_scope\":\"both-bounded-captured-sets\","
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
            "\"normal_rendering_changed\":false,\"samples\":[",
            index,(unsigned long long)current->frame,s_d3d.width,s_d3d.height,
            d2_reference_available && d2_reference_frame==current->frame?"true":"false",
            D2_WORLD_SAMPLES,current->world_records,world_captured,D2_GEOMETRY_LIMIT,D2_ALPHA_SNAPSHOT_BUDGET,
            size?100.0*(double)outcomes[D2_MASK_KEEP]/(double)size:0.0,
            !count?"no-candidate-draws":!depth_filtered?"pre-hud-depth-filter-unavailable":!output?"allocation-failed":
                !matched?"no-matched-shader-masks":outcomes[D2_MASK_KEEP]?"candidate-alpha-depth-ready":"no-surviving-candidate-pixels",
            depth_filtered?"before-recognized-hud":"unavailable",depth_filtered?"true":"false",output?"true":"false",
            matched,count,count && matched==count?"true":"false",
            outcomes[D2_MASK_KEEP]+outcomes[D2_MASK_DEPTH_MISMATCH]>0?"true":"false",(double)epsilon,
            depth_kept,depth_rejected,outcomes[D2_MASK_KEEP],outcomes[D2_MASK_SHADER_REJECTED],outcomes[D2_MASK_UNAVAILABLE],
            outcomes[D2_MASK_DEPTH_MISMATCH],outcomes[D2_MASK_INVALID_OWNER],outcomes[D2_MASK_INVALID_PIXEL],images?"true":"false",
            output && outcomes[D2_MASK_KEEP]?"true":"false");
        for(unsigned d=0;d<count;d++){
            fprintf(json,"%s{\"draw\":%u,\"owner_tag\":%u,\"original_shader_mask_available\":%s,"
                "\"kept_pixels\":%llu,\"alpha_rejected_pixels\":%llu,\"mask_unavailable_pixels\":%llu,"
                "\"original_depth_mismatch_pixels\":%llu,\"invalid_motion_pixels\":%llu}",
                d?",":"",current->samples[draw_samples[d]].draw,d+1,masks[d].coverage?"true":"false",
                per_draw[d][D2_MASK_KEEP],per_draw[d][D2_MASK_SHADER_REJECTED],per_draw[d][D2_MASK_UNAVAILABLE],
                per_draw[d][D2_MASK_DEPTH_MISMATCH],per_draw[d][D2_MASK_INVALID_PIXEL]);
        }
        fprintf(json,"]}\n");fclose(json);
    }
    free(output);
}
