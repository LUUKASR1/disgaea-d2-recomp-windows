/* Checked bridge for our lifted fixture, using the real SDK memory accessors.
 * No arbitrary guest image is accepted. VM ownership is serialized by caller.
 */
#include "dispatch_probe.h"
#include "runtime/memory/vm.h"
#include "runtime/ppu/ppu_memory.h"
static int owns_vm;
int d2_dispatch_vm_begin(void) {
    if (VM_READY || owns_vm) return 0;
    if (vm_init()!=CELL_OK) return 0;
    if (vm_commit(0,0x10000)!=CELL_OK || vm_commit(0x10010000,0xff0000)!=CELL_OK) {
        vm_shutdown(); return 0;
    }
    ppu_vm_size=0x11000000; owns_vm=1; return 1;
}
int d2_dispatch_vm_end(void) {
    if (!owns_vm) return 0; // Never release a different owner's mapping.
    vm_shutdown(); ppu_vm_size=0; owns_vm=0;
    return vm_base==NULL;
}
size_t d2_sdk_ppu_layout(size_t *out,size_t capacity) {
    const size_t values[]={sizeof(ppu_context),_Alignof(ppu_context),
        offsetof(ppu_context,gpr),offsetof(ppu_context,fpr),offsetof(ppu_context,vr),
        offsetof(ppu_context,cr),offsetof(ppu_context,lr),offsetof(ppu_context,ctr),
        offsetof(ppu_context,xer),offsetof(ppu_context,fpscr),offsetof(ppu_context,vscr),
        offsetof(ppu_context,cia),offsetof(ppu_context,thread_id),
        offsetof(ppu_context,reserve_addr),offsetof(ppu_context,reserve_value),
        offsetof(ppu_context,reserve_valid)};
    const size_t n=sizeof(values)/sizeof(values[0]);
    if (capacity>=n && out) memcpy(out,values,sizeof(values));
    return n;
}
static int checked(uint64_t address,uint32_t bytes,int write) {
    if (d2_lift_faulted()) return 0;
    /* Restrict lifted fixture reads/writes to its committed RAM. Check the
     * 64-bit address before conversion: never truncate an overflow to guest EA. */
    int main_range=address>=0x10000 && address<ppu_vm_size && bytes<=(uint64_t)ppu_vm_size-address;
    if (!owns_vm || !vm_base || (!main_range && !d2_bundle_range(address,bytes,write))) {
        d2_lift_fault(write ? D2_DISPATCH_WRITE : D2_DISPATCH_READ); return 0;
    }
    return 1;
}
int d2_dispatch_guest_range(uint64_t address,uint32_t bytes,int write) {
    return checked(address,bytes,write);
}
#define READ(width,type) type d2_lift_read##width(uint64_t a) { \
    return checked(a,sizeof(type),0) ? vm_read##width((uint32_t)a) : 0; }
#define WRITE(width,type) void d2_lift_write##width(uint64_t a,type v) { \
    if(checked(a,sizeof(type),1)) vm_write##width((uint32_t)a,v); }
READ(8,uint8_t) READ(16,uint16_t) READ(32,uint32_t) READ(64,uint64_t)
WRITE(8,uint8_t) WRITE(16,uint16_t) WRITE(32,uint32_t) WRITE(64,uint64_t)
