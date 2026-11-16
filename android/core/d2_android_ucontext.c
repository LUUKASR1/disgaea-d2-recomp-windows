/* ARM64 implementation of the ucontext routines that bionic omits.
 * Only the AAPCS64 callee-saved state is switched (x19-x30, sp, d8-d15), which is all that a cooperative
 * fiber switch needs. The state lives in the otherwise unused uc_mcontext.__reserved area of ucontext_t. */
#if !defined(__aarch64__)
#error "d2_android_ucontext.c is ARM64 only"
#endif
#include "d2_android_ucontext.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

/* Layout of the saved state (bytes): x19..x30 at 0..95, sp at 96, d8..d15 at 104..167. */
void d2_ctx_save(void* state);   /* returns 0, and again 0 when the state is loaded later */
void d2_ctx_load(const void* state) __attribute__((noreturn));
void d2_ctx_swap(void* from, const void* to);
void d2_ctx_entry(void);

__asm__(
    ".text\n"
    ".balign 16\n"
    ".globl d2_ctx_save\n"
    "d2_ctx_save:\n"
    "  stp x19, x20, [x0, #0]\n"
    "  stp x21, x22, [x0, #16]\n"
    "  stp x23, x24, [x0, #32]\n"
    "  stp x25, x26, [x0, #48]\n"
    "  stp x27, x28, [x0, #64]\n"
    "  stp x29, x30, [x0, #80]\n"
    "  mov x9, sp\n"
    "  str x9, [x0, #96]\n"
    "  stp d8, d9,   [x0, #104]\n"
    "  stp d10, d11, [x0, #120]\n"
    "  stp d12, d13, [x0, #136]\n"
    "  stp d14, d15, [x0, #152]\n"
    "  mov x0, #0\n"
    "  ret\n"
    ".balign 16\n"
    ".globl d2_ctx_load\n"
    "d2_ctx_load:\n"
    "  ldp x19, x20, [x0, #0]\n"
    "  ldp x21, x22, [x0, #16]\n"
    "  ldp x23, x24, [x0, #32]\n"
    "  ldp x25, x26, [x0, #48]\n"
    "  ldp x27, x28, [x0, #64]\n"
    "  ldp x29, x30, [x0, #80]\n"
    "  ldr x9, [x0, #96]\n"
    "  mov sp, x9\n"
    "  ldp d8, d9,   [x0, #104]\n"
    "  ldp d10, d11, [x0, #120]\n"
    "  ldp d12, d13, [x0, #136]\n"
    "  ldp d14, d15, [x0, #152]\n"
    "  mov x0, #0\n"
    "  ret\n"
    ".balign 16\n"
    ".globl d2_ctx_swap\n"
    "d2_ctx_swap:\n"
    "  mov x10, x1\n"
    "  stp x19, x20, [x0, #0]\n"
    "  stp x21, x22, [x0, #16]\n"
    "  stp x23, x24, [x0, #32]\n"
    "  stp x25, x26, [x0, #48]\n"
    "  stp x27, x28, [x0, #64]\n"
    "  stp x29, x30, [x0, #80]\n"
    "  mov x9, sp\n"
    "  str x9, [x0, #96]\n"
    "  stp d8, d9,   [x0, #104]\n"
    "  stp d10, d11, [x0, #120]\n"
    "  stp d12, d13, [x0, #136]\n"
    "  stp d14, d15, [x0, #152]\n"
    "  mov x0, x10\n"
    "  b d2_ctx_load\n"
    /* First run of a fiber: x19 = function, x20/x21 = arguments, x22 = uc_link. */
    ".balign 16\n"
    ".globl d2_ctx_entry\n"
    "d2_ctx_entry:\n"
    "  mov x0, x20\n"
    "  mov x1, x21\n"
    "  blr x19\n"
    "  mov x0, x22\n"
    "  cbz x0, 1f\n"
    "  bl setcontext\n"
    "1:\n"
    "  bl abort\n"
);

static void* ctx_state(const ucontext_t* ucp) { return (void*)&ucp->uc_mcontext.__reserved[0]; }

int getcontext(ucontext_t* ucp) {
    d2_ctx_save(ctx_state(ucp));
    return 0;
}

int setcontext(const ucontext_t* ucp) {
    d2_ctx_load(ctx_state(ucp));
}

int swapcontext(ucontext_t* oucp, const ucontext_t* ucp) {
    d2_ctx_swap(ctx_state(oucp), ctx_state(ucp));
    return 0;
}

void makecontext(ucontext_t* ucp, void (*func)(void), int argc, ...) {
    uint64_t* state = (uint64_t*)ctx_state(ucp);
    uint64_t args[2] = {0, 0};
    va_list list;
    va_start(list, argc);
    for (int i = 0; i < argc && i < 2; i++) args[i] = (uint64_t)(uint32_t)va_arg(list, int);
    va_end(list);
    uintptr_t top = ((uintptr_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size) & ~(uintptr_t)15;
    for (int i = 0; i < 21; i++) state[i] = 0;
    state[0] = (uint64_t)(uintptr_t)func;       /* x19 */
    state[1] = args[0];                          /* x20 */
    state[2] = args[1];                          /* x21 */
    state[3] = (uint64_t)(uintptr_t)ucp->uc_link;/* x22 */
    state[11] = (uint64_t)(uintptr_t)&d2_ctx_entry; /* x30: where d2_ctx_load "returns" */
    state[12] = (uint64_t)top;                   /* sp */
}
