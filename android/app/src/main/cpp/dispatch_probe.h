#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
    D2_DISPATCH_OK, D2_DISPATCH_READ, D2_DISPATCH_WRITE,
    D2_DISPATCH_MISSING_FUNCTION, D2_DISPATCH_BAD_OPD,
    D2_DISPATCH_SYSCALL_UNAVAILABLE, D2_DISPATCH_HLE_UNAVAILABLE,
    D2_DISPATCH_DEPTH, D2_DISPATCH_TRAMPOLINE
};
/* Synthetic diagnostic only; serialized after all other VM owners release. */
int d2_dispatch_probe_run(const char *private_directory, char *report, size_t capacity);
int d2_dispatch_vm_begin(void);
int d2_dispatch_vm_end(void);
int d2_dispatch_guest_range(uint64_t address,uint32_t bytes,int write);
int d2_services_init(void);
uint32_t d2_services_rtc_nid(void);
void d2_services_syscall(void *context,size_t bytes);
void d2_services_hle(uint32_t nid,void *context,size_t bytes);
size_t d2_sdk_ppu_layout(size_t *values, size_t capacity);
void d2_lift_fault(int error);
int d2_lift_faulted(void);
uint8_t d2_lift_read8(uint64_t);
uint16_t d2_lift_read16(uint64_t);
uint32_t d2_lift_read32(uint64_t);
uint64_t d2_lift_read64(uint64_t);
void d2_lift_write8(uint64_t,uint8_t);
void d2_lift_write16(uint64_t,uint16_t);
void d2_lift_write32(uint64_t,uint32_t);
void d2_lift_write64(uint64_t,uint64_t);
#ifdef __cplusplus
}
#endif
