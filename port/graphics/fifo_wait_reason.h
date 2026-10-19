#pragma once
#include <stdint.h>
#include "flip_policy.h"
#define D2_WAIT_REASONS 5
/* Priority matches prerequisites of the existing flip selection predicate. */
static inline unsigned d2_wait_reason(int pending,int fifo,uint32_t presented,uint32_t completed,
    int vsync,uint64_t vblank,uint64_t earliest,uint64_t last,int immediate,int unlimited)
{
    if(!pending)return 0;
    if(fifo)return 1;
    if(presented!=completed)return 2;
    return d2_flip_eligible(pending,fifo,presented,completed,vsync,vblank,earliest,last,immediate,unlimited)?4:3;
}
#ifdef __cplusplus
extern "C" {
#endif
void d2_fifo_wait_set_active(int active);
void d2_fifo_wait_take(double milliseconds[D2_WAIT_REASONS],unsigned calls[D2_WAIT_REASONS],unsigned outcomes[3]);
#ifdef __cplusplus
}
#endif
