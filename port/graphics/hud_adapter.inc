/* Included in the generated audited renderer, before render_frame(). */
static int d2_hud_record_candidate(const D3D12DrawRecord* r, u32 parity)
{
    if(!r->is_vp || r->is_clear || r->rt_off || r->rt_mrt[0] || r->rt_mrt[1] || r->rt_mrt[2] ||
       r->d2_capture_vs_hash!=0x2003CB89u || !s_d3d.vp_vb_mapped || !r->vertex_count ||
       r->vb_byte_offset%256u || r->vb_byte_offset/256u>MAX_VERTICES ||
       r->vertex_count>MAX_VERTICES-r->vb_byte_offset/256u) return 0;
    for(unsigned u=0;u<4;u++) if(r->tex_rt[u]>=0) return 0;
    const void* vertices=(const char*)s_d3d.vp_vb_mapped+(u64)parity*MAX_VERTICES*256u+r->vb_byte_offset;
    return d2_hud_vertices(vertices,r->vertex_count,256u);
}

static u32 d2_hud_find_boundary(u32 count)
{
    if(count>MAX_DRAWS || !count || !s_d3d.vp_ready || !s_d3d.vp_fpcb ||
       getenv("GCM_GUEST_FB") || getenv("GCM_COMPOSITE_RT") || getenv("SUBVP_RTT")) return count;
    for(u32 d=0;d<count;d++) {
        const D3D12DrawRecord* r=&s_d3d.draws[d];
        if(!r->is_vp || r->is_clear || r->rt_off || r->rt_mrt[0] || r->rt_mrt[1] || r->rt_mrt[2]) return count;
    }
    u32 start=count;
    while(start && d2_hud_record_candidate(&s_d3d.draws[start-1],(u32)s_d3d.vp_parity)) start--;
    if(!start || start==count) return count;
    int scene=0;
    for(u32 d=0;d<start;d++) {
        if(d2_hud_record_candidate(&s_d3d.draws[d],(u32)s_d3d.vp_parity)) return count;
        if(s_d3d.draws[d].d2_capture_vs_hash==0x99362655u) scene=1;
    }
    return scene?start:count;
}

static int d2_hud_draws(ID3D12GraphicsCommandList* list,u32 start,u32 count,u32 frame,int post)
{
    if(!list) return -1;
    const u32 parity=(u32)(s_d3d.vp_parity^1);
    list->lpVtbl->SetGraphicsRootSignature(list,s_d3d.vp_root_sig);
    ID3D12DescriptorHeap* heaps[]={s_d3d.srv_heap};
    list->lpVtbl->SetDescriptorHeaps(list,1,heaps);
    list->lpVtbl->IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW vbv={0};
    vbv.BufferLocation=s_d3d.vp_vb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_vb)+(u64)parity*MAX_VERTICES*256u;
    vbv.SizeInBytes=MAX_VERTICES*256u;vbv.StrideInBytes=256u;
    list->lpVtbl->IASetVertexBuffers(list,0,1,&vbv);
    D3D12_GPU_DESCRIPTOR_HANDLE base;
    s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap,&base);
    for(u32 d=start;d<count;d++) {
        const D3D12DrawRecord* r=&s_d3d.draws[d];
        ID3D12PipelineState* pso=post?d2_posthud_pipeline(frame,d-start):r->d2_hud_pso;
        if(!pso)return -1;
        list->lpVtbl->SetPipelineState(list,pso);
        list->lpVtbl->OMSetStencilRef(list,r->d2_stencil_ref);
        /* Same viewport/scissor calculation as the adapted original main target. */
        float tw=post?(float)d2_graphics_output_width():(float)s_d3d.width;
        float th=post?(float)d2_graphics_output_height():(float)s_d3d.height;
        float gw=(float)d2_graphics_guest_width(),gh=(float)d2_graphics_guest_height();
        D3D12_VIEWPORT viewport={0,0,tw,th,0,1};
        if(r->vp_w>=2 && r->vp_h>=2 && (float)(r->vp_x+r->vp_w)<=gw+0.5f && (float)(r->vp_y+r->vp_h)<=gh+0.5f) {
            viewport.TopLeftX=(float)r->vp_x*tw/gw;viewport.TopLeftY=(float)r->vp_y*th/gh;
            viewport.Width=(float)r->vp_w*tw/gw;viewport.Height=(float)r->vp_h*th/gh;
        }
        D3D12_RECT scissor={(LONG)viewport.TopLeftX,(LONG)viewport.TopLeftY,
            (LONG)(viewport.TopLeftX+viewport.Width),(LONG)(viewport.TopLeftY+viewport.Height)};
        list->lpVtbl->RSSetViewports(list,1,&viewport);list->lpVtbl->RSSetScissorRects(list,1,&scissor);
        D3D12_GPU_DESCRIPTOR_HANDLE texture=base;texture.ptr+=(u64)(DRAW_SRV_BASE+d*4u)*s_d3d.srv_inc;
        list->lpVtbl->SetGraphicsRootDescriptorTable(list,1,texture);
        list->lpVtbl->SetGraphicsRootConstantBufferView(list,0,
            s_d3d.vp_cb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_cb)+((u64)parity*MAX_DRAWS+r->cb_slot)*VP_CB_STRIDE);
        list->lpVtbl->SetGraphicsRootConstantBufferView(list,2,
            s_d3d.vp_fpcb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_fpcb)+((u64)parity*MAX_DRAWS+r->cb_slot)*VP_FPCB_STRIDE);
        list->lpVtbl->DrawInstanced(list,r->vertex_count,1,r->vb_byte_offset/256u,0);
    }
    return 0;
}

static int d2_hud_replay(u32 start,u32 count,unsigned pass)
{
    for(u32 d=start;d<count;d++)if(!s_d3d.draws[d].d2_hud_pso)return -1;
    ID3D12GraphicsCommandList* list=d2_hud_begin(pass);
    if(d2_hud_draws(list,start,count,0,0)!=0)return -1;
    return d2_hud_end();
}

static int d2_posthud_supported(u32 start,u32 count)
{
    if(start>=count)return 0;
    /* Stencil-dependent HUD requires a separate stencil reconstruction path. */
    for(u32 d=start;d<count;d++)if(s_d3d.draws[d].d2_zs.stencil ||
        s_d3d.draws[d].d2_zs.front || s_d3d.draws[d].d2_zs.back)return 0;
    return 1;
}

static int d2_posthud_present(u32 frame,u32 start,u32 count)
{
    for(u32 d=start;d<count;d++)if(!d2_posthud_pipeline(frame,d-start))return -1;
    ID3D12GraphicsCommandList* list=d2_posthud_begin(frame,d2_graphics_output(frame));
    if(d2_hud_draws(list,start,count,frame,1)!=0)return -1;
    return d2_posthud_end(frame);
}

static int d2_hud_bmp_complete(const char* path)
{
    FILE* file=d2_capture_fopen(path,"rb");if(!file)return 0;
    int seek=fseek(file,0,SEEK_END);long bytes=seek==0?ftell(file):-1;fclose(file);
    u64 expected=54u+(u64)((s_d3d.width*3u+3u)&~3u)*s_d3d.height;
    return bytes>=0 && (u64)bytes==expected;
}

static void d2_hud_dump(u32 fi,u32 count,u32 start,u32 index,int snapshotted)
{
    const char* base=d2_capture_directory();char dir[300],path[512];
    snprintf(dir,sizeof(dir),"%s/frame_%02u",base?base:"rsxcap",index);
    int supported=snapshotted && start<count;
    uint64_t rgb=0,alpha=0;uint32_t maximum=0;
    int replay_ok=supported && d2_hud_replay(start,count,0)==0 && d2_hud_replay(start,count,1)==0;
    if(replay_ok) {
        int images_ok=1;
        const char* names[]={"scene.bmp","hud.bmp","recomposed.bmp"};
        for(unsigned kind=0;kind<3;kind++) {
            snprintf(path,sizeof(path),"%s/%s",dir,names[kind]);
            cap_readback_write(d2_hud_resource(kind),s_d3d.width,s_d3d.height,DXGI_FORMAT_R8G8B8A8_UNORM,
                D3D12_RESOURCE_STATE_COMMON,fi,path);
            images_ok=images_ok && d2_hud_bmp_complete(path);
        }
        if(d2_hud_compare(s_d3d.render_targets[fi],&rgb,&alpha,&maximum)!=0) replay_ok=0;
        if(!images_ok)replay_ok=0;
    }
    snprintf(path,sizeof(path),"%s/hud-split.json",dir);
    FILE* file=d2_capture_fopen(path,"wb");
    if(file) {
        fprintf(file,"{\"schema\":1,\"status\":\"%s\",\"boundary\":%u,\"hud_draws\":%u,"
            "\"full_geometry_checked\":true,\"reference\":\"backbuffer.bmp\",\"scene\":\"scene.bmp\","
            "\"hud\":\"hud.bmp\",\"recomposed\":\"recomposed.bmp\",\"different_rgb_pixels\":%llu,"
            "\"different_alpha_pixels\":%llu,\"maximum_rgb_error\":%u,"
            "\"render_width\":%u,\"render_height\":%u,"
            "\"normal_rendering_changed\":false,\"upscale_before_HUD\":false}\n",
            !supported?"not-supported":!replay_ok?"failed":rgb||alpha?"mismatch":"exact-match",
            start,supported?count-start:0,(unsigned long long)rgb,(unsigned long long)alpha,maximum,
            s_d3d.width,s_d3d.height);
        fclose(file);
    }
    fprintf(stderr,"[D2HUD] split=%s boundary=%u draws=%u RGB-different=%llu alpha-different=%llu max=%u\n",
        !supported?"not-supported":!replay_ok?"failed":rgb||alpha?"mismatch":"exact-match",
        start,supported?count-start:0,(unsigned long long)rgb,(unsigned long long)alpha,maximum);
    for(u32 d=start;d<count && d<MAX_DRAWS;d++) {
        if(s_d3d.draws[d].d2_hud_pso) s_d3d.draws[d].d2_hud_pso->lpVtbl->Release(s_d3d.draws[d].d2_hud_pso);
        s_d3d.draws[d].d2_hud_pso=NULL;
    }
}

static void d2_capture_output(u32 frame,u32 count,u32 index,int post)
{
    const char* base=d2_capture_directory();char dir[300],path[512];
    snprintf(dir,sizeof(dir),"%s/frame_%02u",base?base:"rsxcap",index);
    ID3D12Resource* output=d2_graphics_output(frame);
    if(!output)output=s_d3d.render_targets[frame];
    snprintf(path,sizeof(path),"%s/output.bmp",dir);
    cap_readback_write(output,d2_graphics_output_width(),d2_graphics_output_height(),DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_PRESENT,frame,path);
    const char* upscaler=getenv("D2_UPSCALER");
    int native_comparison=post && (!upscaler || strcmp(upscaler,"native")==0) &&
        s_d3d.width==d2_graphics_output_width() && s_d3d.height==d2_graphics_output_height();
    uint64_t rgb=0,alpha=0;uint32_t maximum=0;
    int compared=native_comparison && d2_hud_compare_pair(s_d3d.render_targets[frame],output,&rgb,&alpha,&maximum)==0;
    snprintf(path,sizeof(path),"%s/presentation.json",dir);FILE* file=d2_capture_fopen(path,"wb");
    if(file){
        fprintf(file,"{\"schema\":1,\"hud_after_upscale\":%s,\"render_width\":%u,\"render_height\":%u,"
            "\"output_width\":%u,\"output_height\":%u,\"native_comparison_available\":%s,"
            "\"status\":\"%s\",\"different_rgb_pixels\":%llu,\"different_alpha_pixels\":%llu,\"maximum_rgb_error\":%u}\n",
            post?"true":"false",s_d3d.width,s_d3d.height,d2_graphics_output_width(),d2_graphics_output_height(),
            compared?"true":"false",!post?"fallback":!native_comparison?"scaled-output":!compared?"comparison-failed":rgb||alpha?"mismatch":"exact-match",
            (unsigned long long)rgb,(unsigned long long)alpha,maximum);fclose(file);
    }
    fprintf(stderr,"[D2POSTHUD] capture mode=%s native-compare=%d RGB=%llu alpha=%llu max=%u\n",post?"separate":"fallback",compared,
        (unsigned long long)rgb,(unsigned long long)alpha,maximum);
    d2_capture_finish(dir,s_d3d.frame_count,count);
}
