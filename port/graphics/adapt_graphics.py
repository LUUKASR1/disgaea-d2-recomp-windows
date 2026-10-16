"""Adapt the audited Windows SDK renderer in a build copy; preserve the SDK lock."""
from pathlib import Path
import hashlib
import re
import sys
from adapt_frame_profile import generate as generate_frame_profile

EXPECTED = '5ae65fef33ca9089af988486deafd08e1664be659e42f95c566c6d6839585421'


def replace(text, old, new):
    if text.count(old) != 1: raise RuntimeError('Renderer anchor changed: ' + old[:70])
    return text.replace(old, new)


def generate(source, output):
    source = Path(source).resolve(); data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED: raise RuntimeError('Pinned renderer differs; SDK preserved')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    profile_header = output.parent / 'd2_frame_profile.h'
    generate_frame_profile(source.parent / 'd2_frame_light.h', profile_header)
    text = data.decode()
    text = replace(text, '#include "d2_frame_light.h"',
                   '#include "' + profile_header.resolve().as_posix() + '"')
    text = replace(text, '    wait_for_gpu_impl();',
        '    double d2_wait_start=d2_lite_enabled()?d2_lite_now():0;\n'
        '    wait_for_gpu_impl();\n'
        '    if(d2_wait_start>0){d2_profile_wait_ms+=(d2_lite_now()-d2_wait_start)*1000.0;d2_profile_wait_calls++;}')
    text = replace(text, '    if (perf_on()) s_perf_pre += perf_now() - _pre0;',
        '    if(d2_profile_prepare_start>0)d2_profile_prepare_ms+=(d2_lite_now()-d2_profile_prepare_start)*1000.0;\n'
        '    if (perf_on()) s_perf_pre += perf_now() - _pre0;')
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
                    if (dr->vp_w >= 2 && dr->vp_h >= 2 &&
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
        '        double d2_present_start=d2_lite_enabled()?d2_lite_now():0;\n'
        '        if(d2_display_present(s_d3d.swap_chain,(UINT)d2_interval)!=0){s_d3d.window_closed=1;return;}\n'
        '        if(d2_present_start>0)d2_profile_present_ms+=(d2_lite_now()-d2_present_start)*1000.0;')
    text = replace(text, '    /* Release D3D12 resources */',
        '    d2_posthud_shutdown();\n    d2_hud_shutdown();\n    d2_draw_release();\n    d2_draw_capture_close();\n    d2_capture_shutdown();\n    d2_graphics_shutdown();\n    /* Release D3D12 resources */')
    text = replace(text, '    u32 d2_diag_valid;',
        '    ID3D12PipelineState* d2_hud_pso;\n    u32 d2_capture_vs_hash;\n    u32 d2_diag_valid;')
    text = replace(text, '        r->d2_diag_valid = enabled ? 1u : 0u;',
        '        r->d2_capture_vs_hash = (enabled || d2_posthud_enabled()) && vs_idx >= 0 && vs_idx < s_d3d.vp_vs_n\n'
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
        '#include "hud_adapter.inc"\n#include "temporal_adapter.inc"\n\nstatic void render_frame(void)\n{')
    text = replace(text, '                s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list,\n                                                         dpso ? dpso : vpso);',
        '                s_d3d.cmd_list->lpVtbl->SetPipelineState(s_d3d.cmd_list,\n                                                         dpso ? dpso : vpso);\n                d2_alpha_record_pso(d,dpso);')
    text = replace(text, '    wait_for_gpu();\n\n    /* Compile the real vertex program once',
        '    wait_for_gpu();\n    d2_posthud_frame(fi);\n    double d2_profile_prepare_start=d2_lite_enabled()?d2_lite_now():0;\n\n    /* Compile the real vertex program once')
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
        '    }\n    d2_scenery_prepare();\n    d2_depth_timeline_prepare();\n\n    /* Transition render target to RENDER_TARGET state */')
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
                s_d3d.cmd_list->lpVtbl->DrawInstanced(s_d3d.cmd_list,
                    dr->vertex_count, 1, dr->vb_byte_offset / 256, 0);
                d2_depth_timeline_after_draw(d);''')
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
    text = '#include "rsx_vp_decompiler.h"\n' + text
    def includes(match):
        path = source.parent / match[1]
        return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else match[0]
    text = re.sub(r'#\s*include\s*"([^"\n]+)"', includes, text)
    output = Path(output); output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('#include "frame_profile_state.h"\n#include "draw_capture.h"\n#include "graphics.h"\n#include "capture.h"\n#include "hud_capture.h"\n#include "hud_present.h"\n'+text, encoding='utf-8', newline='\n')


if __name__ == '__main__': generate(sys.argv[1], sys.argv[2])
