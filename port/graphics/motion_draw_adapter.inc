/* Per-draw diagnostics only. Owner indices exist only inside one raster call. */
static void d2_motion_draws_dump(u32 index,const D2TemporalFrame* current,const D2MotionDraw* draws,
    const unsigned* sample_indices,unsigned count,const D2MotionPixel* combined,int combined_result){
    char path[600];snprintf(path,sizeof(path),"%s/motion-draws.json",d2_temporal_directory);
    FILE* json=d2_capture_fopen(path,"wb");if(!json)return;
    const size_t size=(size_t)s_d3d.width*s_d3d.height;const float epsilon=8.0f/16777215.0f;
    const int scene_available=d2_scene_depth_available && d2_scene_depth_data && d2_scene_depth_frame==current->frame;
    fprintf(json,"{\"schema\":1,\"purpose\":\"per-draw-geometry-alpha-depth-comparison\","
        "\"capture_index\":%u,\"current_frame\":%llu,\"render_width\":%u,\"render_height\":%u,"
        "\"reference_matches_frame\":%s,\"depth_epsilon\":%.9g,\"scene_depth_available\":%s,"
        "\"scene_depth_comparison_source\":\"final-frame\",\"final_depth_used_for_diagnostics_only\":true,"
        "\"combined_owner_tags_available\":%s,\"owner_scope\":\"this-capture-only\","
        "\"applied_to_motion_field\":false,\"full_scene_visibility_validated\":false,"
        "\"motion_vectors_available\":false,\"normal_rendering_changed\":false,\"samples\":[",
        index,(unsigned long long)current->frame,s_d3d.width,s_d3d.height,
        d2_reference_available && d2_reference_frame==current->frame?"true":"false",(double)epsilon,
        scene_available?"true":"false",combined_result==0?"true":"false");
    D2MotionPixel* isolated=(D2MotionPixel*)calloc(size,sizeof(D2MotionPixel));
    unsigned char* image=(unsigned char*)calloc(size,4);
    for(unsigned d=0;d<count;d++){
        const unsigned slot=sample_indices[d];const D2TemporalSample* sample=&current->samples[slot];
        const D2AlphaSnapshot* alpha=&d2_alpha_snapshots[slot];
        const int alpha_available=alpha->available && alpha->mask && alpha->depth &&
            alpha->frame==current->frame && alpha->draw==sample->draw;
        int result=isolated?d2_motion_raster_gpu(s_d3d.device,s_d3d.cmd_queue,draws+d,1,
            s_d3d.width,s_d3d.height,isolated):(int)E_OUTOFMEMORY;
        unsigned long long geometry=0,owners=0,original=0,overlap=0,geometry_only=0,original_only=0;
        unsigned long long geometry_scene[4]={0},original_scene[4]={0},projected_original[4]={0};
        double maximum_error=0,original_scene_max_error=0;
        if(result==0)for(size_t p=0;p<size;p++){
            int g=isolated[p].coverage>.5f,a=alpha_available && alpha->mask[p]!=0;
            geometry+=g;if(combined_result==0 && combined[p].coverage==(float)(d+1))owners++;
            if(g && scene_available)geometry_scene[d2_depth_relation(isolated[p].depth,d2_scene_depth_data[p],epsilon)]++;
            if(alpha_available){
                original+=a;overlap+=g&&a;geometry_only+=g&&!a;original_only+=a&&!g;
                if(a && scene_available){
                    original_scene[d2_depth_relation(alpha->depth[p],d2_scene_depth_data[p],epsilon)]++;
                    double e=fabs((double)alpha->depth[p]-d2_scene_depth_data[p]);
                    if(isfinite(e) && e>original_scene_max_error)original_scene_max_error=e;
                }
                if(g && a){
                    projected_original[d2_depth_relation(isolated[p].depth,alpha->depth[p],epsilon)]++;
                    double e=fabs((double)isolated[p].depth-alpha->depth[p]);if(isfinite(e) && e>maximum_error)maximum_error=e;
                }
            }
        }
        int geometry_image=0,depth_image=0,difference_image=0;
        if(result==0 && image){
            memset(image,0,size*4);
            for(size_t p=0;p<size;p++){
                unsigned char v=isolated[p].coverage>.5f?255:0;
                image[p*4]=image[p*4+1]=image[p*4+2]=v;image[p*4+3]=255;
            }
            snprintf(path,sizeof(path),"%s/geometry-draw-%u-coverage.bmp",d2_temporal_directory,sample->draw);
            cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
            geometry_image=d2_hud_bmp_complete(path);
            if(scene_available){
                memset(image,0,size*4);
                for(size_t p=0;p<size;p++)if(isolated[p].coverage>.5f){
                    int relation=d2_depth_relation(isolated[p].depth,d2_scene_depth_data[p],epsilon);
                    if(relation==1)image[p*4+1]=255;else if(relation==2)image[p*4]=255;else if(relation==3)image[p*4+2]=255;
                    image[p*4+3]=255;
                }
                snprintf(path,sizeof(path),"%s/geometry-draw-%u-depth-classification.bmp",d2_temporal_directory,sample->draw);
                cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
                depth_image=d2_hud_bmp_complete(path);
            }
            if(alpha_available){
                memset(image,0,size*4);
                for(size_t p=0;p<size;p++){
                    int g=isolated[p].coverage>.5f,a=alpha->mask[p]!=0;
                    /* white overlap, red geometry only, green original only */
                    image[p*4]=g?255:0;image[p*4+1]=a?255:0;image[p*4+2]=g&&a?255:0;image[p*4+3]=255;
                }
                snprintf(path,sizeof(path),"%s/geometry-alpha-draw-%u-difference.bmp",d2_temporal_directory,sample->draw);
                cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
                difference_image=d2_hud_bmp_complete(path);
            }
        }
        int no_cull_ran=0,no_cull_result=0;unsigned long long no_cull_pixels=0;
        if(result==0 && geometry==0 && (draws[d].cull&1u)){
            D2MotionDraw uncull=draws[d];uncull.cull=0;no_cull_ran=1;
            no_cull_result=d2_motion_raster_gpu(s_d3d.device,s_d3d.cmd_queue,&uncull,1,s_d3d.width,s_d3d.height,isolated);
            if(no_cull_result==0)for(size_t p=0;p<size;p++)no_cull_pixels+=isolated[p].coverage>.5f;
        }
        fprintf(json,"%s{\"draw\":%u,\"owner_tag\":%u,\"gpu_result\":%d,\"status\":\"%s\","
            "\"geometry_pixels\":%llu,\"combined_owned_pixels\":%llu,\"geometry_image_written\":%s,"
            "\"geometry_scene_depth\":{\"invalid\":%llu,\"equal\":%llu,\"deeper\":%llu,\"nearer\":%llu},"
            "\"depth_classification_image_written\":%s,\"original_shader_available\":%s,"
            "\"coverage_comparison_available\":%s,\"overlap_depth_comparison_available\":%s,"
            "\"original_shader_pixels\":%llu,\"overlap_pixels\":%llu,\"geometry_only_pixels\":%llu,"
            "\"original_shader_only_pixels\":%llu,\"coverage_difference_image_written\":%s,"
            "\"projected_vs_original_depth\":{\"invalid\":%llu,\"equal\":%llu,\"deeper\":%llu,\"nearer\":%llu,\"maximum_error\":%.9g},"
            "\"original_vs_scene_depth\":{\"invalid\":%llu,\"equal\":%llu,\"deeper\":%llu,\"nearer\":%llu,\"maximum_error\":%.9g},"
            "\"no_cull_comparison_ran\":%s,\"no_cull_gpu_result\":%d,\"no_cull_pixels\":%llu}",
            d?",":"",sample->draw,d+1,result,result?"gpu-error":geometry?"geometry-ready":"no-geometry-pixels",
            geometry,owners,geometry_image?"true":"false",geometry_scene[0],geometry_scene[1],geometry_scene[2],geometry_scene[3],
            depth_image?"true":"false",alpha_available?"true":"false",
            result==0 && alpha_available?"true":"false",result==0 && alpha_available && overlap?"true":"false",
            original,overlap,geometry_only,original_only,
            difference_image?"true":"false",projected_original[0],projected_original[1],projected_original[2],projected_original[3],maximum_error,
            original_scene[0],original_scene[1],original_scene[2],original_scene[3],original_scene_max_error,
            no_cull_ran?"true":"false",no_cull_result,no_cull_pixels);
    }
    fprintf(json,"]}\n");fclose(json);free(image);free(isolated);
}
