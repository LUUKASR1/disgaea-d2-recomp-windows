static void d2_motion_layers_dump(u32 index,const D2TemporalFrame* frame,const D2MotionLayer* layers,int complete,
    const D2MotionPixel* raw,const D2MotionPixel* baseline,const D2MotionMask* masks,unsigned count,size_t size){
    unsigned long long existing=0,eligible=0,recovered=0,ambiguous=0,unavailable=0;
    unsigned char* image=(unsigned char*)calloc(size,4);char path[600];int written=0;
    for(size_t p=0;p<size;p++){
        D2MotionPixel out={0,0,0,0};int old=baseline && baseline[p].coverage>.5f;
        existing+=old;
        int allowed=!old && d2_motion_layer_eligible(raw?&raw[p]:NULL,masks,count,p);
        eligible+=allowed;
        if(allowed){
            if(!complete || !layers || !layers[p].contributors)unavailable++;
            else if(layers[p].ambiguous)ambiguous++;
        }
        int filled=complete && baseline && d2_motion_layer_recover(layers?&layers[p]:NULL,raw?&raw[p]:NULL,&baseline[p],masks,count,p,&out);
        recovered+=filled;
        if(image){image[p*4]=filled?0:old?40:0;image[p*4+1]=filled?210:old?190:0;image[p*4+2]=filled?240:old?70:0;image[p*4+3]=255;}
    }
    if(image){snprintf(path,sizeof(path),"%s/motion-layer-recovery-coverage.bmp",d2_temporal_directory);
        cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
        written=d2_hud_bmp_complete(path);
        memset(image,0,size*4);
        for(size_t p=0;p<size;p++){
            D2MotionPixel value={0,0,0,0};
            if(baseline){
                if(complete)d2_motion_layer_recover(layers?&layers[p]:NULL,raw?&raw[p]:NULL,&baseline[p],masks,count,p,&value);
                else value=baseline[p];
            }
            if(value.coverage>.5f){
                double x=.5+(double)value.delta_x/512.,y=.5+(double)value.delta_y/512.;
                image[p*4]=(unsigned char)(255*(x<0?0:x>1?1:x));image[p*4+1]=(unsigned char)(255*(y<0?0:y>1?1:y));
                image[p*4+2]=128;
            }
            image[p*4+3]=255;
        }
        snprintf(path,sizeof(path),"%s/motion-layer-recovery-vectors.bmp",d2_temporal_directory);
        cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
        written=written && d2_hud_bmp_complete(path);free(image);}
    snprintf(path,sizeof(path),"%s/motion-layer-recovery-check.json",d2_temporal_directory);FILE* file=d2_capture_fopen(path,"wb");if(!file)return;
    fprintf(file,"{\"schema\":1,\"purpose\":\"rejected-owner-static-layer-recovery\",\"capture_index\":%u,\"current_frame\":%llu,"
        "\"render_width\":%u,\"render_height\":%u,\"all_selected_layers_available\":%s,\"baseline_field_available\":%s,"
        "\"baseline_retained_pixels\":%llu,\"own_shader_rejected_recovery_eligible_pixels\":%llu,"
        "\"recovered_pixels\":%llu,\"ambiguous_motion_pixels\":%llu,\"no_validated_layer_pixels\":%llu,"
        "\"combined_retained_pixels\":%llu,\"combined_screen_percent\":%.6f,\"coverage_image_written\":%s,"
        "\"status\":\"%s\",\"motion_agreement_tolerance_pixels\":0.25,"
        "\"scope\":\"selected-static-candidates-only\",\"within_draw_layers_resolved\":false,"
        "\"baseline_changed\":false,\"applied_to_game\":false,\"blend_contribution_validated\":false,\"motion_vectors_available\":false}",
        index,(unsigned long long)frame->frame,s_d3d.width,s_d3d.height,complete?"true":"false",baseline?"true":"false",
        existing,eligible,recovered,ambiguous,unavailable,existing+recovered,size?100.*(double)(existing+recovered)/size:0.,written?"true":"false",
        !complete || !baseline?"layer-validation-unavailable":recovered?"candidate-layer-recovery-ready":"no-recoverable-selected-layer");
    fclose(file);
}
