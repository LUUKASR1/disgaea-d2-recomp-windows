#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The caller binds the exact submitted pipeline, descriptors, CBs and VB.
   This isolated replay reports shader survivors using depth writes, including
   fragments whose output color is black/zero. Requires depth-write ALL,
   LESS/LESS_EQUAL, stencil disabled, single RGBA8 target and no depth override.
   This is per-draw shader coverage, not scene visibility or blend contribution.
   Borrowed COM objects are retained until GPU completion (also on timeout). */
typedef void (*D2AlphaRecord)(void* command_list,void* user);
int d2_alpha_replay(void* device,void* queue,uint32_t width,uint32_t height,
    D2AlphaRecord record,void* user,void* const* borrowed,uint32_t borrowed_count,
    unsigned char* mask);
#ifdef __cplusplus
}
#endif
