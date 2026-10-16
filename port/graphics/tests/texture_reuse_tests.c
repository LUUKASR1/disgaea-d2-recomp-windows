#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "texture_reuse.h"
#define VP_TEX_SLOTS 8
#define MAX_DRAWS 8
typedef uint32_t u32;
typedef struct {
    void* res; void* up;
    u32 w, h, fmt, key;
    int cube, used;
} VPTexSlot;
typedef struct {
    int is_vp, is_clear;
    struct { u32 off; int set; } tex[4];
} D3D12DrawRecord;
static struct {
    VPTexSlot vp_tex[VP_TEX_SLOTS];
    D3D12DrawRecord draws[MAX_DRAWS + 1];
    u32 draw_count;
} s_d3d;
static int enabled=1;
static int d2_texture_reuse_enabled(void) { return enabled; }
#include "texture_reuse_adapter.inc"
#define CHECK(condition) do { if(!(condition)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition); return 1; } } while(0)

int main(void)
{
    /* Regression: old scratch slot 31 aliases the first per-draw descriptor.
     * Every experimental scratch entry must be outside all reserved/draw SRVs
     * and inside the expanded heap; A/B off retains the original addresses. */
    for(size_t i=0;i<96;i++) {
        size_t index=d2_texture_descriptor_index(32,2048,i,1);
        CHECK(index >= 32+2048*4 && index < 32+2048*4+96);
        CHECK(d2_texture_descriptor_index(32,2048,i,0)==1+i);
    }
    /* All keys collide in the reservation hash. Duplicate cached keys must
     * both remain protected, regardless of their allocation formats. */
    for(int i=0;i<VP_TEX_SLOTS;i++) {
        s_d3d.vp_tex[i]=(VPTexSlot){(void*)(uintptr_t)(i+1),(void*)(uintptr_t)(i+20),64,64,0x85,
                                   (u32)(i*257+257),0,0};
    }
    s_d3d.vp_tex[6].key=s_d3d.vp_tex[0].key;
    s_d3d.vp_tex[6].fmt=0x81;
    s_d3d.draw_count=MAX_DRAWS+1;
    s_d3d.draws[0].is_vp=1;
    s_d3d.draws[0].tex[0].set=1;
    s_d3d.draws[0].tex[0].off=s_d3d.vp_tex[0].key;
    s_d3d.draws[1].is_vp=1; s_d3d.draws[1].is_clear=1;
    s_d3d.draws[1].tex[0].set=1; s_d3d.draws[1].tex[0].off=s_d3d.vp_tex[1].key;
    s_d3d.draws[2].tex[0].set=1; s_d3d.draws[2].tex[0].off=s_d3d.vp_tex[2].key; /* not VP */
    s_d3d.draws[3].is_vp=1; s_d3d.draws[3].tex[0].off=s_d3d.vp_tex[3].key; /* not bound */
    s_d3d.draws[MAX_DRAWS].is_vp=1;
    s_d3d.draws[MAX_DRAWS].tex[0].set=1;
    s_d3d.draws[MAX_DRAWS].tex[0].off=s_d3d.vp_tex[4].key; /* past bounded draw list */
    d2_texture_reserve_frame();
    CHECK(d2_texture_retained[0] && d2_texture_retained[6]);
    for(int i=1;i<VP_TEX_SLOTS;i++) if(i!=6) CHECK(!d2_texture_retained[i]);
    /* The original free slot is a live cached texture. Choose compatible slot
     * 1 instead, leaving the future request and all resource identities intact. */
    VPTexSlot before[VP_TEX_SLOTS]; memcpy(before,s_d3d.vp_tex,sizeof before);
    CHECK(d2_texture_reuse_slot(0,64,64,0x85,0)==1);
    CHECK(!memcmp(before,s_d3d.vp_tex,sizeof before));
    s_d3d.vp_tex[1].used=1;
    CHECK(d2_texture_reuse_slot(0,64,64,0x85,0)==2);
    s_d3d.vp_tex[2].up=NULL;
    s_d3d.vp_tex[3].h=128;
    s_d3d.vp_tex[4].fmt=0x86;
    s_d3d.vp_tex[5].cube=1;
    s_d3d.vp_tex[7].w=128;
    CHECK(d2_texture_reuse_slot(0,64,64,0x85,0)==0); /* fallback is unchanged */
    CHECK(d2_texture_reuse_slot(-1,64,64,0x85,0)==-1);
    enabled=0;
    CHECK(d2_texture_reuse_slot(7,64,64,0x85,0)==7);
    enabled=1;
    s_d3d.draw_count=0;d2_texture_reserve_frame();
    for(int i=0;i<VP_TEX_SLOTS;i++) CHECK(!d2_texture_retained[i]);
    CHECK(d2_texture_reuse_slot(7,64,64,0x85,0)==0);
    puts("PASS: compatible allocation selection, live-key reservation, collisions, bounds and original fallback");
    return 0;
}
