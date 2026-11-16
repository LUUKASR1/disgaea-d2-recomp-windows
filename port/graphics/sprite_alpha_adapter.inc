/* Save direct-path original shader coverage before releasing captured PSOs.
   Static scenery and sprite mask/depth allocations share one 128 MiB budget. */
static void d2_sprite_alpha_reference(const D2TemporalFrame* temporal,size_t retained){
    if(!d2_sprite_goal())return;
    const D2SpriteFrame* current=&d2_sprite_frames[d2_temporal_next^1];const D2SpriteFrame* previous=&d2_sprite_frames[d2_temporal_next];
    if(!current->valid || !previous->valid || current->frame!=temporal->frame || previous->frame+1!=current->frame)return;
    size_t size=(size_t)s_d3d.width*s_d3d.height;unsigned count=0;
    for(unsigned i=0;i<current->count && count<D2_SPRITE_PROJECTION_LIMIT;i++)if(current->keys[i].shader==0x2003CB89u &&
        current->keys[i].vertices==6 && current->keys[i].topology==RSX_TOPOLOGY_TRIANGLES){
        int pair=d2_sprite_unique_pair(current->keys,current->count,previous->keys,previous->count,i);
        if(pair<0 || !current->records[i].projected || !previous->records[pair].projected)continue;count++;
        D2SpriteAlpha* alpha=&d2_sprite_alpha[i];alpha->frame=current->frame;alpha->draw=current->records[i].draw;
        D2TemporalSample sample={0};sample.draw=alpha->draw;sample.shader=current->keys[i].shader;sample.fp=current->keys[i].fragment;
        sample.vertices=sample.geometry_count=6;sample.cb_slot=s_d3d.draws[sample.draw].cb_slot;
        alpha->reason=d2_alpha_unsupported(&sample);
        if(!alpha->reason && (size>8388608 || size*(1+sizeof(float))>D2_ALPHA_SNAPSHOT_BUDGET-retained))alpha->reason="shared-snapshot-budget-exceeded";
        if(alpha->reason)continue;
        alpha->mask=(unsigned char*)calloc(size,1);alpha->depth=(float*)calloc(size,sizeof(float));
        if(!alpha->mask || !alpha->depth){alpha->reason="allocation-failed";free(alpha->mask);free(alpha->depth);alpha->mask=NULL;alpha->depth=NULL;continue;}
        D2AlphaBinding binding={&s_d3d.draws[sample.draw],sample.draw,(u32)(s_d3d.vp_parity^1),d2_alpha_pipelines[sample.draw]};
        void* keep[256];unsigned n=0;keep[n++]=binding.pso;keep[n++]=s_d3d.vp_root_sig;keep[n++]=s_d3d.srv_heap;
        keep[n++]=s_d3d.vp_vb;keep[n++]=s_d3d.vp_cb;keep[n++]=s_d3d.vp_fpcb;
        for(unsigned t=0;t<VP_TEX_SLOTS;t++)if(s_d3d.vp_tex[t].res)keep[n++]=s_d3d.vp_tex[t].res;
        if(s_d3d.tex_resource)keep[n++]=s_d3d.tex_resource;
        alpha->result=d2_alpha_replay_depth(s_d3d.device,s_d3d.cmd_queue,s_d3d.width,s_d3d.height,d2_alpha_bind,&binding,keep,n,alpha->mask,alpha->depth);
        if(alpha->result)alpha->reason="gpu-error";
        else {alpha->available=1;retained+=size*(1+sizeof(float));}
        if(!alpha->available){free(alpha->mask);free(alpha->depth);alpha->mask=NULL;alpha->depth=NULL;}
    }
}
