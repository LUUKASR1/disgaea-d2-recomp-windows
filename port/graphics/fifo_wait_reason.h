#pragma once
#include <stdint.h>
#include "flip_policy.h"
#define D2_WAIT_REASONS 5
#define D2_NO_FLIP_STATES 5
/* Only sampled for no-pending-flip waits; inputs are entry observations. */
static inline unsigned d2_no_flip_state(int host,int label,int queued,int put_get_differ)
{
    if(!host)return 4;
    if(label)return 0;
    if(queued)return 1;
    return put_get_differ?2:3;
}
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
void d2_fifo_no_flip_take(double milliseconds[D2_NO_FLIP_STATES],unsigned calls[D2_NO_FLIP_STATES]);
void d2_fifo_wait_take(double milliseconds[D2_WAIT_REASONS],unsigned calls[D2_WAIT_REASONS],unsigned outcomes[3]);
#ifdef __cplusplus
}
#endif
