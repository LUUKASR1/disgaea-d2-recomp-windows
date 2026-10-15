/* One explicit request, at most four isolated draws. Runs only after the
   original frame has been submitted; mapped banks use the submitted parity. */
typedef struct {const D3D12DrawRecord* r;u32 draw,parity;ID3D12PipelineState* pso;} D2DrawBinding;
static ID3D12PipelineState* d2_draw_psos[MAX_DRAWS];
static void d2_draw_keep(u32 d,ID3D12PipelineState* pso){
    if(!d2_draw_capture_enabled()||d>=MAX_DRAWS)return;
    if(pso)pso->lpVtbl->AddRef(pso);
    if(d2_draw_psos[d])d2_draw_psos[d]->lpVtbl->Release(d2_draw_psos[d]);d2_draw_psos[d]=pso;
}
static void d2_draw_release(void){if(!d2_draw_capture_enabled())return;for(unsigned d=0;d<MAX_DRAWS;d++)if(d2_draw_psos[d]){d2_draw_psos[d]->lpVtbl->Release(d2_draw_psos[d]);d2_draw_psos[d]=NULL;}}
static void d2_draw_bind(void* lp,void* bp){
    ID3D12GraphicsCommandList* list=(ID3D12GraphicsCommandList*)lp;const D2DrawBinding* b=(const D2DrawBinding*)bp;const D3D12DrawRecord* r=b->r;
    list->lpVtbl->SetPipelineState(list,b->pso);list->lpVtbl->SetGraphicsRootSignature(list,s_d3d.vp_root_sig);
    ID3D12DescriptorHeap* heaps[]={s_d3d.srv_heap};list->lpVtbl->SetDescriptorHeaps(list,1,heaps);
    D3D12_GPU_DESCRIPTOR_HANDLE srv;s_d3d.srv_heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(s_d3d.srv_heap,&srv);
    srv.ptr+=(u64)(DRAW_SRV_BASE+b->draw*4)*s_d3d.srv_inc;list->lpVtbl->SetGraphicsRootDescriptorTable(list,1,srv);
    list->lpVtbl->SetGraphicsRootConstantBufferView(list,0,s_d3d.vp_cb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_cb)+((u64)b->parity*MAX_DRAWS+r->cb_slot)*VP_CB_STRIDE);
    list->lpVtbl->SetGraphicsRootConstantBufferView(list,2,s_d3d.vp_fpcb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_fpcb)+((u64)b->parity*MAX_DRAWS+r->cb_slot)*VP_FPCB_STRIDE);
    D3D12_VERTEX_BUFFER_VIEW vb={s_d3d.vp_vb->lpVtbl->GetGPUVirtualAddress(s_d3d.vp_vb)+(u64)b->parity*MAX_VERTICES*256u,MAX_VERTICES*256u,256u};
    list->lpVtbl->IASetVertexBuffers(list,0,1,&vb);list->lpVtbl->IASetPrimitiveTopology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->lpVtbl->DrawInstanced(list,r->vertex_count,1,r->vb_byte_offset/256u,0);
}
typedef struct {D2DrawBinding bindings[4];u32 count;} D2DrawSequenceBinding;
static void d2_draw_sequence_bind(void* lp,void* bp){
    const D2DrawSequenceBinding* sequence=(const D2DrawSequenceBinding*)bp;
    for(u32 i=0;i<sequence->count;i++)d2_draw_bind(lp,(void*)&sequence->bindings[i]);
}
static void d2_draw_dump(u32 index,u32 count){
    const char* base=d2_capture_directory();if(!base)return;
    char dir[512],path[600];snprintf(dir,sizeof(dir),"%s/frame_%02u",base,index);d2_capture_mkdir(dir);
    D2DrawBinding selected[4];
    unsigned attempted=0,captured=0,empty=0;u32 parity=(u32)(s_d3d.vp_parity^1);
    for(u32 d=0;d<count&&d<MAX_DRAWS&&attempted<8&&captured<4;d++){
        const D3D12DrawRecord* r=&s_d3d.draws[d];
        if(!r->is_vp||r->is_clear||!d2_draw_psos[d]||!d2_draw_pipeline_ready(d2_draw_psos[d])||r->rt_off||r->rt_mrt[0]||r->rt_mrt[1]||r->rt_mrt[2]||
           !r->vertex_count||r->vertex_count>4096||r->vertex_count%3||r->vb_byte_offset%256||r->cb_slot>=MAX_DRAWS||
           r->vb_byte_offset/256u>MAX_VERTICES-r->vertex_count||!s_d3d.vp_vb_mapped||!s_d3d.vp_cb_mapped||!s_d3d.vp_fpcb_mapped)continue;
        if(r->vp_x||r->vp_y||(r->vp_w&&r->vp_w!=d2_graphics_guest_width())||(r->vp_h&&r->vp_h!=d2_graphics_guest_height()))continue;
        int mutable_input=0;for(unsigned u=0;u<4;u++)mutable_input|=r->tex_rt[u]>=0;
        if(mutable_input)continue;
        snprintf(path,sizeof(path),"%s/draw_%03u",dir,d);d2_capture_mkdir(path);attempted++;
        D2DrawBinding binding={r,d,parity,d2_draw_psos[d]};
        void* keep[6]={binding.pso,s_d3d.vp_root_sig,s_d3d.srv_heap,s_d3d.vp_vb,s_d3d.vp_cb,s_d3d.vp_fpcb};
        const void* vb=(const char*)s_d3d.vp_vb_mapped+(u64)parity*MAX_VERTICES*256u+r->vb_byte_offset;
        const void* vc=(const char*)s_d3d.vp_cb_mapped+((u64)parity*MAX_DRAWS+r->cb_slot)*VP_CB_STRIDE;
        const void* fc=(const char*)s_d3d.vp_fpcb_mapped+((u64)parity*MAX_DRAWS+r->cb_slot)*VP_FPCB_STRIDE;
        const char* palette=getenv("D2_PALETTE_CLAMP");
        int result=d2_draw_export(s_d3d.device,s_d3d.cmd_queue,binding.pso,d2_draw_bind,&binding,path,s_d3d.frame_count,d,s_d3d.width,s_d3d.height,
            vb,r->vertex_count,vc,fc,VP_FPCB_STRIDE,DRAW_SRV_BASE+d*4,palette&&strcmp(palette,"1")==0,keep,6);
        if(result==0)selected[captured]=binding;
        captured+=result==0;empty+=result==1;
    }
    /* Largest exact-PSO group. Selected subset only; skipped scene draws are
       not reconstructed or claimed equivalent to the full game frame. */
    u32 best=0,best_count=0;int sequence_result=-1;
    for(u32 i=0;i<captured;i++){u32 n=0;for(u32 j=0;j<captured;j++)n+=selected[j].pso==selected[i].pso;
        if(n>best_count){best=i;best_count=n;}}
    if(best_count>=2){
        D2DrawSequenceBinding sequence;sequence.count=0;u32 ids[4],first_srvs[4];void* keep[9];u32 n=0;
        keep[n++]=s_d3d.vp_root_sig;keep[n++]=s_d3d.srv_heap;keep[n++]=s_d3d.vp_vb;keep[n++]=s_d3d.vp_cb;keep[n++]=s_d3d.vp_fpcb;
        for(u32 i=0;i<captured;i++)if(selected[i].pso==selected[best].pso){
            first_srvs[sequence.count]=DRAW_SRV_BASE+selected[i].draw*4;ids[sequence.count]=selected[i].draw;sequence.bindings[sequence.count++]=selected[i];keep[n++]=selected[i].pso;}
        sequence_result=d2_draw_sequence_reference(s_d3d.device,s_d3d.cmd_queue,d2_draw_sequence_bind,&sequence,dir,s_d3d.frame_count,s_d3d.width,s_d3d.height,ids,first_srvs,sequence.count,keep,n);
    }
    snprintf(path,sizeof(path),"%s/complete.json",dir);FILE* json=d2_capture_fopen(path,"wb");
    if(json){fprintf(json,"{\"schema\":1,\"status\":\"%s\",\"purpose\":\"isolated-real-draw-packets\",\"frame\":%llu,\"attempted\":%u,\"captured\":%u,\"empty_references\":%u,\"sequence_draws\":%u,\"sequence_captured\":%s,\"vulkan_replayed\":false}\n",
        captured?"complete":"failed",(unsigned long long)s_d3d.frame_count,attempted,captured,empty,best_count,sequence_result==0?"true":"false");fclose(json);}
    d2_draw_release();
}
