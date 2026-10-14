#pragma once
#include <stdint.h>
/* Only timing eligibility; FIFO completion, ticket ownership and retirement stay mandatory. */
static inline int d2_flip_eligible(int pending,int waiting_fifo,uint32_t presented,uint32_t completed,
    int vsync,uint64_t vblank,uint64_t earliest,uint64_t last,int immediate,int unlimited)
{
    return pending && !waiting_fifo && presented==completed &&
        (unlimited || !vsync || (immediate && vblank>last) || (vblank>=earliest && vblank>last));
}
