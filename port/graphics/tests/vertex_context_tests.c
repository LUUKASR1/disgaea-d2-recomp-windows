/* Exercise the actual pinned SDK helper and decoder, not a rewritten decoder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rsx_vertex_fetch.h"
typedef struct { float v[4]; } VPSlot;
#include "d2_vertex_context.h"
static u8 guest[1024*1024];
u8* vm_base=guest;
u32 cellGcmResolveLocated(int local,u32 offset)
{ return (local?512u*1024u:0u)+(offset%(512u*1024u-16u)); }
u32 cellGcmResolveOffset(u32 offset) { return cellGcmResolveLocated(0,offset); }
static u32 rng=0x9e3779b9u;
static u32 next(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed line %d\n",__LINE__);return 1;}}while(0)
static void reference(const rsx_state* state,u32 vi,VPSlot out[16])
{
    memcpy(out,state->vertex_data4f,sizeof(VPSlot)*16);
    for(unsigned i=0;i<16;i++) {
        const rsx_vertex_attrib* a=&state->vertex_attribs[i];
        if(a->enabled && a->stride && vm_base) rsx_fetch_attrib(state,(int)i,vi,out[i].v);
    }
}
int main(void)
{
    const u32 frequencies[]={0,1,2,3,17,65535};
    for(unsigned i=0;i<sizeof guest;i++) guest[i]=(u8)next();
    rsx_state state={0},other={0};
    VPSlot expected[16],actual[16];
    for(unsigned trial=0;trial<300;trial++) {
        for(unsigned i=0;i<16;i++) {
            for(unsigned lane=0;lane<4;lane++) {
                u32 bits=next();memcpy(&state.vertex_data4f[i][lane],&bits,4);
            }
            rsx_vertex_attrib* a=&state.vertex_attribs[i];
            a->enabled=(int)(next()&1u);a->stride=(next()%5u)*4u;
            a->size=next()%6u;a->type=next()%9u;
            a->frequency=frequencies[next()%6u];
            a->offset=(next()&0x1ffffu)|((next()&1u)<<31);
        }
        state.frequency_divider_op=next();
        d2_vertex_context_prepare(&state);
        CHECK(d2_vertex_context.valid);
        CHECK(!d2_vertex_context_read(&other,0,actual));
        for(unsigned k=0;k<24;k++) {
            u32 vi=k==23?0xffffffffu:next()%1000u;
            reference(&state,vi,expected);
            CHECK(d2_vertex_context_read(&state,vi,actual));
            CHECK(memcmp(expected,actual,sizeof actual)==0);
        }
        /* Guest memory changes between uploads must be observed, regardless
         * of identical address/layout. Preparation is not a decoded-data cache. */
        guest[next()%sizeof guest]^=255;
    }
    /* Disabled arrays and zero-stride registers keep all four live lanes. */
    memset(&state,0,sizeof state);
    for(unsigned lane=0;lane<4;lane++) state.vertex_data4f[0][lane]=(float)(lane+1);
    d2_vertex_context_prepare(&state);
    CHECK(d2_vertex_context_read(&state,0,actual));
    CHECK(memcmp(actual[0].v,state.vertex_data4f[0],16)==0);
    state.vertex_data4f[0][0]=99;
    d2_vertex_context_prepare(&state);
    CHECK(d2_vertex_context_read(&state,0,actual) && actual[0].v[0]==99);
    state.vertex_attribs[0].enabled=1;state.vertex_attribs[0].stride=16;
    state.vertex_attribs[0].type=2;state.vertex_attribs[0].size=4;
    state.vertex_attribs[0].offset=0x80000000u; /* MAIN starts at guest[0] in this fixture. */
    d2_vertex_context_prepare(&state);
    reference(&state,0,expected);
    CHECK(d2_vertex_context_read(&state,0,actual) && memcmp(expected,actual,sizeof actual)==0);
    /* Same layout and address, new bytes: active data is always fetched live. */
    guest[0]=0x42;guest[1]=0x28;guest[2]=0;guest[3]=0; /* float32 BE 42 */
    d2_vertex_context_prepare(&state);
    CHECK(d2_vertex_context_read(&state,0,actual) && actual[0].v[0]==42);
    vm_base=NULL;d2_vertex_context_prepare(&state);
    CHECK(!d2_vertex_context_read(&state,0,actual));
    vm_base=guest;d2_vertex_context_prepare(NULL);
    CHECK(!d2_vertex_context_read(&state,0,actual));
    puts("Pinned prepared attributes match baseline byte-for-byte (7200 vertices)");
    return 0;
}
