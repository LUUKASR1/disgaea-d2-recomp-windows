"""Adapt the audited Windows SDK renderer in a build copy; preserve the SDK lock."""
from pathlib import Path
import hashlib
import re
import sys

EXPECTED = '5ae65fef33ca9089af988486deafd08e1664be659e42f95c566c6d6839585421'


def replace(text, old, new):
    if text.count(old) != 1: raise RuntimeError('Renderer anchor changed: ' + old[:70])
    return text.replace(old, new)


def generate(source, output):
    source = Path(source).resolve(); data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED: raise RuntimeError('Pinned renderer differs; SDK preserved')
    text = data.decode()
    # Persistent shader/pipeline cache: the guest pixel/vertex shaders and PSOs are built on the render thread the first time a
    # scene needs them, which is what the user saw as 100-700 ms hitches. Later runs read them back from D2_SHADER_CACHE_DIR.
    text = replace(text, '    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), "guest_fp", NULL, NULL,\n                            "main", "ps_5_0", 0, 0, &pb, &e);',
        '    HRESULT hr = d2_cached_compile(hlsl, strlen(hlsl), "guest_fp", "ps_5_0", (void**)&pb, (void**)&e);')
    text = replace(text, '    HRESULT hr = D3DCompile(hlsl, strlen(hlsl), "guest_vp2", NULL, NULL,\n                            "main", "vs_5_0", 0, 0, &vb, &e);',
        '    HRESULT hr = d2_cached_compile(hlsl, strlen(hlsl), "guest_vp2", "vs_5_0", (void**)&vb, (void**)&e);')
    text = replace(text, '    ID3D12PipelineState* pso = NULL;\n    hr = s_d3d.device->lpVtbl->CreateGraphicsPipelineState(\n        s_d3d.device, &pd, &IID_ID3D12PipelineState, (void**)&pso);\n    pb->lpVtbl->Release(pb);',
        '    ID3D12PipelineState* pso = NULL;\n    hr = d2_create_pso_cached(s_d3d.device, &pd, (void**)&pso);\n    pb->lpVtbl->Release(pb);')
    text = replace(text, '    s_d3d.width = width;\n    s_d3d.height = height;',
        '    if (d2_graphics_configure(width, height) != 0) return -1;\n'
        '    width = d2_graphics_render_width(); height = d2_graphics_render_height();\n'
        '    s_d3d.width = width;\n    s_d3d.height = height;')
    text = replace(text, 's_d3d.hwnd = create_window(width, height, title);',
        's_d3d.hwnd = create_window(d2_graphics_output_width(), d2_graphics_output_height(), title);')
    text = replace(text, '    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;',
        '    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;\n'
        '    sc_desc.Flags |= d2_display_swap_flags(factory);')
    text = replace(text, 's_d3d.hwnd = create_window(d2_graphics_output_width(), d2_graphics_output_height(), title);',
        's_d3d.hwnd = create_window(d2_graphics_output_width(), d2_graphics_output_height(), title);\n'
        '    d2_display_start(s_d3d.hwnd);')
    text = replace(text, '    sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;',
        '    sc_desc.Format = d2_graphics_hdr_enabled() ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;')
    text = replace(text, '    if (FAILED(hr)) {\n        printf("[D3D12] ERROR: CreateSwapChainForHwnd failed',
        '    if (FAILED(hr) && d2_graphics_hdr_enabled()) {\n'
        '        printf("[D3D12] HDR swap chain failed (0x%08lX); retrying as SDR\\n", hr);\n'
        '        d2_graphics_hdr_disable();\n'
        '        sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;\n'
        '        hr = factory->lpVtbl->CreateSwapChainForHwnd(\n'
        '            factory, (IUnknown*)s_d3d.cmd_queue, s_d3d.hwnd, &sc_desc, NULL, NULL, &swap_chain1);\n'
        '    }\n'
        '    if (FAILED(hr)) {\n        printf("[D3D12] ERROR: CreateSwapChainForHwnd failed')
    text = replace(text, '    sc_desc.Width = width;', '    sc_desc.Width = d2_graphics_output_width();')
    text = replace(text, '    sc_desc.Height = height;', '    sc_desc.Height = d2_graphics_output_height();')
    text = replace(text, '    /* Create RTV descriptor heap */',
        '    if (d2_graphics_start(s_d3d.device, s_d3d.cmd_queue, s_d3d.swap_chain) != 0) return -1;\n'
        '    /* Create RTV descriptor heap */')
    old = '''        hr = s_d3d.swap_chain->lpVtbl->GetBuffer(
            s_d3d.swap_chain, i, &IID_ID3D12Resource, (void**)&s_d3d.render_targets[i]);'''
    text = replace(text, old, '''        int graphics_scene = d2_graphics_scene(i, (void**)&s_d3d.render_targets[i]);
        if (graphics_scene < 0) return -1;
        hr = S_OK;
        if (!graphics_scene) hr = s_d3d.swap_chain->lpVtbl->GetBuffer(
            s_d3d.swap_chain, i, &IID_ID3D12Resource, (void**)&s_d3d.render_targets[i]);''')
    text = replace(text, 'depth_desc.Width              = 2048;',
        'depth_desc.Width              = width > 2048 ? width : 2048;')
    text = replace(text, 'depth_desc.Height             = 2048;',
        'depth_desc.Height             = height > 2048 ? height : 2048;')
    text = replace(text, 'depth_desc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;',
        'depth_desc.Format             = d2_posthud_enabled()?DXGI_FORMAT_R24G8_TYPELESS:DXGI_FORMAT_D24_UNORM_S8_UINT;')
    # Classify original guest surfaces using guest dimensions, not host raster sizes.
    for old,new in [
        ('st->surface_clip_w == s_d3d.width && st->surface_clip_h == s_d3d.height',
         'st->surface_clip_w == d2_graphics_guest_width() && st->surface_clip_h == d2_graphics_guest_height()'),
        ('dr->tex[_u].w == s_d3d.width','dr->tex[_u].w == d2_graphics_guest_width()'),
        ('dr->tex[_u].h == s_d3d.height','dr->tex[_u].h == d2_graphics_guest_height()'),
        ('dr->vp_w < s_d3d.width','dr->vp_w < d2_graphics_guest_width()'),
        ('dr->vp_w == s_d3d.width','dr->vp_w == d2_graphics_guest_width()')]:
        if old not in text: raise RuntimeError('Guest surface classification changed')
        text = text.replace(old,new)
    text = replace(text, '''                    if (dr->vp_w >= 2 && dr->vp_h >= 2 &&
                        (float)(dr->vp_x + dr->vp_w) <= tw + 0.5f &&
                        (float)(dr->vp_y + dr->vp_h) <= th + 0.5f) {
                        dvp.TopLeftX = (float)dr->vp_x;
                        dvp.TopLeftY = (float)dr->vp_y;
                        dvp.Width    = (float)dr->vp_w;
                        dvp.Height   = (float)dr->vp_h;
                    }''', '''                    float gw = cur_rt >= 0 ? tw : (float)d2_graphics_guest_width();
                    float gh = cur_rt >= 0 ? th : (float)d2_graphics_guest_height();
                    if (cur_rt < 0 && d2_vertplus_use_16x9(d, dr, (u32)s_d3d.vp_parity)) {
                        float ox = 0.0f, oy = 0.0f, sc = 1.0f;
                        d2_vertplus_16x9_box(tw, th, gw, gh, &ox, &oy, &sc);
                        if (dr->vp_w >= 2 && dr->vp_h >= 2 &&
                            (float)(dr->vp_x + dr->vp_w) <= gw + 0.5f &&
                            (float)(dr->vp_y + dr->vp_h) <= gh + 0.5f) {
                            dvp.TopLeftX = ox + (float)dr->vp_x * sc;
                            dvp.TopLeftY = oy + (float)dr->vp_y * sc;
                            dvp.Width    = (float)dr->vp_w * sc;
                            dvp.Height   = (float)dr->vp_h * sc;
                        } else {
                            dvp.TopLeftX = ox;
                            dvp.TopLeftY = oy;
                            dvp.Width    = gw * sc;
                            dvp.Height   = gh * sc;
                        }
                    } else if (dr->vp_w >= 2 && dr->vp_h >= 2 &&
                        (float)(dr->vp_x + dr->vp_w) <= gw + 0.5f &&
                        (float)(dr->vp_y + dr->vp_h) <= gh + 0.5f) {
                        dvp.TopLeftX = (float)dr->vp_x * tw / gw;
                        dvp.TopLeftY = (float)dr->vp_y * th / gh;
                        dvp.Width    = (float)dr->vp_w * tw / gw;
                        dvp.Height   = (float)dr->vp_h * th / gh;
                    }''')
    text = replace(text, '        s_d3d.swap_chain->lpVtbl->Present(s_d3d.swap_chain, (UINT)d2_interval, 0);',
        '        if (d2_graphics_present(fi, d2_post_snapshotted?d2_posthud_scene(fi):s_d3d.render_targets[fi]) != 0 ||\n'
        '            (d2_post_snapshotted && d2_posthud_present(fi,d2_hud_start,s_dbg_last_draws)!=0)) {\n'
        '            s_d3d.window_closed = 1; return;\n        }\n'
        '        if(d2_capture_requested)d2_capture_output(fi,s_dbg_last_draws,d2_capture_index,d2_post_snapshotted,d2_post_reason);\n'
        '        if(d2_posthud_enabled()){static const char* previous=NULL;\n'
        '            const char* reason=d2_post_snapshotted?"separate":d2_post_reason;\n'
        '            if(!previous || strcmp(previous,reason)!=0){previous=reason;\n'
        '                fprintf(stderr,"[D2POSTHUD] presentation=%s reason=%s\\n",d2_post_snapshotted?"HUD-after-upscale":"fallback",reason);}}\n'
        '        if(d2_display_present(s_d3d.swap_chain,(UINT)d2_interval)!=0){s_d3d.window_closed=1;return;}')
    text = replace(text, '    /* Release D3D12 resources */',
        '    d2_posthud_shutdown();\n    d2_hud_shutdown();\n    d2_draw_release();\n    d2_draw_capture_close();\n    d2_capture_shutdown();\n    d2_graphics_shutdown();\n    /* Release D3D12 resources */')
    text = replace(text, '    u32 d2_diag_valid;',
        '    ID3D12PipelineState* d2_hud_pso;\n    ID3D12PipelineState* d2_cached_pso;\n    u32 d2_capture_vs_hash;\n    u32 d2_diag_valid;')
    text = replace(text, '        r->d2_diag_valid = enabled ? 1u : 0u;',
        '        r->d2_capture_vs_hash = (enabled || d2_posthud_enabled() || d2_graphics_vertplus_enabled()) && vs_idx >= 0 && vs_idx < s_d3d.vp_vs_n\n'
        '            ? s_d3d.vp_vs[vs_idx].hash : 0;\n'
        '        r->d2_diag_valid = enabled ? 1u : 0u;')
    # Existing readbacks execute only upon request and restore each resource state.
    # Convert capture-only paths to UTF-8-aware Win32 IO; preserve ordinary SDK IO.
    start = text.index('static void cap_write_bmp(')
    end = text.index('/* Decompile the captured RSX vertex program', start)
    capture = text[start:end].replace('fopen(', 'd2_capture_fopen(')
    capture = replace(capture, 'const char* base = getenv("RSX_CAP_DIR");',
        'const char* base = d2_capture_directory();')
    capture = capture.replace('CreateDirectoryA(base, NULL);', 'd2_capture_mkdir(base);')
    capture = capture.replace('CreateDirectoryA(dir, NULL);', 'd2_capture_mkdir(dir);')
    capture = replace(capture, '                if(r->is_vp) {',
        '                if(r->is_vp) {\n'
        '                    fprintf(mf,"      SHADER vs-record-hash=%08X fp-address=%08X\\n",\n'
        '                        r->d2_capture_vs_hash,r->fp_addr);')
    capture = capture.replace('+ d) * VP_CB_STRIDE', '+ r->cb_slot) * VP_CB_STRIDE')
    text = text[:start] + capture + text[end:]
    text = replace(text, 'static void render_frame(void)\n{',
        '#include "vertplus_adapter.inc"\n#include "hud_adapter.inc"\n#include "outline_adapter.inc"\n#include "temporal_adapter.inc"\n\nstatic void render_frame(void)\n{')
    text = replace(text, '                s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list,\n                                                         dpso ? dpso : vpso);',
        '                s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list,\n                                                         dpso ? dpso : vpso);\n                d2_alpha_record_pso(d,dpso);')
    text = replace(text, '    off = _off0; mapped = _map0;\n    t->up->lpVtbl->Unmap(t->up, 0, NULL);',
        '    if (!cube) d2_texture_dump(vm_base + off, tl.face_bytes, w, h, fmt & 0xFFu, (int)tl.fmt, mapped, pitch);\n'
        '    off = _off0; mapped = _map0;\n    t->up->lpVtbl->Unmap(t->up, 0, NULL);')
    text = replace(text, 'static int vp_upload_tex_slot(u32 off, u32 w, u32 h, u32 fmt, int cube, u32 mips, u32 src_pitch)\n{',
        'static int vp_upload_tex_slot_orig(u32 off, u32 w, u32 h, u32 fmt, int cube, u32 mips, u32 src_pitch)\n{')
    text = replace(text, '    int used;           /* referenced this frame */\n} VPTexSlot;',
        '    int used;           /* referenced this frame */\n'
        '    ID3D12Resource *d2_rep, *d2_rep_up, *d2_orig; /* texture pack replacement (see texture_replace_adapter.inc) */\n'
        '    u32 d2_key, d2_w, d2_h, d2_fmt, d2_csum; int d2_valid, d2_swapped, d2_rgba;\n'
        '} VPTexSlot;')
    text = replace(text, '/* ---------------------------------------------------------------------------\n * Render-to-texture: offscreen RT pool.',
        '#include "texture_replace_adapter.inc"\n/* ---------------------------------------------------------------------------\n * Render-to-texture: offscreen RT pool.')
    text = replace(text, '                        DXGI_FORMAT sf =\n                            (_bf == 0x85',
        '                        DXGI_FORMAT sf = s_d3d.vp_tex[ts].d2_swapped ? DXGI_FORMAT_R8G8B8A8_UNORM :\n                            (_bf == 0x85')
    text = replace(text, '                s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list,\n                    dr->vertex_count, 1, dr->vb_byte_offset / 256, 0);\n                /* VP_SUBMIT',
        '                if (d2_graphics_outline_level() > 0 && dpso && d2_outline_is_character(dr, d)) {\n'
        '                    ID3D12PipelineState* opso = d2_outline_pso(dr);\n'
        '                    if (opso) {\n'
        '                        static const int ooff[8][2] = {{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}};\n'
        '                        int opx = d2_graphics_outline_level() * (dvp_h >= 1000.0f ? (int)(dvp_h / 720.0f + 0.5f) : 1);\n'
        '                        if (opx < 1) opx = 1;\n'
        '                        s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list, opso);\n'
        '                        for (int _o = 0; _o < 8; _o++) {\n'
        '                            D3D12_VIEWPORT ovp = {dvp_x + (float)(ooff[_o][0] * opx), dvp_y + (float)(ooff[_o][1] * opx), dvp_w, dvp_h, 0.0f, 1.0f};\n'
        '                            s_d3d.cmd_list->lpVtbl->RSSetViewports(s_d3d.cmd_list, 1, &ovp);\n'
        '                            s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list, dr->vertex_count, 1, dr->vb_byte_offset / 256, 0);\n'
        '                        }\n'
        '                        D3D12_VIEWPORT rvp = {dvp_x, dvp_y, dvp_w, dvp_h, 0.0f, 1.0f};\n'
        '                        s_d3d.cmd_list->lpVtbl->RSSetViewports(s_d3d.cmd_list, 1, &rvp);\n'
        '                        s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list, dpso);\n'
        '                    }\n'
        '                }\n'
        '                s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list,\n                    dr->vertex_count, 1, dr->vb_byte_offset / 256, 0);\n                /* VP_SUBMIT')
    text = replace(text, '    wait_for_gpu();\n\n    /* Compile the real vertex program once',
        '    wait_for_gpu();\n    d2_posthud_frame(fi);\n\n    /* Compile the real vertex program once')
    text = replace(text, '    /* Transition render target to RENDER_TARGET state */',
        '    u32 d2_capture_index=0,d2_hud_start=s_d3d.draw_count;\n'
        '    int d2_hud_snapshotted=0;\n'
        '    int d2_post_ready=0,d2_post_snapshotted=0;\n'
        '    const char* d2_post_reason=d2_posthud_enabled()?"not-evaluated":"option-disabled";\n'
        '    int d2_capture_requested=s_present_this_frame && s_d3d.draw_count>0 &&\n'
        '        d2_temporal_request(&d2_capture_index,s_d3d.hwnd);\n'
        '    if(d2_capture_requested || (d2_posthud_enabled() && s_present_this_frame)) {\n'
        '        for(u32 d=0;d<s_d3d.draw_count && d<MAX_DRAWS;d++) s_d3d.draws[d].d2_hud_pso=NULL;\n'
        '        d2_hud_start=d2_hud_find_boundary(s_d3d.draw_count);\n'
        '        if(d2_capture_requested && d2_hud_start<s_d3d.draw_count &&\n'
        '           d2_hud_prepare(s_d3d.device,s_d3d.cmd_queue,s_d3d.render_targets[fi],s_d3d.depth_buffer)!=0)\n'
        '            {d2_hud_start=s_d3d.draw_count;d2_hud_boundary_reason="capture-prepare-failed";}\n'
        '        if(d2_posthud_enabled() && d2_posthud_supported(d2_hud_start,s_d3d.draw_count,&d2_post_reason)){\n'
        '            if(d2_posthud_prepare(s_d3d.device,s_d3d.cmd_queue,s_d3d.render_targets[fi],s_d3d.depth_buffer,d2_graphics_output(fi))==0){\n'
        '                d2_post_ready=1;d2_post_reason="snapshot-not-reached";\n'
        '            }else d2_post_reason="resource-prepare-failed";\n'
        '        }\n'
        '    }\n    d2_vertplus_prepare(s_d3d.draw_count);\n    d2_scenery_prepare();\n    d2_depth_timeline_prepare();\n\n    /* Transition render target to RENDER_TARGET state */')
    text = replace(text, '''    s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(
        s_d3d.cmd_list, rtv_handle, s_d3d.clear_color, 0, NULL);''',
        '''    if (d2_graphics_vertplus_enabled() && !d2_vp_frame_has_3d) {
        static const float d2_blk[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        D3D12_RECT d2_vpr = {0};
        d2_vertplus_aspect_rect((float)s_d3d.width, (float)s_d3d.height, (float)d2_graphics_guest_width(), (float)d2_graphics_guest_height(), &d2_vpr);
        s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rtv_handle, d2_blk, 0, NULL);
        s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rtv_handle, s_d3d.clear_color, 1, &d2_vpr);
    } else {
        s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(
            s_d3d.cmd_list, rtv_handle, s_d3d.clear_color, 0, NULL);
    }''')
    text = replace(text, '''                      if (cd && cur_rt >= 0) {
                          const float mag[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rh, mag, 0, NULL);
                      } else
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rh, dr->cc, 0, NULL); }''',
        '''                      if (cd && cur_rt >= 0) {
                          const float mag[4] = {1.0f, 0.0f, 1.0f, 1.0f};
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rh, mag, 0, NULL);
                      } else if (cur_rt < 0 && d2_graphics_vertplus_enabled() && !d2_vp_frame_has_3d) {
                          D3D12_RECT d2_vpr = {0};
                          d2_vertplus_aspect_rect((float)s_d3d.width, (float)s_d3d.height, (float)d2_graphics_guest_width(), (float)d2_graphics_guest_height(), &d2_vpr);
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rh, dr->cc, 1, &d2_vpr);
                      } else
                          s_d3d.cmd_list->lpVtbl->ClearRenderTargetView(s_d3d.cmd_list, rh, dr->cc, 0, NULL); }''')
    text = replace(text, '''            for (u32 d = 0; d < s_d3d.draw_count && d < MAX_DRAWS; d++) {
                const D3D12DrawRecord* dr = &s_d3d.draws[d];
                if (!dr->is_vp) continue;''', '''            for (u32 d = 0; d < s_d3d.draw_count && d < MAX_DRAWS; d++) {
                const D3D12DrawRecord* dr = &s_d3d.draws[d];
                d2_depth_timeline_before_draw(d);
                if(d2_capture_requested && d==d2_hud_start)
                    d2_hud_snapshotted=d2_hud_snapshot(s_d3d.cmd_list,s_d3d.render_targets[fi],s_d3d.depth_buffer)==0;
                if(d2_post_ready && d==d2_hud_start){
                    d2_post_snapshotted=d2_posthud_snapshot(fi,s_d3d.cmd_list,s_d3d.render_targets[fi],s_d3d.depth_buffer)==0;
                    d2_post_reason=d2_post_snapshotted?"separate":"snapshot-failed";
                }
                if (!dr->is_vp) continue;''')
    text = replace(text, '''                s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list,
                    dr->vertex_count, 1, dr->vb_byte_offset / 256, 0);''',
        '''                if(d2_capture_requested && d>=d2_hud_start) {
                    s_d3d.draws[d].d2_hud_pso=dpso?dpso:vpso;
                    s_d3d.draws[d].d2_hud_pso->lpVtbl->AddRef(s_d3d.draws[d].d2_hud_pso);
                }
                if(d2_post_ready && d>=d2_hud_start)d2_posthud_keep(fi,d-d2_hud_start,dpso?dpso:vpso);
                if (cur_rt >= 0 || !d2_vertplus_draw_hud_split(s_d3d.cmd_list, d, dr, (u32)s_d3d.vp_parity,
                        (float)s_d3d.width, (float)s_d3d.height,
                        (float)d2_graphics_guest_width(), (float)d2_graphics_guest_height()))
                    s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list,
                        dr->vertex_count, 1, dr->vb_byte_offset / 256, 0);
                d2_depth_timeline_after_draw(d);''')
    text = replace(text, '''        const rsx_vertex_attrib* a=&state->vertex_attribs[0];
        u32 n=a->size?a->size:4; if(n>4) n=4;
        rsx_vtx_pos_dbg(state,cpu[0].v,n);
        memcpy(out16,cpu,sizeof(cpu));
        return;''', '''        static int d2_vtx_pos_cap = -1;
        if (d2_vtx_pos_cap < 0) { const char* _e = getenv("VTX_POS"); d2_vtx_pos_cap = _e ? atoi(_e) : 0; }
        if (__builtin_expect(d2_vtx_pos_cap > 0, 0)) {
            const rsx_vertex_attrib* a=&state->vertex_attribs[0];
            u32 n=a->size?a->size:4; if(n>4) n=4;
            rsx_vtx_pos_dbg(state,cpu[0].v,n);
        }
        memcpy(out16,cpu,sizeof(cpu));
        return;''')
    text = replace(text, 'typedef struct { float v[4]; } VPSlot;',
        'typedef struct { float v[4]; } VPSlot;\n'
        'static inline void d2_vp_shadow_pos(const void* out16, const float pos[4]) {\n'
        '    if (!s_d3d.vp_vb_mapped) return;\n'
        '    uintptr_t base = (uintptr_t)s_d3d.vp_vb_mapped;\n'
        '    uintptr_t p = (uintptr_t)out16;\n'
        '    if (p >= base) {\n'
        '        size_t off = (size_t)(p - base);\n'
        '        if (off < (size_t)(2u * MAX_VERTICES) * 256u)\n'
        '            memcpy(d2_vp_pos_shadow[off >> 8], pos, 16);\n'
        '    }\n'
        '}')
    text = replace(text, '''        memcpy(out16,cpu,sizeof(cpu));
        return;
    }
    for (int i = 0; i < 16; i++) {
        rsx_fetch_attrib(state, i, vi, out16[i].v);
        if (i == 0) {
            const rsx_vertex_attrib* a = &state->vertex_attribs[0];
            u32 n = a->size ? a->size : 4; if (n > 4) n = 4;
            rsx_vtx_pos_dbg(state, out16[0].v, n);
        }
    }''', '''        memcpy(out16,cpu,sizeof(cpu));
        d2_vp_shadow_pos(out16, cpu[0].v);
        return;
    }
    for (int i = 0; i < 16; i++) {
        rsx_fetch_attrib(state, i, vi, out16[i].v);
        if (i == 0) {
            d2_vp_shadow_pos(out16, out16[0].v);
            const rsx_vertex_attrib* a = &state->vertex_attribs[0];
            u32 n = a->size ? a->size : 4; if (n > 4) n = 4;
            rsx_vtx_pos_dbg(state, out16[0].v, n);
        }
    }''')
    text = replace(text, '''        static const int idx[6] = {0,1,2, 0,2,3};
        for (int t = 0; t < 6; t++) { memcpy(&out[o*16], c[idx[t]], sizeof(c[0])); o++; }
        if (d2_cached_vtx_dump()) { static int _n=0; if (_n++ < 4) {''', '''        static const int idx[6] = {0,1,2, 0,2,3};
        for (int t = 0; t < 6; t++) { memcpy(&out[o*16], c[idx[t]], sizeof(c[0])); d2_vp_shadow_pos(&out[o*16], c[idx[t]][0].v); o++; }
        if (d2_cached_vtx_dump()) { static int _n=0; if (_n++ < 4) {''')
    text = replace(text, '''        static const int idx[6] = {0,1,2, 0,2,3};
        for (int t = 0; t < 6; t++) { memcpy(&out[o*16], c[idx[t]], sizeof(c[0])); o++; }
        if (d2_cached_vtx_dump()) { static int _n=0; if (_n++ < 6) {''', '''        static const int idx[6] = {0,1,2, 0,2,3};
        for (int t = 0; t < 6; t++) { memcpy(&out[o*16], c[idx[t]], sizeof(c[0])); d2_vp_shadow_pos(&out[o*16], c[idx[t]][0].v); o++; }
        if (d2_cached_vtx_dump()) { static int _n=0; if (_n++ < 6) {''')
    text = replace(text, '''    u32 o = 0;
    for (u32 q = 0; q < quads; q++) {
        VPSlot c[4][16];
        for (u32 k = 0; k < 4; k++)
            read_vp_vertex(state, first + q*4 + k, c[k]);''', '''    u32 o = 0;
    double _d2s = d2_sample_begin(5);
    if (!d2_cached_vtx_dump() && d2_vertex_context_upload_quads(
            state, first, quads, out,
            &d2_vp_pos_shadow[(size_t)(u32)s_d3d.vp_parity * MAX_VERTICES + (s_d3d.vp_vb_offset >> 8)])) {
        o = quads * 6u;
        d2_sample_end(5, _d2s);
    } else { d2_sample_end(5, _d2s); for (u32 q = 0; q < quads; q++) {
        VPSlot c[4][16];
        for (u32 k = 0; k < 4; k++)
            read_vp_vertex(state, first + q*4 + k, c[k]);''')
    text = replace(text, '''    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

/* Straight triangle-list upload through the VP path''', '''    } s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

/* Straight triangle-list upload through the VP path''')
    text = replace(text, '''    { double _tv = perf_on() ? perf_now() : 0.0;
      for (u32 k = 0; k < count; k++)
          read_vp_vertex(state, first + k, &out[k*16]);
      if (perf_on()) { s_perf_vtx += perf_now() - _tv; s_perf_nverts += count; } }''', '''    { double _tv = perf_on() ? perf_now() : 0.0;
      double _d2s = d2_sample_begin(5);
      if (!d2_vertex_context_upload_tris(
              state, first, count, out,
              &d2_vp_pos_shadow[(size_t)(u32)s_d3d.vp_parity * MAX_VERTICES + (s_d3d.vp_vb_offset >> 8)])) {
          for (u32 k = 0; k < count; k++)
              read_vp_vertex(state, first + k, &out[k*16]);
      }
      d2_sample_end(5, _d2s);
      if (perf_on()) { s_perf_vtx += perf_now() - _tv; s_perf_nverts += count; } }''')
    text = replace(text, '''    u32 o = 0;
    for (u32 t = 0; t < tris; t++) {
        u32 i0 = fan ? 0 : t;
        /* strip winding alternates per triangle to keep facing consistent */
        u32 i1 = t + 1, i2 = t + 2;
        if (!fan && (t & 1)) { u32 tmp = i1; i1 = i2; i2 = tmp; }
        read_vp_vertex(state, first + i0, &out[o*16]); o++;
        read_vp_vertex(state, first + i1, &out[o*16]); o++;
        read_vp_vertex(state, first + i2, &out[o*16]); o++;
    }''', '''    u32 o = 0;
    double _d2s = d2_sample_begin(5);
    if (d2_vertex_context_upload_strip(
            state, first, tris, fan, out,
            &d2_vp_pos_shadow[(size_t)(u32)s_d3d.vp_parity * MAX_VERTICES + (s_d3d.vp_vb_offset >> 8)])) {
        o = tris * 3u;
    } else for (u32 t = 0; t < tris; t++) {
        u32 i0 = fan ? 0 : t;
        /* strip winding alternates per triangle to keep facing consistent */
        u32 i1 = t + 1, i2 = t + 2;
        if (!fan && (t & 1)) { u32 tmp = i1; i1 = i2; i2 = tmp; }
        read_vp_vertex(state, first + i0, &out[o*16]); o++;
        read_vp_vertex(state, first + i1, &out[o*16]); o++;
        read_vp_vertex(state, first + i2, &out[o*16]); o++;
    }
    d2_sample_end(5, _d2s);''')
    text = replace(text, '''    u32 o = 0;
    for (u32 q = 0; q < quads; q++) {
        VPSlot c[4][16];
        for (u32 k = 0; k < 4; k++)
            read_vp_vertex(state, read_guest_index(state, first + q*4 + k), c[k]);''', '''    u32 o = 0;
    double _d2s = d2_sample_begin(5);
    if (!d2_cached_vtx_dump() && d2_vertex_context_upload_quads_indexed(
            state, first, quads, out,
            &d2_vp_pos_shadow[(size_t)(u32)s_d3d.vp_parity * MAX_VERTICES + (s_d3d.vp_vb_offset >> 8)])) {
        o = quads * 6u;
        d2_sample_end(5, _d2s);
    } else { d2_sample_end(5, _d2s); for (u32 q = 0; q < quads; q++) {
        VPSlot c[4][16];
        for (u32 k = 0; k < 4; k++)
            read_vp_vertex(state, read_guest_index(state, first + q*4 + k), c[k]);''')
    text = replace(text, '''    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

static u32 upload_tris_vp_indexed''', '''    } s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}

static u32 upload_tris_vp_indexed''')
    text = replace(text, '''    { double _tv = perf_on() ? perf_now() : 0.0;
      for (u32 k = 0; k < count; k++)
          read_vp_vertex(state, read_guest_index(state, first + k), &out[k*16]);
      if (perf_on()) { s_perf_vtx += perf_now() - _tv; s_perf_nverts += count; } }''', '''    { double _tv = perf_on() ? perf_now() : 0.0;
      double _d2s = d2_sample_begin(5);
      if (!d2_vertex_context_upload_tris_indexed(
              state, first, count, out,
              &d2_vp_pos_shadow[(size_t)(u32)s_d3d.vp_parity * MAX_VERTICES + (s_d3d.vp_vb_offset >> 8)])) {
          for (u32 k = 0; k < count; k++)
              read_vp_vertex(state, read_guest_index(state, first + k), &out[k*16]);
      }
      d2_sample_end(5, _d2s);
      if (perf_on()) { s_perf_vtx += perf_now() - _tv; s_perf_nverts += count; } }''')
    text = replace(text, '''    u32 o = 0;
    for (u32 t = 0; t < tris; t++) {
        u32 i0 = fan ? 0 : t;
        read_vp_vertex(state, read_guest_index(state, first + i0),    &out[o*16]); o++;
        read_vp_vertex(state, read_guest_index(state, first + t + 1), &out[o*16]); o++;
        read_vp_vertex(state, read_guest_index(state, first + t + 2), &out[o*16]); o++;
    }''', '''    u32 o = 0;
    double _d2s = d2_sample_begin(5);
    if (d2_vertex_context_upload_strip_indexed(
            state, first, tris, fan, out,
            &d2_vp_pos_shadow[(size_t)(u32)s_d3d.vp_parity * MAX_VERTICES + (s_d3d.vp_vb_offset >> 8)])) {
        o = tris * 3u;
    } else for (u32 t = 0; t < tris; t++) {
        u32 i0 = fan ? 0 : t;
        read_vp_vertex(state, read_guest_index(state, first + i0),    &out[o*16]); o++;
        read_vp_vertex(state, read_guest_index(state, first + t + 1), &out[o*16]); o++;
        read_vp_vertex(state, read_guest_index(state, first + t + 2), &out[o*16]); o++;
    }
    d2_sample_end(5, _d2s);''')
    text = replace(text, '    /* Close and execute */',
        '    d2_depth_timeline_end_commands();\n    /* Close and execute */')
    text = replace(text, '''        u32 index;
        if(s_present_this_frame && s_dbg_last_draws>0 && d2_capture_f8_next(&index))
            rsx_capture_frame(fi,s_dbg_last_draws,index);''',
        '''        if(s_present_this_frame) {
            d2_temporal_observe(s_dbg_last_draws,d2_hud_start);
            d2_temporal_reference(fi);
        }
        if(d2_capture_requested) {
            rsx_capture_frame(fi,s_dbg_last_draws,d2_capture_index);
            d2_temporal_dump(d2_capture_index);
            d2_hud_dump(fi,s_dbg_last_draws,d2_hud_start,d2_capture_index,d2_hud_snapshotted);
        }''')
    text = replace(text, '    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, res, &sv, h);',
        '    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, res, &sv, h);\n    d2_draw_srv(heap_slot,res,&sv);')
    text = replace(text, '    pb->lpVtbl->Release(pb);\n    if (FAILED(hr)) {',
        '    if(SUCCEEDED(hr) && pso)d2_draw_pipeline(pso,&pd,hlsl);\n    pb->lpVtbl->Release(pb);\n    if (FAILED(hr)) {')
    text = replace(text, '    int slot;\n    if (s_d3d.vp_vs_n < VP_VS_CACHE)',
        '    d2_draw_vertex_source(vb->lpVtbl->GetBufferPointer(vb),vb->lpVtbl->GetBufferSize(vb),hlsl);\n    int slot;\n    if (s_d3d.vp_vs_n < VP_VS_CACHE)')
    text = text.replace('    d2_posthud_frame(fi);', '    d2_posthud_frame(fi);\n    d2_draw_frame_begin();\n    d2_draw_release();')
    text = text.replace('#include "temporal_adapter.inc"', '#include "temporal_adapter.inc"\n#include "draw_capture_adapter.inc"')
    text = text.replace('                d2_alpha_record_pso(d,dpso);',
        '                d2_alpha_record_pso(d,dpso);\n                if(d2_capture_requested)d2_draw_keep(d,dpso);')
    text = text.replace('            rsx_capture_frame(fi,s_dbg_last_draws,d2_capture_index);',
        '            if(d2_draw_capture_enabled()){d2_draw_dump(d2_capture_index,s_dbg_last_draws);}else{\n            rsx_capture_frame(fi,s_dbg_last_draws,d2_capture_index);')
    text = text.replace('            d2_hud_dump(fi,s_dbg_last_draws,d2_hud_start,d2_capture_index,d2_hud_snapshotted);',
        '            d2_hud_dump(fi,s_dbg_last_draws,d2_hud_start,d2_capture_index,d2_hud_snapshotted);}')
    text = text.replace('d2_capture_requested && d2_hud_start<', 'd2_capture_requested && !d2_draw_capture_enabled() && d2_hud_start<')
    # Export only the final, successfully compiled HLSL, on cache misses.
    # Prefix capture.h already declares this opt-in observer for the C backend.
    text = replace(text, '    int slot;\n    if (s_d3d.vp_vs_n < VP_VS_CACHE)',
        '    if(d2_capture_shader_enabled()) d2_capture_shader_program("vp",hlsl,strlen(hlsl),vuc,16u*rsx_vp_program_size_instrs(vuc,vlen),vstart/16u,0,0);\n    int slot;\n    if (s_d3d.vp_vs_n < VP_VS_CACHE)')
    text = replace(text, '    if (e) e->lpVtbl->Release(e);\n\n    D3D12_INPUT_ELEMENT_DESC il[16];',
        '    if (e) e->lpVtbl->Release(e);\n    if(d2_capture_shader_enabled()) d2_capture_shader_program("fp",hlsl,strlen(hlsl),vm_base+off,rsx_fp_program_size(vm_base+off,4096),0,RSX_FP_CTRL_AUTO,cube_mask);\n\n    D3D12_INPUT_ELEMENT_DESC il[16];')
    text = replace(text, '''    const char* sample="rsx_tex[1].Sample(rsx_samp[1], ";
    const char* anchor="struct PSOutput {";
    const char* function=
        "float4 d2_palette_lookup(float2 uv) {\\n"
        "    uint w, h; rsx_tex[1].GetDimensions(w, h);\\n"
        "    int2 size = int2(w, h);\\n"
        "    int2 p = clamp(int2(floor(uv * float2(w, h))), int2(0, 0), size - int2(1, 1));\\n"
        "    return rsx_tex[1].Load(int3(p, 0));\\n"
        "}\\n";
    unsigned count=0; const char* at=hlsl;
    while((at=strstr(at,sample))!=NULL) { count++; at+=strlen(sample); }
    if(count!=4 || !strstr(hlsl,anchor) || strlen(hlsl)+strlen(function)+1>cap) return -1;
    /* Both replacements shrink or preserve length, so the preflight bound
     * guarantees every splice fits and the helper precedes its callers. */
    hlsl_replace_all(hlsl,cap,sample,"d2_palette_lookup(");
    char replacement[512];
    snprintf(replacement,sizeof(replacement),"%s%s",function,anchor);
    hlsl_replace_all(hlsl,cap,anchor,replacement);
    return 1;''', '''    const char* sample="rsx_tex[1].Sample(rsx_samp[1], ";
    const char* point_sample="rsx_tex[1].Sample(rsx_samp[0], ";
    unsigned count=0; const char* at=hlsl;
    while((at=strstr(at,sample))!=NULL) { count++; at+=strlen(sample); }
    if(count!=4) return -1;
    /* Use static sampler s0 (POINT + CLAMP) instead of GetDimensions/Load on
     * rsx_tex[16], which Wine builtin d3dcompiler_47 (vkd3d-shader) miscompiles. */
    hlsl_replace_all(hlsl,cap,sample,point_sample);
    return 1;''')
    text = replace(text, '#include "rsx_fp_decompiler.h"',
        '#include "rsx_fp_decompiler.h"\n'
        '#include "d2_rsx_cache_index.h"\n'
        'static D2FpCacheIndex s_d2_fp_idx;\n'
        'static D2TexCacheIndex s_d2_tex_idx;\n'
        'static u64 s_d2_render_epoch = 0;\n'
        'static int s_d2_prepass_pso_safe = 0;\n'
        'static u64 s_d2_prepass_evict_epoch = 0;\n'
        'static u32 s_d2_b8_res_w = 0, s_d2_b8_res_h = 0;\n'
        'static u64 s_d2_b8_up_bytes = 0;\n'
        'static int s_d2_b8_in_psr = 0;\n'
        'typedef struct {\n'
        '    u32 offset, format, control1, control3, image_rect;\n'
        '    int valid, b8_state;\n'
        '} D2BoundTexCache;\n'
        'static D2BoundTexCache s_d2_tex_state_cache[4];')
    text = replace(text, '''    u32 uhash;
    {
        u32 usz = rsx_fp_program_size(vm_base + off, 4096);
        if (usz == 0) usz = 64;
        s_perf_pso_hashbytes += (int)usz;
        if (d2_cached_fp_raw_constants()) {
            uhash = 2166136261u;
            const u8* up = vm_base + off;
            for (u32 i = 0; i < usz; i++) { uhash ^= up[i]; uhash *= 16777619u; }
        } else {
            uhash = rsx_fp_code_hash(vm_base + off, 4096);
        }
    }

    s_perf_pso_calls++;
    for (int i = 0; i < s_d3d.vp_fp_n; i++)
        if (s_d3d.vp_fp[i].fp_addr != fp_addr) continue;   /* cheap reject first */
        else if (s_d3d.vp_fp[i].vs_idx == vs_idx &&
            s_d3d.vp_fp[i].vs_hash == vs_hash && s_d3d.vp_fp[i].gen == s_d3d.vp_gen &&
            s_d3d.vp_fp[i].blend == blend && s_d3d.vp_fp[i].nrt == nrt &&
            s_d3d.vp_fp[i].rtfmt == (u32)rtfmt && s_d3d.vp_fp[i].exp32 == exp32 &&
            s_d3d.vp_fp[i].ucode_hash == uhash && s_d3d.vp_fp[i].cmask == cmask &&
            s_d3d.vp_fp[i].cull == cull &&
            s_d3d.vp_fp[i].cube_mask == cube_mask &&
            d2_zs_equal(s_d3d.vp_fp[i].d2_zs, d2_zs))
            return s_d3d.vp_fp[i].pso;''',
        '''    u32 uhash;
    {
        u32 usz = 0;
        uhash = d2_fp_hash_memoized(vm_base + off, 4096u, d2_cached_fp_raw_constants(), &usz);
        s_perf_pso_hashbytes += (int)usz;
    }

    s_perf_pso_calls++;
    if (s_d3d.vp_fp_n == 0 && s_d2_fp_idx.count != 0) d2_fp_index_reset(&s_d2_fp_idx);
    const D2FpPsoKey d2_fp_key = d2_fp_make_key(
        fp_addr, vs_idx, vs_hash, s_d3d.vp_gen, blend, nrt, (u32)rtfmt,
        exp32, uhash, cmask, cull, cube_mask,
        d2_zs.depth, d2_zs.stencil, d2_zs.front, d2_zs.back);
    {
        int d2_fp_hit = d2_fp_index_find(&s_d2_fp_idx, &d2_fp_key);
        if (d2_fp_hit >= 0 && d2_fp_hit < s_d3d.vp_fp_n)
            return s_d3d.vp_fp[d2_fp_hit].pso;
    }''')
    text = replace(text, '''    /* insert (evict oldest when full) */
    if (s_d3d.vp_fp_n >= VP_FP_CACHE) {
        if (s_d3d.vp_fp[0].pso) s_d3d.vp_fp[0].pso->lpVtbl->Release(s_d3d.vp_fp[0].pso);
        memmove(&s_d3d.vp_fp[0], &s_d3d.vp_fp[1], sizeof(VPFPEntry) * (VP_FP_CACHE - 1));
        s_d3d.vp_fp_n = VP_FP_CACHE - 1;
    }
    s_d3d.vp_fp[s_d3d.vp_fp_n].fp_addr = fp_addr;
    s_d3d.vp_fp[s_d3d.vp_fp_n].vs_idx  = vs_idx;
    s_d3d.vp_fp[s_d3d.vp_fp_n].vs_hash = vs_hash;
    s_d3d.vp_fp[s_d3d.vp_fp_n].gen     = s_d3d.vp_gen;
    s_d3d.vp_fp[s_d3d.vp_fp_n].blend   = blend;
    s_d3d.vp_fp[s_d3d.vp_fp_n].nrt     = nrt;
    s_d3d.vp_fp[s_d3d.vp_fp_n].rtfmt   = (u32)rtfmt;
    s_d3d.vp_fp[s_d3d.vp_fp_n].exp32   = exp32;
    s_d3d.vp_fp[s_d3d.vp_fp_n].ucode_hash = uhash;
    s_d3d.vp_fp[s_d3d.vp_fp_n].cmask   = cmask;
    s_d3d.vp_fp[s_d3d.vp_fp_n].cull    = cull;
    s_d3d.vp_fp[s_d3d.vp_fp_n].cube_mask = cube_mask;
    s_d3d.vp_fp[s_d3d.vp_fp_n].d2_zs = d2_zs;
    s_d3d.vp_fp[s_d3d.vp_fp_n].pso     = pso;
    s_d3d.vp_fp_n++;''',
        '''    /* insert (evict LRU when full via O(1) index) */
    int d2_fp_evicted = 0;
    int d2_fp_slot = d2_fp_index_insert(&s_d2_fp_idx, &d2_fp_key, &d2_fp_evicted);
    if (d2_fp_evicted && s_d3d.vp_fp[d2_fp_slot].pso) {
        s_d3d.vp_fp[d2_fp_slot].pso->lpVtbl->Release(s_d3d.vp_fp[d2_fp_slot].pso);
        s_d3d.vp_fp[d2_fp_slot].pso = NULL;
    }
    s_d3d.vp_fp[d2_fp_slot].fp_addr = fp_addr;
    s_d3d.vp_fp[d2_fp_slot].vs_idx  = vs_idx;
    s_d3d.vp_fp[d2_fp_slot].vs_hash = vs_hash;
    s_d3d.vp_fp[d2_fp_slot].gen     = s_d3d.vp_gen;
    s_d3d.vp_fp[d2_fp_slot].blend   = blend;
    s_d3d.vp_fp[d2_fp_slot].nrt     = nrt;
    s_d3d.vp_fp[d2_fp_slot].rtfmt   = (u32)rtfmt;
    s_d3d.vp_fp[d2_fp_slot].exp32   = exp32;
    s_d3d.vp_fp[d2_fp_slot].ucode_hash = uhash;
    s_d3d.vp_fp[d2_fp_slot].cmask   = cmask;
    s_d3d.vp_fp[d2_fp_slot].cull    = cull;
    s_d3d.vp_fp[d2_fp_slot].cube_mask = cube_mask;
    s_d3d.vp_fp[d2_fp_slot].d2_zs = d2_zs;
    s_d3d.vp_fp[d2_fp_slot].pso     = pso;
    s_d3d.vp_fp_n = s_d2_fp_idx.count;''')
    text = replace(text, '''    int slot = -1, freeslot = -1;
    for (int i = 0; i < VP_TEX_SLOTS; i++) {
        VPTexSlot* c = &s_d3d.vp_tex[i];
        if (c->res && c->key == key_off && c->w == w && c->h == h && c->fmt == fmt
            && c->cube == cube && c->src_pitch == src_pitch) {
            if (c->used) return i;                /* already bound this frame */
            { static int nocache = -1;            /* TEX_NOCACHE=1: always re-upload */
              if (nocache < 0) { const char* e = getenv("TEX_NOCACHE");
                                 nocache = e ? atoi(e) : 0; }
              if (nocache) { slot = i; break; } }
            /* Held from an earlier frame: re-upload only if the guest bytes
             * changed. Most of this scene's textures are static, and converting
             * every one of them every frame was ~70% of the frame's CPU time. */
            u32 nb = tl.face_bytes;
            u32 cs = TEX_CSUM(vm_base + c->off, nb);
            if (cs == c->csum) { c->used = 1; return i; }
            slot = i; break;                      /* stale: fall through and redo */
        }
        if (!c->used && freeslot < 0) freeslot = i;
    }''',
        '''    if (!s_d2_tex_idx.initialized) d2_tex_index_reset(&s_d2_tex_idx);
    const D2TexKey d2_tkey = d2_tex_make_key(key_off, w, h, fmt, src_pitch, cube);
    int slot = -1, freeslot = -1;
    u32 d2_pre_off = 0, d2_pre_csum = 0;
    int d2_have_csum = 0;
    {
        int i = d2_tex_index_find(&s_d2_tex_idx, &d2_tkey);
        if (i >= 0) {
            VPTexSlot* c = &s_d3d.vp_tex[i];
            if (c->res) {
                if (c->used) {
                    d2_tex_index_mark_used(&s_d2_tex_idx, i);
                    return i;                /* already bound this frame */
                }
                { static int nocache = -1;            /* TEX_NOCACHE=1: always re-upload */
                  if (nocache < 0) { const char* e = getenv("TEX_NOCACHE");
                                     nocache = e ? atoi(e) : 0; }
                  if (nocache) { slot = i; }
                  else {
                      /* Held from an earlier frame: re-upload only if the guest bytes
                       * changed. Most of this scene's textures are static, and converting
                       * every one of them every frame was ~70% of the frame's CPU time. */
                      u32 nb = tl.face_bytes;
                      u32 cs = TEX_CSUM(vm_base + c->off, nb);
                      if (cs == c->csum) {
                          c->used = 1;
                          d2_tex_index_mark_used(&s_d2_tex_idx, i);
                          return i;
                      }
                      d2_pre_off = c->off;
                      d2_pre_csum = cs;
                      d2_have_csum = 1;
                      slot = i;                      /* stale: fall through and redo */
                  } }
            } else {
                d2_tex_index_remove_slot(&s_d2_tex_idx, i);
            }
        }
        if (slot < 0) freeslot = d2_tex_index_pick_free(&s_d2_tex_idx);
    }''')
    text = replace(text, '''    if (t->res && (t->w != w || t->h != h || t->fmt != fmt || t->cube != cube)) {
        t->res->lpVtbl->Release(t->res); t->res = NULL;
        if (t->up) { t->up->lpVtbl->Release(t->up); t->up = NULL; }
    }''',
        '''    if (t->res && (t->w != w || t->h != h || t->fmt != fmt || t->cube != cube)) {
        d2_tex_index_remove_slot(&s_d2_tex_idx, slot);
        t->res->lpVtbl->Release(t->res); t->res = NULL;
        if (t->up) { t->up->lpVtbl->Release(t->up); t->up = NULL; }
    }''')
    text = replace(text, '''    t->src_pitch = src_pitch;
    t->off = off; t->key = key_off; t->w = w; t->h = h; t->fmt = fmt;
    t->cube = cube; t->used = 1;''',
        '''    t->src_pitch = src_pitch;
    t->off = off; t->key = key_off; t->w = w; t->h = h; t->fmt = fmt;
    t->cube = cube; t->used = 1;
    d2_tex_index_set_slot(&s_d2_tex_idx, slot, &d2_tkey);
    d2_tex_index_mark_used(&s_d2_tex_idx, slot);''')
    text = replace(text, '''    u32 _face_bytes = tl.face_bytes;
    if (cube) {
        u32 total = 0;
        for (u32 lw = w, lh = h, l = 0; l < (mips ? mips : 1u); l++) {
            total += dxt ? (((lw + 3) / 4) * (bpp == 8 ? 16u : 8u) * ((lh + 3) / 4))
                         : (lw * lh * bpp);
            if (lw > 1) lw >>= 1;
            if (lh > 1) lh >>= 1;
        }
        _face_bytes = rsx_texture_cube_face_stride(fmt, w, h, mips, src_pitch);
    }''',
        '''    u32 _face_bytes = tl.face_bytes;
    if (cube) {
        _face_bytes = rsx_texture_cube_face_stride(fmt, w, h, mips, src_pitch);
    }''')
    text = replace(text, '''    { u32 nb = tl.face_bytes;
      t->csum = TEX_CSUM(vm_base + off, nb); }''',
        '''    { u32 nb = tl.face_bytes;
      t->csum = (d2_have_csum && off == d2_pre_off) ? d2_pre_csum : TEX_CSUM(vm_base + off, nb); }''')
    text = replace(text, '''    if (s_d3d.tex_bound && (!s_d3d.tex_ready || s_d3d.tex_dirty) && s_d3d.srv_heap && s_d3d.pipeline_state_tex) {
        extern uint8_t* vm_base;
        u32 w = s_d3d.tex_w, h = s_d3d.tex_h;
        u32 pitch = (w + 255) & ~255u;   /* D3D12 requires 256-byte row pitch */

        if (s_d3d.tex_resource) { s_d3d.tex_resource->lpVtbl->Release(s_d3d.tex_resource); s_d3d.tex_resource = NULL; }
        if (s_d3d.tex_upload)   { s_d3d.tex_upload->lpVtbl->Release(s_d3d.tex_upload);     s_d3d.tex_upload = NULL; }

        D3D12_HEAP_PROPERTIES hp_def = {0}; hp_def.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {0};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
        td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        HRESULT thr = s_d3d.device->lpVtbl->CreateCommittedResource(
            s_d3d.device, &hp_def, D3D12_HEAP_FLAG_NONE, &td,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            &IID_ID3D12Resource, (void**)&s_d3d.tex_resource);

        D3D12_HEAP_PROPERTIES hp_up = {0}; hp_up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ud = {0};
        ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ud.Width = (u64)pitch * h; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
        ud.SampleDesc.Count = 1; ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (SUCCEEDED(thr))
            thr = s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hp_up, D3D12_HEAP_FLAG_NONE, &ud,
                D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.tex_upload);

        if (SUCCEEDED(thr) && vm_base) {
            void* mapped = NULL;
            D3D12_RANGE nr = {0, 0};
            if (SUCCEEDED(s_d3d.tex_upload->lpVtbl->Map(s_d3d.tex_upload, 0, &nr, &mapped)) && mapped) {
                const u8* srcbase = vm_base + s_d3d.tex_src_offset;
                for (u32 y = 0; y < h; y++)
                    memcpy((u8*)mapped + (u64)y * pitch, srcbase + (u64)y * w, w);
                s_d3d.tex_upload->lpVtbl->Unmap(s_d3d.tex_upload, 0, NULL);

                D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
                dst.pResource = s_d3d.tex_resource;
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.SubresourceIndex = 0;
                src.pResource = s_d3d.tex_upload;
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset = 0;
                src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8_UNORM;
                src.PlacedFootprint.Footprint.Width    = w;
                src.PlacedFootprint.Footprint.Height   = h;
                src.PlacedFootprint.Footprint.Depth    = 1;
                src.PlacedFootprint.Footprint.RowPitch = pitch;
                s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);

                D3D12_RESOURCE_BARRIER tb = {0};
                tb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                tb.Transition.pResource   = s_d3d.tex_resource;
                tb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                tb.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                tb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &tb);

                D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
                sv.Format = DXGI_FORMAT_R8_UNORM;
                sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                sv.Texture2D.MipLevels = 1;
                D3D12_CPU_DESCRIPTOR_HANDLE sh;
                s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &sh);
                s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, s_d3d.tex_resource, &sv, sh);

                s_d3d.tex_ready = 1;
                s_d3d.tex_dirty = 0;   /* content now in sync with guest atlas */
                { static int _au = 0; if (_au++ < 3)
                    printf("[D3D12] atlas uploaded (%ux%u R8) -> textured\\n", w, h); }
            }
        }
    }''',
        '''    if (s_d3d.tex_bound && (!s_d3d.tex_ready || s_d3d.tex_dirty) && s_d3d.srv_heap && s_d3d.pipeline_state_tex) {
        extern uint8_t* vm_base;
        u32 w = s_d3d.tex_w, h = s_d3d.tex_h;
        u32 pitch = (w + 255) & ~255u;   /* D3D12 requires 256-byte row pitch */
        u64 need_up = (u64)pitch * h;
        HRESULT thr = S_OK;
        int srv_dirty = 0;

        if (!s_d3d.tex_resource || s_d2_b8_res_w != w || s_d2_b8_res_h != h) {
            if (s_d3d.tex_resource) { s_d3d.tex_resource->lpVtbl->Release(s_d3d.tex_resource); s_d3d.tex_resource = NULL; }
            s_d2_b8_res_w = s_d2_b8_res_h = 0;
            s_d2_b8_in_psr = 0;

            D3D12_HEAP_PROPERTIES hp_def = {0}; hp_def.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC td = {0};
            td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
            td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
            td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            thr = s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hp_def, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.tex_resource);
            if (SUCCEEDED(thr)) {
                s_d2_b8_res_w = w;
                s_d2_b8_res_h = h;
                srv_dirty = 1;
            }
        }

        if (SUCCEEDED(thr) && (!s_d3d.tex_upload || s_d2_b8_up_bytes < need_up)) {
            if (s_d3d.tex_upload) { s_d3d.tex_upload->lpVtbl->Release(s_d3d.tex_upload); s_d3d.tex_upload = NULL; }
            s_d2_b8_up_bytes = 0;

            D3D12_HEAP_PROPERTIES hp_up = {0}; hp_up.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC ud = {0};
            ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            ud.Width = need_up; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
            ud.SampleDesc.Count = 1; ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            thr = s_d3d.device->lpVtbl->CreateCommittedResource(
                s_d3d.device, &hp_up, D3D12_HEAP_FLAG_NONE, &ud,
                D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                &IID_ID3D12Resource, (void**)&s_d3d.tex_upload);
            if (SUCCEEDED(thr)) s_d2_b8_up_bytes = need_up;
        }

        if (SUCCEEDED(thr) && s_d3d.tex_resource && s_d3d.tex_upload && vm_base) {
            void* mapped = NULL;
            D3D12_RANGE nr = {0, 0};
            if (SUCCEEDED(s_d3d.tex_upload->lpVtbl->Map(s_d3d.tex_upload, 0, &nr, &mapped)) && mapped) {
                const u8* srcbase = vm_base + s_d3d.tex_src_offset;
                for (u32 y = 0; y < h; y++)
                    memcpy((u8*)mapped + (u64)y * pitch, srcbase + (u64)y * w, w);
                s_d3d.tex_upload->lpVtbl->Unmap(s_d3d.tex_upload, 0, NULL);

                if (s_d2_b8_in_psr) {
                    D3D12_RESOURCE_BARRIER pb = {0};
                    pb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    pb.Transition.pResource   = s_d3d.tex_resource;
                    pb.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    pb.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
                    pb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &pb);
                    s_d2_b8_in_psr = 0;
                }

                D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
                dst.pResource = s_d3d.tex_resource;
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.SubresourceIndex = 0;
                src.pResource = s_d3d.tex_upload;
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset = 0;
                src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8_UNORM;
                src.PlacedFootprint.Footprint.Width    = w;
                src.PlacedFootprint.Footprint.Height   = h;
                src.PlacedFootprint.Footprint.Depth    = 1;
                src.PlacedFootprint.Footprint.RowPitch = pitch;
                s_d3d.cmd_list->lpVtbl->CopyTextureRegion(s_d3d.cmd_list, &dst, 0, 0, 0, &src, NULL);

                D3D12_RESOURCE_BARRIER tb = {0};
                tb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                tb.Transition.pResource   = s_d3d.tex_resource;
                tb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                tb.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                tb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                s_d3d.cmd_list->lpVtbl->ResourceBarrier(s_d3d.cmd_list, 1, &tb);
                s_d2_b8_in_psr = 1;

                if (srv_dirty) {
                    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {0};
                    sv.Format = DXGI_FORMAT_R8_UNORM;
                    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    sv.Texture2D.MipLevels = 1;
                    D3D12_CPU_DESCRIPTOR_HANDLE sh;
                    s_d3d.srv_heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(s_d3d.srv_heap, &sh);
                    s_d3d.device->lpVtbl->CreateShaderResourceView(s_d3d.device, s_d3d.tex_resource, &sv, sh);
                }

                s_d3d.tex_ready = 1;
                s_d3d.tex_dirty = 0;   /* content now in sync with guest atlas */
                { static int _au = 0; if (_au++ < 3)
                    printf("[D3D12] atlas uploaded (%ux%u R8) -> textured\\n", w, h); }
            }
        }
    }''')
    text = replace(text, '''    for (int _i = 0; _i < VP_TEX_SLOTS; _i++) s_d3d.vp_tex[_i].used = 0;
    for (u32 _d = 0; _d < s_d3d.draw_count && _d < MAX_DRAWS; _d++) {
        D3D12DrawRecord* dr = &s_d3d.draws[_d];
        if (!dr->is_vp || dr->is_clear) continue;''',
        '''    for (int _i = 0; _i < VP_TEX_SLOTS; _i++) s_d3d.vp_tex[_i].used = 0;
    d2_tex_index_begin_frame(&s_d2_tex_idx);
    s_d2_render_epoch++;
    s_d2_prepass_pso_safe = 0;
    s_d2_prepass_evict_epoch = s_d2_fp_idx.evictions;
    for (u32 _d = 0; _d < s_d3d.draw_count && _d < MAX_DRAWS; _d++) {
        D3D12DrawRecord* dr = &s_d3d.draws[_d];
        dr->d2_cached_pso = NULL;
        if (!dr->is_vp || dr->is_clear) continue;''')
    text = replace(text, '''        double _ps0 = perf_on() ? perf_now() : 0.0;
        if (dr->fp_addr) vp_get_fp_pso(dr->vs_idx, dr->fp_addr, dr->blend_key,
                                       dr_num_rts(dr),
                                       dr->rt_off ? rsx_surface_dxgi(dr->rt_fmt)
                                                  : DXGI_FORMAT_R8G8B8A8_UNORM,
                                       dr->fp_exp32, dr->cmask, dr->cull, dr_cube_mask(dr), dr->d2_zs);
        if (perf_on()) s_perf_pso += perf_now() - _ps0;
    }''',
        '''        double _ps0 = perf_on() ? perf_now() : 0.0;
        dr->d2_cached_pso = dr->fp_addr ? vp_get_fp_pso(dr->vs_idx, dr->fp_addr, dr->blend_key,
                                       dr_num_rts(dr),
                                       dr->rt_off ? rsx_surface_dxgi(dr->rt_fmt)
                                                  : DXGI_FORMAT_R8G8B8A8_UNORM,
                                       dr->fp_exp32, dr->cmask, dr->cull, dr_cube_mask(dr), dr->d2_zs) : NULL;
        if (perf_on()) s_perf_pso += perf_now() - _ps0;
    }
    s_d2_prepass_pso_safe = (s_d2_fp_idx.evictions == s_d2_prepass_evict_epoch);''')
    text = replace(text, '''                ID3D12PipelineState* dpso =
                    dr->fp_addr ? vp_get_fp_pso(dr->vs_idx, dr->fp_addr, dr->blend_key,
                                                dr_num_rts(dr),
                                                dr->rt_off ? rsx_surface_dxgi(dr->rt_fmt)
                                                           : DXGI_FORMAT_R8G8B8A8_UNORM,
                                                dr->fp_exp32, dr->cmask, dr->cull,
                                                dr_cube_mask(dr), dr->d2_zs) : NULL;''',
        '''                ID3D12PipelineState* dpso =
                    (s_d2_prepass_pso_safe && s_d2_fp_idx.evictions == s_d2_prepass_evict_epoch)
                        ? dr->d2_cached_pso
                        : (dr->fp_addr ? vp_get_fp_pso(dr->vs_idx, dr->fp_addr, dr->blend_key,
                                                       dr_num_rts(dr),
                                                       dr->rt_off ? rsx_surface_dxgi(dr->rt_fmt)
                                                                  : DXGI_FORMAT_R8G8B8A8_UNORM,
                                                       dr->fp_exp32, dr->cmask, dr->cull,
                                                       dr_cube_mask(dr), dr->d2_zs) : NULL);''')
    text = replace(text, '''static void d2_refresh_textures(void)
{
    const rsx_state* st = s_d3d.current_rsx_state;
    if (!st) return;
    for (u32 u=0;u<4;u++)
        d3d12_bind_texture(NULL,u,&st->textures[u]);
}''',
        '''static void d2_refresh_textures(void)
{
    const rsx_state* st = s_d3d.current_rsx_state;
    if (!st) return;
    extern uint8_t* vm_base;
    static int diag_env = -1;
    if (diag_env < 0)
        diag_env = (getenv("TEX_PROBE") != NULL || getenv("MOVIE_BIND") != NULL) ? 1 : 0;
    if (!vm_base || diag_env) {
        for (u32 u = 0; u < 4; u++)
            d3d12_bind_texture(NULL, u, &st->textures[u]);
        return;
    }
    u32 dirty_mask = 0u;
    for (u32 u = 0; u < 4; u++) {
        const rsx_texture_state* t = &st->textures[u];
        const D2BoundTexCache* c = &s_d2_tex_state_cache[u];
        if (!c->valid || t->dirty ||
            c->offset != t->offset || c->format != t->format ||
            c->control1 != t->control1 || c->control3 != t->control3 ||
            c->image_rect != t->image_rect) {
            dirty_mask |= (1u << u);
        }
    }
    if (dirty_mask == 0u) {
        if (s_d3d.tex_bound && !s_d3d.tex_dirty) s_d3d.tex_dirty = 1;
        return;
    }
    for (u32 u = 0; u < 4; u++) {
        if (dirty_mask & (1u << u))
            d3d12_bind_texture(NULL, u, &st->textures[u]);
    }
    if ((dirty_mask & 0xFu) != 0xFu) {
        for (int u = 3; u >= 0; u--) {
            if (s_d2_tex_state_cache[u].valid && s_d2_tex_state_cache[u].b8_state >= 0) {
                s_d3d.tex_bound = s_d2_tex_state_cache[u].b8_state;
                break;
            }
        }
    }
}''')
    text = replace(text, '''    if (!vm_base || width == 0 || height == 0) return;''',
        '''    if (!vm_base) return;
    if (width == 0 || height == 0) {
        if (unit < 4) {
            s_d2_tex_state_cache[unit].offset = tex->offset;
            s_d2_tex_state_cache[unit].format = tex->format;
            s_d2_tex_state_cache[unit].control1 = tex->control1;
            s_d2_tex_state_cache[unit].control3 = tex->control3;
            s_d2_tex_state_cache[unit].image_rect = tex->image_rect;
            s_d2_tex_state_cache[unit].b8_state = -1;
            s_d2_tex_state_cache[unit].valid = 1;
        }
        return;
    }''')
    text = replace(text, '''    if (base_fmt == 0x81 /* B8 */) {
        s_d3d.tex_src_offset = cellGcmResolveLocated((tex->format & 3) == 1, offset);
        if (s_d3d.tex_w != width || s_d3d.tex_h != height) {
            /* dims changed -> resource must be (re)created in render_frame */
            s_d3d.tex_ready = 0;
        }
        s_d3d.tex_w     = width;
        s_d3d.tex_h     = height;
        s_d3d.tex_bound = 1;
        s_d3d.tex_dirty = 1;
    } else {
        s_d3d.tex_bound = 0;
    }''',
        '''    if (base_fmt == 0x81 /* B8 */) {
        s_d3d.tex_src_offset = cellGcmResolveLocated((tex->format & 3) == 1, offset);
        if (s_d3d.tex_w != width || s_d3d.tex_h != height) {
            /* dims changed -> resource must be (re)created in render_frame */
            s_d3d.tex_ready = 0;
        }
        s_d3d.tex_w     = width;
        s_d3d.tex_h     = height;
        s_d3d.tex_bound = 1;
        s_d3d.tex_dirty = 1;
    } else {
        s_d3d.tex_bound = 0;
    }
    if (unit < 4) {
        s_d2_tex_state_cache[unit].offset = tex->offset;
        s_d2_tex_state_cache[unit].format = tex->format;
        s_d2_tex_state_cache[unit].control1 = tex->control1;
        s_d2_tex_state_cache[unit].control3 = tex->control3;
        s_d2_tex_state_cache[unit].image_rect = tex->image_rect;
        s_d2_tex_state_cache[unit].b8_state = (base_fmt == 0x81) ? 1 : 0;
        s_d2_tex_state_cache[unit].valid = 1;
    }''')
    text = replace(text, '''int rsx_d3d12_backend_init(u32 width, u32 height, const char* title)
{
    memset(&s_d3d, 0, sizeof(s_d3d));''',
        '''static void d2_reset_all_caches(void)
{
    d2_fp_index_reset(&s_d2_fp_idx);
    d2_tex_index_reset(&s_d2_tex_idx);
    d2_fp_hash_memo_reset();
    d2_fp_const_cache_reset();
    d2_pair_cache_invalidate();
    d2_pair_discard_pending = 0;
    d2_pair_discarded_slot = 0;
    memset(s_d2_tex_state_cache, 0, sizeof(s_d2_tex_state_cache));
    s_d2_b8_res_w = s_d2_b8_res_h = 0;
    s_d2_b8_up_bytes = 0;
    s_d2_b8_in_psr = 0;
    s_d2_render_epoch++;
    s_d2_prepass_pso_safe = 0;
    s_d2_prepass_evict_epoch = 0;
}

int rsx_d3d12_backend_init(u32 width, u32 height, const char* title)
{
    memset(&s_d3d, 0, sizeof(s_d3d));
    d2_reset_all_caches();''')
    text = replace(text, '''    rsx_set_backend(NULL);
    s_d3d.initialized = 0;''',
        '''    rsx_set_backend(NULL);
    s_d3d.initialized = 0;
    d2_reset_all_caches();''')
    text = '#include "rsx_vp_decompiler.h"\n' + text
    def includes(match):
        path = source.parent / match[1]
        return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else match[0]
    text = re.sub(r'#\s*include\s*"([^"\n]+)"', includes, text)
    output = Path(output); output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('#include "draw_capture.h"\n#include "graphics.h"\n#include "capture.h"\n#include "hud_capture.h"\n#include "hud_present.h"\n'+text, encoding='utf-8', newline='\n')


if __name__ == '__main__': generate(sys.argv[1], sys.argv[2])
