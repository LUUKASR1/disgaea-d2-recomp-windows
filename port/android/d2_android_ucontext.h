/* Android (bionic) has ucontext_t but not getcontext/makecontext/swapcontext/setcontext.
 * ps3recomp's cellFiber uses them for PPU fibers, so ARM64 provides them here (see the .c file). */
#ifndef D2_ANDROID_UCONTEXT_H
#define D2_ANDROID_UCONTEXT_H
#include <ucontext.h>
#include <pthread.h>

/* bionic has no pthread_cancel/pthread_testcancel. Guest workers are only cancelled while the game shuts down,
 * and on Android shutdown ends the process (the app calls _exit), so cancellation is a no-op here. */
static inline int pthread_cancel(pthread_t thread) { (void)thread; return 0; }
static inline void pthread_testcancel(void) {}
#ifndef PTHREAD_CANCEL_ENABLE
#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#endif
static inline int pthread_setcancelstate(int state, int* old) { (void)state; if (old) *old = PTHREAD_CANCEL_ENABLE; return 0; }
static inline int pthread_setcanceltype(int type, int* old) { (void)type; if (old) *old = PTHREAD_CANCEL_DEFERRED; return 0; }
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
