#include "executable_probe.h"
#include <array>
#include <vector>
#include <cstring>
#include <algorithm>
#include <functional>
#include <zlib.h>
namespace {
uint64_t be(const std::byte *p,unsigned n) {
    uint64_t v=0; for(unsigned i=0;i<n;++i) v=(v<<8)|std::to_integer<unsigned char>(p[i]); return v;
}
struct Segment { uint64_t offset,address,bytes,memory; unsigned flags; };
}
namespace {
using Read = std::function<size_t(uint64_t,std::span<std::byte>)>;
std::string inspect_elf(uint64_t size,const Read &read,std::string report,const char *success) {
    auto fail=[&](const char *why) { return report+"executable_status=rejected\nexecutable_reason="+why+"\n"; };
    std::array<std::byte,64> header{};
    size_t head_size=std::min<uint64_t>(size,header.size());
    if(read(0,std::span(header).first(head_size))!=head_size) return fail("header-read-failed");
        if(head_size!=64) return fail("truncated-elf-header");
        auto h=header.data();
        if(h[4]!=std::byte{2} || h[5]!=std::byte{2} || h[6]!=std::byte{1} ||
           be(h+16,2)!=2 || be(h+18,2)!=21 || be(h+20,4)!=1 || be(h+52,2)!=64)
            return fail("requires-elf64-big-endian-ppc64-executable");
        const uint64_t entry=be(h+24,8), table=be(h+32,8), count=be(h+56,2);
        if(!count || count>128 || be(h+54,2)!=56 || table<64 || table>size ||
           count*56>size-table) return fail("program-table-bounds");
        std::vector<std::byte> ph(count*56);
        if(read(table,ph)!=ph.size()) return fail("program-table-read-failed");
        std::vector<Segment> loads;
        uint64_t total=0;
        for(uint64_t i=0;i<count;++i) {
            auto p=ph.data()+i*56;
            if(be(p,4)!=1) continue;
            Segment s{be(p+8,8),be(p+16,8),be(p+32,8),be(p+40,8),unsigned(be(p+4,4))};
            uint64_t align=be(p+48,8);
            if(s.bytes>s.memory || s.offset>size || s.bytes>size-s.offset ||
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
            if(read(s.offset+(entry-s.address),opd)!=opd.size()) return fail("opd-read-failed");
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
            "\nexecutable_entry_opd=structurally-valid\nexecutable_status="+success+"\n";
}
}
namespace {
bool range(uint64_t offset,uint64_t bytes,uint64_t size) {
    return offset<=size && bytes<=size-offset;
}
std::string inspect_self(const d2::vfs::IsoVfs::File &file,std::string report) {
    report+="executable_format=self-container\nself_scope=bounded-public-headers-and-plain-segment-plan\n"
        "self_signature_verified=false\nself_metadata_decrypted=false\n";
    auto fail=[&](const char *why) { return report+"executable_status=rejected\nexecutable_reason="+why+"\n"; };
    // Independent parser of the public SCE/SELF layout. No RPCS3 code linked.
    // Only read declared public structures; private metadata is never decoded.
    std::array<std::byte,112> head{};
    if(file.ReadAt(0,head)!=head.size()) return fail("truncated-self-header");
    auto h=head.data();
    const auto version=be(h+4,4), revision=be(h+8,2), kind=be(h+10,2);
    report+="self_version="+std::to_string(version)+"\nself_revision="+std::to_string(revision)+"\n";
    if(version!=2 || kind!=1 || be(h+32,8)!=3)
        return report+"executable_status=self-profile-unsupported\n";
    const uint64_t headers=be(h+16,8), elfsize=be(h+24,8), metadata=be(h+12,4)+uint64_t{32};
    if(headers<112 || headers>1024u*1024u || headers>file.Size() ||
       elfsize<64 || elfsize>512u*1024u*1024u || !range(metadata,64,headers))
        return fail("self-header-bounds-or-budget");
    const auto app=be(h+40,8), eh=be(h+48,8), ph=be(h+56,8), seg=be(h+72,8);
    const auto controls=be(h+88,8), controlsize=be(h+96,8);
    auto public_range=[&](uint64_t off,uint64_t bytes) {return off>=112 && range(off,bytes,headers);};
    if(!public_range(app,32) || !public_range(eh,64) || controlsize>64u*1024u ||
       (controlsize && !public_range(controls,controlsize))) return fail("self-public-table-bounds");
    std::array<std::byte,32> application{};
    std::array<std::byte,64> elf{};
    if(file.ReadAt(app,application)!=application.size() || file.ReadAt(eh,elf)!=elf.size())
        return fail("self-public-header-read-failed");
    report+="self_application_type="+std::to_string(be(application.data()+12,4))+"\n";
    auto e=elf.data();
    if(std::memcmp(e,"\x7f" "ELF",4) || e[4]!=std::byte{2} || e[5]!=std::byte{2} ||
       e[6]!=std::byte{1} || be(e+16,2)!=2 || be(e+18,2)!=21 || be(e+20,4)!=1 || be(e+52,2)!=64)
        return fail("self-requires-elf64-big-endian-ppc64-executable");
    const auto count=be(e+56,2), logicalph=be(e+32,8);
    if(!count || count>128 || be(e+54,2)!=56 || logicalph<64 ||
       !range(logicalph,count*56,elfsize) || !public_range(ph,count*56) || !public_range(seg,count*32))
        return fail("self-program-or-segment-table-bounds");
    std::vector<std::byte> programs(count*56), segments(count*32);
    if(file.ReadAt(ph,programs)!=programs.size() || file.ReadAt(seg,segments)!=segments.size())
        return fail("self-table-read-failed");
    // Supplemental chain is bounded independently of untrusted next flags.
    uint64_t cursor=controls, end=controls+controlsize;
    unsigned controlcount=0; bool npd=false;
    while(cursor<end) {
        if(++controlcount>128 || end-cursor<16) return fail("self-control-chain-bounds");
        std::array<std::byte,32> c{};
        if(file.ReadAt(cursor,std::span(c).first(16))!=16) return fail("self-control-read-failed");
        auto type=be(c.data(),4), bytes=be(c.data()+4,4), next=be(c.data()+8,8);
        if(bytes<16 || bytes>end-cursor || next>1 || ((next==0)!=(bytes==end-cursor)))
            return fail("self-control-chain-bounds");
        if(type==3) {
            if(npd || bytes!=144 || file.ReadAt(cursor+16,std::span(c).first(16))!=16 ||
               std::memcmp(c.data(),"NPD\0",4)) return fail("self-npd-header-invalid");
            npd=true;
            const auto license=be(c.data()+8,4);
            report+="self_npd_license_type="+std::to_string(license)+"\n";
        }
        cursor+=bytes;
    }
    struct Mapping {uint64_t logical,physical,bytes; std::vector<std::byte> decoded{};};
    struct Compressed {uint64_t logical,physical,stored,bytes;};
    std::vector<Compressed> packed;
    std::vector<Mapping> maps{{0,eh,64},{logicalph,ph,count*56}};
    unsigned encrypted=0, compressed=0, loads=0;
    uint64_t total=0;
    std::vector<Segment> loadplans;
    for(uint64_t i=0;i<count;++i) {
        auto p=programs.data()+i*56, s=segments.data()+i*32;
        const auto physical=be(s,8), stored=be(s+8,8), compression=be(s+16,4), encryption=be(s+24,8);
        const auto logical=be(p+8,8), bytes=be(p+32,8);
        if(!range(logical,bytes,elfsize) || !range(physical,stored,file.Size()) ||
           (stored && physical<headers) || encryption>2 || (compression!=1 && compression!=2))
            return fail("self-segment-bounds-or-flags");
        if(encryption==2) ++encrypted;
        if(compression==2) ++compressed;
        if(compression==1 && stored!=bytes) return fail("self-plain-segment-size-mismatch");
        if(be(p,4)==1) {
            Segment plan{logical,be(p+16,8),bytes,be(p+40,8),unsigned(be(p+4,4))};
            auto align=be(p+48,8);
            if(bytes>plan.memory || plan.address>UINT32_MAX || plan.memory>UINT64_C(0x100000000)-plan.address ||
               plan.memory>256u*1024u*1024u || (align>1 && ((align&(align-1)) || plan.address%align!=logical%align)))
                return fail("self-load-segment-bounds-or-alignment");
            for(const auto &prior:loadplans) if(plan.memory && prior.memory &&
                plan.address<prior.address+prior.memory && prior.address<plan.address+plan.memory)
                return fail("self-overlapping-load-segments");
            loadplans.push_back(plan); ++loads; total+=plan.memory;
            if(total>512u*1024u*1024u) return fail("self-load-plan-budget");
        }
        if(bytes && encryption!=2) {
            if(compression==1) maps.push_back({logical,physical,bytes});
            else packed.push_back({logical,physical,stored,bytes});
        }
    }
    if(!loads) return fail("self-no-load-segments");
    report+="self_program_headers="+std::to_string(count)+"\nself_load_segments="+std::to_string(loads)+
        "\nself_declared_encrypted_segments="+std::to_string(encrypted)+
        "\nself_declared_compressed_segments="+std::to_string(compressed)+
        "\nself_public_headers=structurally-valid\n";
    if(encrypted) return report+"executable_status=self-decryption-required\n";
    // zlib is an Android/host system library, not a downloaded runtime.
    // Strict input/output caps and exact stream termination bound inflate.
    uint64_t budget=0;
    for(const auto &part:packed) {
        if(!part.stored || part.bytes>16u*1024u*1024u || part.stored>16u*1024u*1024u ||
           part.bytes+part.stored>24u*1024u*1024u-budget)
            return fail("self-decompression-budget");
        budget+=part.bytes+part.stored;
        std::vector<std::byte> input(part.stored), output(part.bytes+1);
        if(file.ReadAt(part.physical,input)!=input.size()) return fail("self-compressed-read-failed");
        z_stream stream{};
        stream.next_in=reinterpret_cast<Bytef*>(input.data()); stream.avail_in=input.size();
        stream.next_out=reinterpret_cast<Bytef*>(output.data()); stream.avail_out=output.size();
        if(inflateInit(&stream)!=Z_OK) return fail("self-inflate-init-failed");
        int result=inflate(&stream,Z_FINISH);
        bool valid=result==Z_STREAM_END && stream.total_in==part.stored && stream.total_out==part.bytes;
        inflateEnd(&stream);
        if(!valid) return fail("self-invalid-compressed-segment");
        output.resize(part.bytes);
        maps.push_back({part.logical,0,part.bytes,std::move(output)});
    }
    report+="self_decompressed_segments="+std::to_string(packed.size())+"\n";
    // Plain-only virtual ELF view. No output file, guest VM or executable code.
    // Overlapping logical regions are ambiguous and deliberately unsupported.
    std::sort(maps.begin(),maps.end(),[](auto &a,auto &b){return a.logical<b.logical;});
    for(size_t i=1;i<maps.size();++i)
        if(maps[i].logical<maps[i-1].logical+maps[i-1].bytes)
            return fail("self-plain-logical-overlap");
    Read read=[&](uint64_t offset,std::span<std::byte> out)->size_t {
        if(!range(offset,out.size(),elfsize)) return 0;
        size_t copied=0;
        while(copied<out.size()) {
            auto it=std::find_if(maps.begin(),maps.end(),[&](auto &m){return offset>=m.logical && offset-m.logical<m.bytes;});
            if(it==maps.end()) return copied;
            auto n=std::min<uint64_t>(out.size()-copied,it->bytes-(offset-it->logical));
            if(!it->decoded.empty())
                std::memcpy(out.data()+copied,it->decoded.data()+(offset-it->logical),n);
            else if(file.ReadAt(it->physical+(offset-it->logical),out.subspan(copied,n))!=n) return copied;
            copied+=n; offset+=n;
        }
        return copied;
    };
    return inspect_elf(elfsize,read,report,"self-plain-plan-valid");
}
}
std::string d2_inspect_executable(const d2::vfs::IsoVfs::File &file) {
    std::string report="executable_scope=bounded-self-public-headers-and-elf-load-plan\n"
        "executable_code_executed=false\nexecutable_guest_memory_written=false\n";
    try {
        std::array<std::byte,4> magic{};
        auto got=file.ReadAt(0,magic);
        if(got==4 && !std::memcmp(magic.data(),"SCE\0",4)) return inspect_self(file,report);
        if(got==4 && !std::memcmp(magic.data(),"\x7f" "ELF",4))
            return inspect_elf(file.Size(),[&](auto offset,auto out){return file.ReadAt(offset,out);},
                report+"executable_format=elf\n","elf-plan-valid");
        return report+"executable_format=unknown\nexecutable_status=unrecognized\n";
    } catch(const d2::vfs::Error&) { return report+"executable_status=rejected\nexecutable_reason=read-error\n"; }
}
