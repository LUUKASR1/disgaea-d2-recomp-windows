static void d2_sprite_raster_dump(u32 index,const D2TemporalFrame* frame){
    if(!d2_sprite_goal())return;
    const D2SpriteFrame* current=&d2_sprite_frames[d2_temporal_next^1];const D2SpriteFrame* previous=&d2_sprite_frames[d2_temporal_next];
    int frames=current->valid && previous->valid && current->frame==frame->frame && previous->frame+1==current->frame;
    const float* scene=d2_depth_timeline_pre_hud(frame);const float epsilon=8.f/16777215.f;
    size_t size=(size_t)s_d3d.width*s_d3d.height;
    D2MotionPixel* isolated=size<=8388608?(D2MotionPixel*)calloc(size,sizeof(*isolated)):NULL;
    D2MotionLayer* combined=size<=8388608?(D2MotionLayer*)calloc(size,sizeof(*combined)):NULL;
    char path[600];snprintf(path,sizeof(path),"%s/sprite-motion-check.json",d2_temporal_directory);FILE* file=d2_capture_fopen(path,"wb");
    unsigned candidates=0,matched=0,replayed=0;unsigned long long covered=0,ambiguous=0;
    if(file)fprintf(file,"{\"schema\":1,\"purpose\":\"uv-matched-original-shader-sprite-motion\",\"capture_index\":%u,\"current_frame\":%llu,"
        "\"render_width\":%u,\"render_height\":%u,\"pre_hud_depth_available\":%s,\"reference_matches_frame\":%s,"
        "\"shared_snapshot_budget_bytes\":%u,\"object_identity_available\":false,\"full_scene_visibility_validated\":false,"
        "\"blend_contribution_validated\":false,\"motion_vectors_available\":false,\"applied_to_game\":false,\"samples\":[",
        index,(unsigned long long)current->frame,s_d3d.width,s_d3d.height,scene?"true":"false",
        d2_reference_available && d2_reference_frame==current->frame?"true":"false",D2_ALPHA_SNAPSHOT_BUDGET);
    if(frames)for(unsigned i=0;i<current->count && candidates<D2_SPRITE_PROJECTION_LIMIT;i++)if(current->keys[i].shader==0x2003CB89u &&
        current->keys[i].vertices==6 && current->keys[i].topology==RSX_TOPOLOGY_TRIANGLES){
        int pair=d2_sprite_unique_pair(current->keys,current->count,previous->keys,previous->count,i);
        if(pair<0 || !current->records[i].projected || !previous->records[pair].projected)continue;
        const D2SpriteRecord* now=&current->records[i];const D2SpriteRecord* old=&previous->records[pair];
        const D2SpriteAlpha* alpha=&d2_sprite_alpha[i];unsigned map[6]={0};
        int uv=d2_sprite_correspondence(now->uv,old->uv,now->clip,old->clip,map);matched+=uv==D2_SPRITE_UV_MATCH;
        static const char* uv_status[]={"texture-corners-matched","invalid-values","not-a-supported-quad","uv-image-changed","orientation-flipped"};
        const char* reason=uv?uv_status[uv]:!isolated || !combined?"allocation-failed":!scene?"pre-hud-depth-unavailable":
            !alpha->available || alpha->frame!=current->frame || alpha->draw!=now->draw?alpha->reason?alpha->reason:"original-shader-unavailable":NULL;
        int result=0;unsigned long long geometry=0,kept=0,shader_rejected=0,depth_mismatch=0,scene_rejected=0,invalid=0;
        if(!reason){
            D2MotionVertex vertices[6];for(unsigned v=0;v<6;v++){
                memcpy(vertices[v].current_clip,now->clip[v],4*sizeof(float));memcpy(vertices[v].previous_clip,old->clip[map[v]],4*sizeof(float));
            }
            const D3D12DrawRecord* record=&s_d3d.draws[now->draw];D2MotionDraw draw={vertices,6,record->cull};
            result=d2_motion_raster_gpu(s_d3d.device,s_d3d.cmd_queue,&draw,1,s_d3d.width,s_d3d.height,isolated);
            if(result)reason="gpu-raster-error";
            else {
                replayed++;D2MotionMask own={alpha->mask,alpha->depth,size};
                for(size_t p=0;p<size;p++)if(isolated[p].coverage>.5f){
                    geometry++;D2MotionPixel value={0,0,0,0};int code=d2_motion_mask_filter_pixel(&isolated[p],&own,1,p,epsilon,&value);
                    if(code==D2_MASK_SHADER_REJECTED){shader_rejected++;continue;}
                    if(code==D2_MASK_DEPTH_MISMATCH){depth_mismatch++;continue;}
                    if(code!=D2_MASK_KEEP){invalid++;continue;}
                    if(d2_depth_relation(value.depth,scene[p],epsilon)!=1){scene_rejected++;continue;}
                    kept++;d2_motion_layer_add(&combined[p],&isolated[p],&own,p,scene[p],epsilon,now->draw);
                }
            }
        }
        if(file){
            fprintf(file,"%s{\"draw\":%u,\"previous_draw\":%u,\"texture\":\"%08X\",\"status\":\"%s\","
                "\"uv_correspondence\":\"%s\",\"original_shader_available\":%s,\"gpu_result\":%d,"
                "\"geometry_pixels\":%llu,\"own_shader_rejected_pixels\":%llu,\"original_depth_mismatch_pixels\":%llu,"
                "\"scene_depth_rejected_pixels\":%llu,\"invalid_pixels\":%llu,\"retained_pixels\":%llu,\"previous_vertex_indices\":",
                candidates?",":"",now->draw,old->draw,current->keys[i].texture,reason?reason:"candidate-sprite-raster-ready",uv_status[uv],alpha->available?"true":"false",
                result,geometry,shader_rejected,depth_mismatch,scene_rejected,invalid,kept);
            if(!uv){fprintf(file,"[");for(unsigned v=0;v<6;v++)fprintf(file,"%s%u",v?",":"",map[v]);fprintf(file,"]");}else fprintf(file,"null");
            fprintf(file,"}");
        }
        candidates++;
    }
    unsigned char* image=combined?(unsigned char*)calloc(size,4):NULL;int written=0;
    if(combined)for(size_t p=0;p<size;p++){covered+=combined[p].contributors && !combined[p].ambiguous;ambiguous+=combined[p].ambiguous!=0;}
    if(image){
        for(size_t p=0;p<size;p++){unsigned char v=combined[p].contributors && !combined[p].ambiguous?255:0;image[p*4]=image[p*4+1]=image[p*4+2]=v;image[p*4+3]=255;}
        snprintf(path,sizeof(path),"%s/sprite-motion-coverage.bmp",d2_temporal_directory);cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
        written=d2_hud_bmp_complete(path);memset(image,0,size*4);
        for(size_t p=0;p<size;p++){
            if(combined[p].contributors && !combined[p].ambiguous){double x=.5+combined[p].value.delta_x/512.,y=.5+combined[p].value.delta_y/512.;
                image[p*4]=(unsigned char)(255*(x<0?0:x>1?1:x));image[p*4+1]=(unsigned char)(255*(y<0?0:y>1?1:y));image[p*4+2]=128;}
            image[p*4+3]=255;
        }
        snprintf(path,sizeof(path),"%s/sprite-motion-vectors.bmp",d2_temporal_directory);cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
        written=written && d2_hud_bmp_complete(path);
    }
    if(file){fprintf(file,"],\"candidate_pairs\":%u,\"uv_matched_pairs\":%u,\"rasterized_pairs\":%u,\"covered_pixels\":%llu,"
        "\"ambiguous_pixels\":%llu,\"images_written\":%s,\"candidate_sprite_field_available\":%s,"
        "\"scope\":\"signature-unique-unchanged-uv-candidates-only\",\"normal_rendering_changed\":false}\n",
        candidates,matched,replayed,covered,ambiguous,written?"true":"false",covered?"true":"false");fclose(file);}
    free(image);free(isolated);free(combined);
}
