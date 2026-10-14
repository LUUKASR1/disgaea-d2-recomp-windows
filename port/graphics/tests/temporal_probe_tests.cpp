#include "temporal_probe.h"
#include <cstdio>
#include <stdexcept>
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    auto reads=d2_constant_reads("dcl_constantbuffer cb0[514], immediateIndexed\n"
        "dp4 o0.x, v0, cb0[256]\ndp4 o0.y, v0, cb0[257]\n"
        "dp4 o0.z, v0, cb0[258]\ndp4 o0.w, v0, cb0[259]\nadd r0, cb0[256], cb0[513]\n");
    require(reads.count==5 && !reads.indirect,"Read sets must skip declarations and deduplicate");
    require(reads.used[256]&&reads.used[257]&&reads.used[258]&&reads.used[259]&&reads.used[513],"Matrix and viewport rows retained");
    require(!reads.used[0],"Unreferenced row is not a camera guess");
    auto dynamic=d2_constant_reads("mov r0, cb0[r1.x + 32]\nmov r1, cb0[0]\n");
    require(dynamic.indirect && dynamic.count==1 && dynamic.used[0],"Indirect reads must be reported");
    require(d2_constant_reads(nullptr).count==0,"Missing shader has no invented constants");
    require(d2_constant_reads("mov r0, cb1[12]\n").count==0,"FP bank cannot be classified as VP bank");
    puts("PASS temporal constant discovery: literal/indirect reads, bounds and no matrix assumptions");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
