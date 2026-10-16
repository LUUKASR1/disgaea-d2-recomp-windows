"""Remove only the audited presentation gate in an isolated SDK build copy."""
from pathlib import Path
import hashlib
import re
import sys

EXPECTED = 'b1f3fe82a77330e262b3e9a6f9ed263ff89a4442111b7e36ba7797c422027ff7'

def replace(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('GCM timing anchor changed: '+old[:80])
    return text.replace(old,new)

def generate(source, output):
    source=Path(source).resolve()
    data=source.read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXPECTED:
        raise RuntimeError('Pinned GCM differs; original SDK preserved')
    text=data.decode()
    text=replace(text, 'void cellGcm_rsx_process_fifo(void)\n{\n    AcquireSRWLockExclusive(&s_gcm_fifo_lock);',
        '#include "fifo_lock_profile.h"\n\nvoid cellGcm_rsx_process_fifo(void)\n{\n'
        '    LONG64 d2_lock_start=d2_fifo_lock_begin();\n'
        '    AcquireSRWLockExclusive(&s_gcm_fifo_lock);\n'
        '    d2_fifo_lock_acquired(d2_lock_start,s_fifo_flip_reached);')
    text=replace(text,'int cellGcm_take_flip_pending(void)', '''static int d2_gcm_unlimited(void)
{
    static int enabled=-1;
    if(enabled<0){
        const char* value=getenv("D2_GCM_UNLIMITED");
        enabled=value && strcmp(value,"1")==0;
        fprintf(stderr,"[D2GCM] unlimited-presentation=%d vblank-clock=60Hz\\n",enabled);
    }
    return enabled;
}

int cellGcm_take_flip_pending(void)''')
    text=replace(text,'''    if (s_flip_pending && !s_flip_queue[slot].waiting_fifo &&
        s_presented_flip_count == completed &&
        (s_flip_mode != CELL_GCM_DISPLAY_VSYNC ||
         (d2_gcm_immediate() && vblank > s_last_present_vblank) ||
         (vblank >= s_flip_queue[slot].earliest_vblank && vblank > s_last_present_vblank))) {''',
        '''    if (d2_flip_eligible(s_flip_pending,s_flip_queue[slot].waiting_fifo,
        s_presented_flip_count,completed,s_flip_mode==CELL_GCM_DISPLAY_VSYNC,
        vblank,s_flip_queue[slot].earliest_vblank,s_last_present_vblank,
        d2_gcm_immediate(),d2_gcm_unlimited())) {''')
    text=replace(text,'if(d2_ready_wake_enabled()) {','if(d2_ready_wake_enabled() || d2_gcm_unlimited()) {')
    text=replace(text,'''int d2_ready=d2_ready_wake_predicate(s_flip_pending,
            s_flip_queue[d2_slot].waiting_fifo,s_presented_flip_count,d2_completed,
            s_flip_mode==CELL_GCM_DISPLAY_VSYNC,gcm_vblank_at(gcm_vblank_elapsed_ns()),
            s_flip_queue[d2_slot].earliest_vblank,s_last_present_vblank,d2_gcm_immediate());''',
        '''int d2_ready=d2_flip_eligible(s_flip_pending,
            s_flip_queue[d2_slot].waiting_fifo,s_presented_flip_count,d2_completed,
            s_flip_mode==CELL_GCM_DISPLAY_VSYNC,gcm_vblank_at(gcm_vblank_elapsed_ns()),
            s_flip_queue[d2_slot].earliest_vblank,s_last_present_vblank,d2_gcm_immediate(),d2_gcm_unlimited());''')
    def include(match):
        path=source.parent/match[1]
        return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else match[0]
    text=re.sub(r'#\s*include\s*"([^"\n]+)"',include,text)
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text('#include "flip_policy.h"\n'+text,encoding='utf-8',newline='\n')

if __name__=='__main__':
    generate(sys.argv[1],sys.argv[2])
