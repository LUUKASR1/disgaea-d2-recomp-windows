/* Minimal service bridge. Actual handlers and both tables come from the SDK.
 * Full PPU HLE ABI, timer workers and unrelated services are not enabled.
 */
#include "dispatch_probe.h"
#include "file_services.h"
#include "runtime/syscalls/sys_timer.h"
#include "libs/system/cellRtc.h"
#include "ps3emu/nid.h"
#include <string.h>
static lv2_syscall_table syscalls;
static ps3_nid_table imports;
static ps3_nid_entry import_storage[1];
static uint32_t rtc_nid;

int d2_services_init(void) {
    lv2_syscall_table_init(&syscalls);
    lv2_syscall_register(&syscalls,SYS_TIME_GET_TIMEBASE_FREQUENCY,sys_time_get_timebase_frequency);
    lv2_syscall_register(&syscalls,SYS_TIME_GET_CURRENT_TIME,sys_time_get_current_time);
    rtc_nid=ps3_compute_nid("cellRtcGetCurrentTick");
    ps3_nid_table_init(&imports,import_storage,1);
    return ps3_nid_table_add(&imports,rtc_nid,"cellRtcGetCurrentTick",(void*)cellRtcGetCurrentTick)==0;
}
uint32_t d2_services_rtc_nid(void) { return rtc_nid; }
static int pointer_out(uint64_t address,int optional) {
    if(!address && optional) return 1;
    if(address&7) { d2_lift_fault(D2_DISPATCH_WRITE); return 0; }
    return d2_dispatch_guest_range(address,8,1);
}
static void result(void *ctx,int64_t value) {
    /* Context ABI was checked before dispatch; copy avoids aliasing the lifter's
     * independently named context type as the SDK's C struct. Only r3 changes. */
    memcpy((char*)ctx+offsetof(ppu_context,gpr)+3*sizeof(uint64_t),&value,sizeof(value));
}
void d2_services_syscall(void *context,size_t bytes) {
    if(d2_lift_faulted()) return;
    if(!context || bytes!=sizeof(ppu_context)) { d2_lift_fault(D2_DISPATCH_BAD_OPD); return; }
    ppu_context ctx; memcpy(&ctx,context,sizeof(ctx));
    uint64_t num=ctx.gpr[11];
    if(num!=SYS_TIME_GET_TIMEBASE_FREQUENCY && num!=SYS_TIME_GET_CURRENT_TIME) {
        if(d2_files_syscall(context,bytes)) return;
        if(d2_bundle_syscall(context,bytes)) return;
        result(context,(int64_t)(int32_t)CELL_ENOSYS);
        d2_lift_fault(D2_DISPATCH_SYSCALL_UNAVAILABLE); return;
    }
    if(num==SYS_TIME_GET_CURRENT_TIME &&
       (!pointer_out(ctx.gpr[3],1) || !pointer_out(ctx.gpr[4],1))) {
        result(context,(int64_t)(int32_t)CELL_EFAULT); return;
    }
    lv2_syscall_dispatch(&syscalls,&ctx);
    result(context,(int64_t)ctx.gpr[3]);
}
void d2_services_hle(uint32_t nid,void *context,size_t bytes) {
    if(d2_lift_faulted()) return;
    if(!context || bytes!=sizeof(ppu_context)) { d2_lift_fault(D2_DISPATCH_BAD_OPD); return; }
    ps3_nid_entry *entry=ps3_nid_table_find(&imports,nid);
    if(!entry) {
        result(context,(int64_t)(int32_t)CELL_ENOSYS);
        d2_lift_fault(D2_DISPATCH_HLE_UNAVAILABLE); return;
    }
    ppu_context ctx; memcpy(&ctx,context,sizeof(ctx));
    // NULL goes to the real handler, which must return CELL_EINVAL.
    if(!pointer_out(ctx.gpr[3],1)) { result(context,(int64_t)(int32_t)CELL_EFAULT); return; }
    typedef s32 (*rtc_function)(CellRtcTick*);
    s32 value=((rtc_function)entry->handler)((CellRtcTick*)(uintptr_t)ctx.gpr[3]);
    result(context,(int64_t)value);
}
