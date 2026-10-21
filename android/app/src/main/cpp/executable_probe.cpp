#include "executable_probe.h"
#include <array>
#include <vector>
#include <cstring>
#include <algorithm>
namespace {
uint64_t be(const std::byte *p,unsigned n) {
    uint64_t v=0; for(unsigned i=0;i<n;++i) v=(v<<8)|std::to_integer<unsigned char>(p[i]); return v;
}
struct Segment { uint64_t offset,address,bytes,memory; unsigned flags; };
}
std::string d2_inspect_executable(const d2::vfs::IsoVfs::File &file) {
    std::string report="executable_scope=bounded-header-and-elf-load-plan\n"
        "executable_code_executed=false\nexecutable_guest_memory_written=false\n";
    auto fail=[&](const char *why) { return report+"executable_status=rejected\nexecutable_reason="+why+"\n"; };
    try {
        std::array<std::byte,64> header{};
        size_t head_size=std::min<uint64_t>(file.Size(),header.size());
        if(file.ReadAt(0,std::span(header).first(head_size))!=head_size) return fail("header-read-failed");
        if(head_size>=4 && !std::memcmp(header.data(),"SCE\0",4)) {
            // Magic identifies the container; it does not prove encryption or
            // that a complete/valid SELF can be loaded by the current runtime.
            return report+"executable_format=self-container\nexecutable_status=self-loader-required\n";
        }
        if(head_size<4 || std::memcmp(header.data(),"\x7f" "ELF",4))
            return report+"executable_format=unknown\nexecutable_status=unrecognized\n";
        report+="executable_format=elf\n";
        if(head_size!=64) return fail("truncated-elf-header");
        auto h=header.data();
        if(h[4]!=std::byte{2} || h[5]!=std::byte{2} || h[6]!=std::byte{1} ||
           be(h+16,2)!=2 || be(h+18,2)!=21 || be(h+20,4)!=1 || be(h+52,2)!=64)
            return fail("requires-elf64-big-endian-ppc64-executable");
        const uint64_t entry=be(h+24,8), table=be(h+32,8), count=be(h+56,2);
        if(!count || count>128 || be(h+54,2)!=56 || table<64 || table>file.Size() ||
           count*56>file.Size()-table) return fail("program-table-bounds");
        std::vector<std::byte> ph(count*56);
        if(file.ReadAt(table,ph)!=ph.size()) return fail("program-table-read-failed");
        std::vector<Segment> loads;
        uint64_t total=0;
        for(uint64_t i=0;i<count;++i) {
            auto p=ph.data()+i*56;
            if(be(p,4)!=1) continue;
            Segment s{be(p+8,8),be(p+16,8),be(p+32,8),be(p+40,8),unsigned(be(p+4,4))};
            uint64_t align=be(p+48,8);
            if(s.bytes>s.memory || s.offset>file.Size() || s.bytes>file.Size()-s.offset ||
               s.address>UINT32_MAX || s.memory>UINT64_C(0x100000000)-s.address ||
               s.memory>256u*1024u*1024u || (align>1 && ((align&(align-1)) || s.address%align!=s.offset%align)))
                return fail("load-segment-bounds-or-alignment");
            for(const auto &prior:loads) if(s.memory && prior.memory &&
                s.address<prior.address+prior.memory && prior.address<s.address+s.memory)
                return fail("overlapping-load-segments");
            total+=s.memory; if(total>512u*1024u*1024u) return fail("load-plan-budget");
            loads.push_back(s);
        }
        if(loads.empty()) return fail("no-load-segments");
        // PS3 ELFv1 entry points to an OPD with two big-endian 32-bit words.
        bool opd_found=false,code_found=false,toc_found=false;
        uint64_t code=0,toc=0;
        if(entry>UINT32_MAX || (entry&3)) return fail("invalid-entry-opd");
        for(const auto &s:loads) if((s.flags&4) && entry>=s.address && entry-s.address<=s.bytes &&
            s.bytes-(entry-s.address)>=8) {
            std::array<std::byte,8> opd{};
            if(file.ReadAt(s.offset+(entry-s.address),opd)!=opd.size()) return fail("opd-read-failed");
            code=be(opd.data(),4); toc=be(opd.data()+4,4); opd_found=true; break;
        }
        if(!opd_found || !code || (code&3)) return fail("entry-opd-not-file-backed");
        for(const auto &s:loads) {
            code_found |= (s.flags&1) && code>=s.address && code-s.address<=s.bytes && s.bytes-(code-s.address)>=4;
            toc_found |= (s.flags&4) && toc>=s.address && toc-s.address<s.memory;
        }
        if(!code_found || !toc_found) return fail("entry-code-or-toc-outside-load-plan");
        return report+"executable_load_segments="+std::to_string(loads.size())+
            "\nexecutable_planned_memory_bytes="+std::to_string(total)+
            "\nexecutable_entry_opd=structurally-valid\nexecutable_status=elf-plan-valid\n";
    } catch(const d2::vfs::Error&) { return fail("read-error"); }
}
