"""Extend the pinned lightweight sampler in a build copy, never the SDK."""
from pathlib import Path
import hashlib

EXPECTED = '25327e7524f4e8885e22397dfe06e8c40c7a0431776d7373f8e3f8ffc2343bb7'


def replace(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Light profiler anchor changed: ' + old[:70])
    return text.replace(old, new)


def generate(source, destination):
    data = Path(source).read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise RuntimeError('Pinned lightweight sampler differs; SDK preserved')
    text = data.decode()
    text = replace(text, '    double interval_ms, render_ms, compile_ms, fifo_ms;',
                   '    double interval_ms, render_ms, compile_ms, fifo_ms;\n'
                   '    double wait_ms, prepare_ms, present_ms;\n    unsigned wait_calls;')
    text = replace(text, 'static double d2_lite_last_compile;',
                   'static double d2_lite_last_compile;\n'
                   'static double d2_last_wait_ms, d2_last_prepare_ms, d2_last_present_ms;\n'
                   'static unsigned d2_last_wait_calls;')
    text = replace(text, '    FILE* f=fopen("tempos-quadros.csv","w");',
                   '    const char* path=d2_performance_csv_path();\n'
                   '    FILE* f=d2_capture_fopen(path && *path ? path : "tempos-quadros.csv","w");')
    text = replace(text, 'could not open tempos-quadros.csv', 'could not open performance CSV')
    text = replace(text, 'draws,fifo_host_ms\\n',
                   'draws,fifo_host_ms,wait_fence_host_ms,prepare_host_ms,present_host_ms,wait_fence_calls\\n')
    text = replace(text, '"%u,%s,%.6f,%.6f,%.6f,%u,%u,%u,%.6f\\n"',
                   '"%u,%s,%.6f,%.6f,%.6f,%u,%u,%u,%.6f,%.6f,%.6f,%.6f,%u\\n"')
    text = replace(text, 'p->misses,p->cache,p->draws,p->fifo_ms);',
                   'p->misses,p->cache,p->draws,p->fifo_ms,p->wait_ms,p->prepare_ms,p->present_ms,p->wait_calls);')
    text = replace(text, '    for(unsigned i=0;i<4;i++)if(GetAsyncKeyState(vk[i])&0x8000)keys|=1u<<i;',
                   '    if(GetForegroundWindow()==s_d3d.hwnd)\n'
                   '        for(unsigned i=0;i<4;i++)if(GetAsyncKeyState(vk[i])&0x8000)keys|=1u<<i;')
    text = replace(text, '{"inicio", "castle", "batalha", "save"}',
                   '{"inicio", "castle", "batalha", "mothman"}')
    text = replace(text, 'F9 save, F10 finish', 'F9 mothman, F10 finish')
    # Do not sort samples or write five-second summaries during measurement.
    text = replace(text, '        d2_lite_summary();\n', '')
    text = replace(text, '        if(now-d2_lite_report_time>=5.0){d2_lite_summary();d2_lite_report_time=now;}\n', '')
    text = replace(text, 'if(d2_lite_n==D2_LITE_MAX){d2_lite_summary();d2_lite_save();finished=1;}',
                   'if(d2_lite_n==D2_LITE_MAX){d2_lite_save();finished=1;}')
    text = replace(text, '        p->fifo_ms=d2_fifo_ms;',
                   '        p->fifo_ms=d2_fifo_ms;\n'
                   '        p->wait_ms=d2_profile_wait_ms-d2_last_wait_ms;\n'
                   '        p->prepare_ms=d2_profile_prepare_ms-d2_last_prepare_ms;\n'
                   '        p->present_ms=d2_profile_present_ms-d2_last_present_ms;\n'
                   '        p->wait_calls=d2_profile_wait_calls-d2_last_wait_calls;')
    text = replace(text, '    d2_lite_previous=now;d2_lite_last_compile=d2_lite_compile_ms;d2_lite_last_misses=d2_lite_misses;',
                   '    d2_lite_previous=now;d2_lite_last_compile=d2_lite_compile_ms;d2_lite_last_misses=d2_lite_misses;\n'
                   '    d2_last_wait_ms=d2_profile_wait_ms;d2_last_prepare_ms=d2_profile_prepare_ms;\n'
                   '    d2_last_present_ms=d2_profile_present_ms;d2_last_wait_calls=d2_profile_wait_calls;')
    Path(destination).write_text(text, encoding='utf-8', newline='\n')
