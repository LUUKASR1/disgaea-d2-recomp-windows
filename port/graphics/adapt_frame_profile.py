"""Extend the pinned lightweight sampler in a build copy, never the SDK."""
from pathlib import Path
import hashlib

EXPECTED = '25327e7524f4e8885e22397dfe06e8c40c7a0431776d7373f8e3f8ffc2343bb7'
DETAIL_TIMES = (('prepare_setup_ms', 'setup_ms'), ('prepare_targets_ms', 'targets_ms'),
                ('prepare_draws_ms', 'draws_ms'), ('resource_create_host_ms', 'resource_ms'),
                ('shader_compile_host_ms', 'shader_ms'), ('pso_create_host_ms', 'pso_ms'))
DETAIL_COUNTS = (('texture_create_calls', 'textures'), ('buffer_create_calls', 'buffers'),
                 ('resource_create_failures', 'resource_failures'), ('shader_compile_calls', 'shader_calls'),
                 ('pso_create_calls', 'pso_calls'), ('pso_create_failures', 'pso_failures'))


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
    # Versioned, bounded detail columns; counters are snapshotted even before a
    # stage starts, so startup costs cannot leak into the first recorded frame.
    fields = DETAIL_TIMES + DETAIL_COUNTS
    declarations = ''.join('    ' + kind + ' ' + name + ';\n'
                           for kind, entries in [('double', DETAIL_TIMES), ('unsigned', DETAIL_COUNTS)]
                           for name, _ in entries)
    text = replace(text, '    unsigned wait_calls;', '    unsigned wait_calls;\n' + declarations)
    text = replace(text, 'static unsigned d2_last_wait_calls;',
                   'static unsigned d2_last_wait_calls;\n' + ''.join(
                       'static ' + kind + ' d2_last_' + counter + ';\n'
                       for kind, entries in [('double', DETAIL_TIMES), ('unsigned', DETAIL_COUNTS)]
                       for _, counter in entries))
    text = replace(text, 'present_host_ms,wait_fence_calls\\n',
                   'present_host_ms,wait_fence_calls,' + ','.join(name for name, _ in fields) + '\\n')
    text = replace(text, '%.6f,%u\\n",i,d2_lite_names[p->stage],',
                   '%.6f,%u,' + ','.join(['%.6f'] * len(DETAIL_TIMES) + ['%u'] * len(DETAIL_COUNTS)) + '\\n",i,d2_lite_names[p->stage],')
    text = replace(text, 'p->present_ms,p->wait_calls);',
                   'p->present_ms,p->wait_calls,' + ','.join('p->' + name for name, _ in fields) + ');')
    text = replace(text, '        p->wait_calls=d2_profile_wait_calls-d2_last_wait_calls;',
                   '        p->wait_calls=d2_profile_wait_calls-d2_last_wait_calls;\n' + ''.join(
                       '        p->' + name + '=d2_profile_' + counter + '-d2_last_' + counter + ';\n'
                       for name, counter in fields))
    text = replace(text, 'd2_last_wait_calls=d2_profile_wait_calls;',
                   'd2_last_wait_calls=d2_profile_wait_calls;\n' + ''.join(
                       '    d2_last_' + counter + '=d2_profile_' + counter + ';\n' for _, counter in fields))
    text = replace(text, '    unsigned wait_calls;',
                   '    unsigned wait_calls;\n    double fifo_lock_wait_ms;\n'
                   '    unsigned fifo_lock_calls, fifo_flip_held_calls;')
    text = replace(text, 'extern double d2_fifo_frame_take_ms(void);',
                   'extern double d2_fifo_frame_take_ms(void);\n'
                   'extern void d2_fifo_lock_take(double*,unsigned*,unsigned*);')
    text = replace(text, '    double d2_fifo_ms=d2_fifo_frame_take_ms();',
                   '    double d2_fifo_ms=d2_fifo_frame_take_ms();\n'
                   '    double d2_lock_ms; unsigned d2_lock_calls,d2_held_calls;\n'
                   '    d2_fifo_lock_take(&d2_lock_ms,&d2_lock_calls,&d2_held_calls);')
    text = replace(text, '        p->fifo_ms=d2_fifo_ms;',
                   '        p->fifo_ms=d2_fifo_ms;\n'
                   '        p->fifo_lock_wait_ms=d2_lock_ms;\n'
                   '        p->fifo_lock_calls=d2_lock_calls;\n'
                   '        p->fifo_flip_held_calls=d2_held_calls;')
    text = replace(text, 'pso_create_failures\\n',
                   'pso_create_failures,fifo_lock_wait_ms,fifo_lock_calls,fifo_flip_held_calls\\n')
    text = replace(text, 'p->pso_create_failures);',
                   'p->pso_create_failures,p->fifo_lock_wait_ms,p->fifo_lock_calls,p->fifo_flip_held_calls);')
    # Append a floating time and two integer counts to the final format fields.
    old_format = ','.join(['%.6f'] * len(DETAIL_TIMES) + ['%u'] * len(DETAIL_COUNTS)) + '\\n",i,'
    text = replace(text, old_format, old_format.replace('\\n",i,', ',%.6f,%u,%u\\n",i,'))
    text = replace(text, '    if(!d2_lite_n || d2_lite_saved==d2_lite_n) return;',
                   '    if(!d2_lite_n || d2_lite_saved==d2_lite_n) return;\n'
                   '#ifdef D2_DRAW_PROFILE_STOP\n    D2_DRAW_PROFILE_STOP();\n#endif')
    text = replace(text, '    if(fclose(f)==0){d2_lite_saved=d2_lite_n;',
                   '    if(fclose(f)==0){\n#ifdef D2_DRAW_PROFILE_SAVE\n'
                   '        D2_DRAW_PROFILE_SAVE(path && *path ? path : "tempos-quadros.csv");\n#endif\n'
                   '        d2_lite_saved=d2_lite_n;')
    Path(destination).write_text(text, encoding='utf-8', newline='\n')
