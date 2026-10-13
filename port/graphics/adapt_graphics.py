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
    text = replace(text, '    s_d3d.width = width;\n    s_d3d.height = height;',
        '    if (d2_graphics_configure(width, height) != 0) return -1;\n'
        '    width = d2_graphics_render_width(); height = d2_graphics_render_height();\n'
        '    s_d3d.width = width;\n    s_d3d.height = height;')
    text = replace(text, 's_d3d.hwnd = create_window(width, height, title);',
        's_d3d.hwnd = create_window(d2_graphics_output_width(), d2_graphics_output_height(), title);')
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
        '        if (d2_graphics_present(fi, s_d3d.render_targets[fi]) != 0) {\n'
        '            s_d3d.window_closed = 1; return;\n        }\n'
        '        s_d3d.swap_chain->lpVtbl->Present(s_d3d.swap_chain, (UINT)d2_interval, 0);')
    text = replace(text, '    /* Release D3D12 resources */',
        '    d2_capture_shutdown();\n    d2_graphics_shutdown();\n    /* Release D3D12 resources */')
    text = replace(text,
        'd2_capture_f8_next(&index)', 'd2_capture_next(&index, s_d3d.hwnd)')
    text = replace(text, '    u32 d2_diag_valid;', '    u32 d2_capture_vs_hash;\n    u32 d2_diag_valid;')
    text = replace(text, '        r->d2_diag_valid = enabled ? 1u : 0u;',
        '        r->d2_capture_vs_hash = enabled && vs_idx >= 0 && vs_idx < s_d3d.vp_vs_n\n'
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
    capture = replace(capture, '    printf("[RSXCAP] frame %llu -> %s (%u ops)\\n",',
        '    d2_capture_finish(dir, s_d3d.frame_count, ndraws);\n'
        '    printf("[RSXCAP] frame %llu -> %s (%u ops)\\n",')
    text = text[:start] + capture + text[end:]
    def includes(match):
        path = source.parent / match[1]
        return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else match[0]
    text = re.sub(r'#\s*include\s*"([^"\n]+)"', includes, text)
    output = Path(output); output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('#include "graphics.h"\n#include "capture.h"\n'+text, encoding='utf-8', newline='\n')


if __name__ == '__main__': generate(sys.argv[1], sys.argv[2])
