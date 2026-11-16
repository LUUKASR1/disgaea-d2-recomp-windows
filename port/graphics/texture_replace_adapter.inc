/* Texture pack for the DirectX 12 renderer (included into the adapted rsx_d3d12_backend.c).
 *
 * vp_upload_tex_slot_orig is the SDK's upload. The wrapper below runs after it: when the texture's bytes match a PNG
 * in the texture pack, the slot's resource is swapped for an RGBA8 copy of that PNG (any size). The draw path reads
 * s_d3d.vp_tex[slot].res, so the swap is visible to it. Before every upload the swap is undone, because the SDK reuses
 * t->res and assumes it still has the guest texture's size and format. */
static int d2_rep_make(VPTexSlot* t, const uint8_t* px, u32 rw, u32 rh)
{
    D3D12_HEAP_PROPERTIES hp = {0}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {0};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = rw; td.Height = rh; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hp, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void**)&t->d2_rep)))
        return 0;
    u32 pitch = (rw * 4 + 255) & ~255u;
    D3D12_HEAP_PROPERTIES hu = {0}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd = {0};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = (u64)pitch * rh; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(s_d3d.device->lpVtbl->CreateCommittedResource(s_d3d.device, &hu, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&t->d2_rep_up))) {
        t->d2_rep->lpVtbl->Release(t->d2_rep); t->d2_rep = NULL; return 0;
    }
    void* mapped = NULL; D3D12_RANGE nr = {0, 0};
    if (FAILED(t->d2_rep_up->lpVtbl->Map(t->d2_rep_up, 0, &nr, &mapped)) || !mapped) {
        t->d2_rep_up->lpVtbl->Release(t->d2_rep_up); t->d2_rep_up = NULL;
        t->d2_rep->lpVtbl->Release(t->d2_rep); t->d2_rep = NULL; return 0;
    }
    for (u32 y = 0; y < rh; y++) memcpy((u8*)mapped + (u64)y * pitch, px + (u64)y * rw * 4, (size_t)rw * 4);
    t->d2_rep_up->lpVtbl->Unmap(t->d2_rep_up, 0, NULL);
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    dst.pResource = t->d2_rep; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = t->d2_rep_up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = rw; src.PlacedFootprint.Footprint.Height = rh;
    src.PlacedFootprint.Footprint.Depth = 1; src.PlacedFootprint.Footprint.RowPitch = pitch;
    s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t->d2_rep;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &b);
    return 1;
}

static void d2_rep_free(VPTexSlot* t)
{
    if (t->d2_rep) { t->d2_rep->lpVtbl->Release(t->d2_rep); t->d2_rep = NULL; }
    if (t->d2_rep_up) { t->d2_rep_up->lpVtbl->Release(t->d2_rep_up); t->d2_rep_up = NULL; }
    t->d2_valid = 0;
}

static int vp_upload_tex_slot(u32 off, u32 w, u32 h, u32 fmt, int cube, u32 mips, u32 src_pitch)
{
    extern uint8_t* vm_base;
    if (!d2_texture_pack_enabled())
        return vp_upload_tex_slot_orig(off, w, h, fmt, cube, mips, src_pitch);
    for (int i = 0; i < VP_TEX_SLOTS; i++) {          /* undo the swap before the SDK looks at the slot */
        VPTexSlot* s = &s_d3d.vp_tex[i];
        if (s->d2_swapped) { s->res = s->d2_orig; s->d2_orig = NULL; s->d2_swapped = 0; }
    }
    int slot = vp_upload_tex_slot_orig(off, w, h, fmt, cube, mips, src_pitch);
    if (slot < 0 || cube || !vm_base) return slot;
    VPTexSlot* t = &s_d3d.vp_tex[slot];
    if (!t->res) return slot;
    if (t->d2_valid && t->d2_key == t->key && t->d2_w == w && t->d2_h == h && t->d2_fmt == fmt &&
        t->d2_csum == t->csum) {
        if (t->d2_rep) { t->d2_orig = t->res; t->res = t->d2_rep; t->d2_swapped = 1; }
        return slot;
    }
    d2_rep_free(t);
    t->d2_valid = 1; t->d2_key = t->key; t->d2_w = w; t->d2_h = h; t->d2_fmt = fmt; t->d2_csum = t->csum;
    rsx_tex_layout tl;
    rsx_texture_layout_pitched(fmt, w, h, src_pitch, &tl);
    u32 basef = fmt & 0x9F;
    int rgba = (tl.fmt == RSX_TEXFMT_R8G8B8A8);
    int bc = tl.compressed;
    if (basef == 0x81 || basef == 0x8B || (!rgba && !bc)) return slot;   /* palette index / 2-channel: left alone */
    u32 rw = 0, rh = 0;
    uint8_t* px = d2_texture_replacement(vm_base + t->off, tl.face_bytes, w, h, fmt & 0xFFu, &rw, &rh);
    if (!px) return slot;
    int ok = d2_rep_make(t, px, rw, rh);
    d2_texture_free(px);
    if (!ok) return slot;
    t->d2_rgba = 1;
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
        sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE sh;
        s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &sh);
        sh.ptr += (u64)(1 + slot) * s_d3d.srv_inc;
        s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, t->d2_rep, &sv, sh);
    }
    t->d2_orig = t->res; t->res = t->d2_rep; t->d2_swapped = 1;
    return slot;
}

