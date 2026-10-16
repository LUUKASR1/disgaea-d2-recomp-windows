static void d2_motion_gap_dump(u32 index,const D2TemporalFrame* frame,const D2MotionPixel* pixels,
    const D2MotionPixel* filtered,const D2MotionMask* masks,const unsigned* slots,unsigned count,size_t size,float epsilon){
    static const char* names[D2_GAP_COUNT]={"retained-candidate","no-selected-candidate","pre-hud-depth-unavailable",
        "pre-hud-depth-rejected","original-mask-unavailable","own-shader-coverage-rejected","original-depth-mismatch","invalid-evidence"};
    static const unsigned char colors[D2_GAP_COUNT][3]={{40,190,70},{30,50,130},{170,80,210},{240,160,30},
        {0,200,220},{230,50,100},{220,20,20},{255,255,255}};
    unsigned long long totals[D2_GAP_COUNT]={0},blend_unvalidated=0;
    /* Full-frame partition. No screen pixel is silently counted as stationary. */
    unsigned char* image=(unsigned char*)calloc(size,4);
    for(size_t p=0;p<size;p++){
        int code=d2_motion_gap_class(pixels?&pixels[p]:NULL,filtered?&filtered[p]:NULL,masks,count,p,epsilon);
        totals[code]++;
        if(code==D2_GAP_RETAINED){unsigned owner=(unsigned)pixels[p].coverage-1;
            blend_unvalidated+=frame->samples[slots[owner]].blend!=0;}
        if(image){for(unsigned k=0;k<3;k++)image[p*4+k]=colors[code][k];image[p*4+3]=255;}
    }
    char path[600];int written=0;
    if(image){snprintf(path,sizeof(path),"%s/motion-gap-classification.bmp",d2_temporal_directory);
        cap_write_bmp(path,image,s_d3d.width,s_d3d.height,s_d3d.width*4u,DXGI_FORMAT_R8G8B8A8_UNORM);
        written=d2_hud_bmp_complete(path);free(image);}
    snprintf(path,sizeof(path),"%s/motion-gap-check.json",d2_temporal_directory);FILE* file=d2_capture_fopen(path,"wb");
    if(!file)return;
    fprintf(file,"{\"schema\":1,\"purpose\":\"selected-candidate-gap-classification\",\"capture_index\":%u,"
        "\"current_frame\":%llu,\"render_width\":%u,\"render_height\":%u,\"screen_pixels\":%llu,"
        "\"candidate_raster_available\":%s,\"pre_hud_depth_available\":%s,\"reference_matches_frame\":%s,"
        "\"classification_image_written\":%s,\"retained_blend_enabled_pixels\":%llu,"
        "\"blend_contribution_validated\":false,\"motion_vectors_available\":false,"
        "\"no_candidate_identifies_object_type\":false,\"normal_rendering_changed\":false,\"classes\":[",
        index,(unsigned long long)frame->frame,s_d3d.width,s_d3d.height,(unsigned long long)size,pixels?"true":"false",
        filtered?"true":"false",d2_reference_available && d2_reference_frame==frame->frame?"true":"false",written?"true":"false",blend_unvalidated);
    for(unsigned i=0;i<D2_GAP_COUNT;i++)fprintf(file,"%s{\"reason\":\"%s\",\"pixels\":%llu,\"screen_percent\":%.6f,\"rgb\":[%u,%u,%u]}",
        i?",":"",names[i],totals[i],size?100.*(double)totals[i]/(double)size:0.,colors[i][0],colors[i][1],colors[i][2]);
    fprintf(file,"]}\n");fclose(file);
}
