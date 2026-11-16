/* Android (bionic) has ucontext_t but not getcontext/makecontext/swapcontext/setcontext.
 * ps3recomp's cellFiber uses them for PPU fibers, so ARM64 provides them here (see the .c file). */
#ifndef D2_ANDROID_UCONTEXT_H
#define D2_ANDROID_UCONTEXT_H
#include <ucontext.h>
#ifdef __cplusplus
extern "C" {
#endif
int getcontext(ucontext_t* ucp);
int setcontext(const ucontext_t* ucp);
int swapcontext(ucontext_t* oucp, const ucontext_t* ucp);
void makecontext(ucontext_t* ucp, void (*func)(void), int argc, ...);
#ifdef __cplusplus
}
#endif
#endif
