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
// Empty LOADs have no mapped bytes: retain range/power checks, but do not
// impose address/offset congruence on a zero-size memory image.
std::vector<const char*> load_failures(const Segment &s,uint64_t align) {
    std::vector<const char*> failures;
    if(s.bytes>s.memory) failures.push_back("filesz-exceeds-memsz");
    if(s.address>UINT32_MAX) failures.push_back("address-width");
    else if(s.memory>UINT64_C(0x100000000)-s.address) failures.push_back("address-range");
    if(s.memory>256u*1024u*1024u) failures.push_back("segment-budget");
    if(align>1) {
        if(align&(align-1)) failures.push_back("alignment-not-power-of-two");
        if(s.memory && s.address%align!=s.offset%align) failures.push_back("offset-address-incongruent");
    }
    return failures;
}

}
namespace {
using Read = std::function<size_t(uint64_t,std::span<std::byte>)>;
std::string inspect_elf(uint64_t size,const Read &read,std::string report,const char *success,const Read &payload_read={}) {
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
        unsigned empty_loads=0;
        for(uint64_t i=0;i<count;++i) {
            auto p=ph.data()+i*56;
            if(be(p,4)!=1) continue;
            Segment s{be(p+8,8),be(p+16,8),be(p+32,8),be(p+40,8),unsigned(be(p+4,4))};
            uint64_t align=be(p+48,8);
            if(s.bytes>s.memory || s.offset>size || s.bytes>size-s.offset ||
               s.address>UINT32_MAX || s.memory>UINT64_C(0x100000000)-s.address ||
               s.memory>256u*1024u*1024u || (align>1 && ((align&(align-1)) || (s.memory && s.address%align!=s.offset%align))))
                return fail("load-segment-bounds-or-alignment");
            if(!s.memory) { ++empty_loads; continue; }
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
            if((payload_read ? payload_read : read)(s.offset+(entry-s.address),opd)!=opd.size()) return fail("opd-read-failed");
            code=be(opd.data(),4); toc=be(opd.data()+4,4); opd_found=true; break;
        }
        if(!opd_found || !code || (code&3)) return fail("entry-opd-not-file-backed");
        for(const auto &s:loads) {
            code_found |= (s.flags&1) && code>=s.address && code-s.address<=s.bytes && s.bytes-(code-s.address)>=4;
            toc_found |= (s.flags&4) && toc>=s.address && toc-s.address<s.memory;
        }
        if(!code_found || !toc_found) return fail("entry-code-or-toc-outside-load-plan");
        return report+"executable_empty_load_segments_ignored="+std::to_string(empty_loads)+
            "\nexecutable_load_segments="+std::to_string(loads.size())+
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
    report+="self_layout_diagnostics=public-numeric-fields-v1\nself_elf_declared_bytes="+std::to_string(elfsize)+
        "\nself_container_bytes="+std::to_string(file.Size())+"\nself_header_bytes="+std::to_string(headers)+
        "\nself_program_headers="+std::to_string(count)+"\n";
    unsigned declared_encrypted=0,declared_compressed=0;
    for(uint64_t i=0;i<count;++i) {
        auto p=programs.data()+i*56, s=segments.data()+i*32;
        auto align=be(p+48,8);
        report+="self_segment_"+std::to_string(i)+"=type:"+std::to_string(be(p,4))+
            ";flags:"+std::to_string(be(p+4,4))+";elf_offset:"+std::to_string(be(p+8,8))+
            ";virtual_address:"+std::to_string(be(p+16,8))+";file_bytes:"+std::to_string(be(p+32,8))+
            ";memory_bytes:"+std::to_string(be(p+40,8))+";alignment:"+std::to_string(align)+
            ";offset_mod_alignment:"+std::to_string(align ? be(p+8,8)%align : 0)+
            ";address_mod_alignment:"+std::to_string(align ? be(p+16,8)%align : 0)+
            ";container_offset:"+std::to_string(be(s,8))+";stored_bytes:"+std::to_string(be(s+8,8))+
            ";compression:"+std::to_string(be(s+16,4))+";encryption:"+std::to_string(be(s+24,8))+"\n";
        declared_encrypted+=be(s+24,8)==2; declared_compressed+=be(s+16,4)==2;
    }
    report+="self_declared_encrypted_segments="+std::to_string(declared_encrypted)+
        "\nself_declared_compressed_segments="+std::to_string(declared_compressed)+"\n";
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
    struct Mapping {uint64_t logical,physical,bytes; std::vector<std::byte> decoded{}; int program=-1;};
    struct Compressed {uint64_t logical,physical,stored,bytes; int program;};
    std::vector<Compressed> packed;
    std::vector<Mapping> maps{{0,eh,64},{logicalph,ph,count*56}};
    unsigned encrypted=0, loads=0, empty_loads=0;
    uint64_t total=0;
    std::vector<Segment> loadplans;
    for(uint64_t i=0;i<count;++i) {
        auto p=programs.data()+i*56, s=segments.data()+i*32;
        const auto physical=be(s,8), stored=be(s+8,8), compression=be(s+16,4), encryption=be(s+24,8);
        const auto logical=be(p+8,8), bytes=be(p+32,8);
        auto segment_fail=[&](const char *why) {
            report+="self_failure_segment_index="+std::to_string(i)+"\n";
            return fail(why);
        };
        if(!range(logical,bytes,elfsize)) return segment_fail("self-logical-segment-outside-elf");
        if(!range(physical,stored,file.Size())) return segment_fail("self-stored-segment-outside-container");
        if(stored && physical<headers) return segment_fail("self-stored-segment-inside-headers");
        if(encryption>2) return segment_fail("self-unsupported-encryption-flag");
        if(compression!=1 && compression!=2) return segment_fail("self-unsupported-compression-flag");
        if(encryption==2) ++encrypted;
        if(compression==1 && stored!=bytes) return fail("self-plain-segment-size-mismatch");
        if(be(p,4)==1) {
            Segment plan{logical,be(p+16,8),bytes,be(p+40,8),unsigned(be(p+4,4))};
            auto align=be(p+48,8);
            auto failures=load_failures(plan,align);
            if(!failures.empty()) {
                report+="self_failure_segment_index="+std::to_string(i)+
                    "\nself_failure_group=load-segment-bounds-or-alignment\nself_failure_checks=";
                bool policy_only=true;
                for(size_t f=0;f<failures.size();++f) {
                    if(f) report+=",";
                    report+=failures[f];
                    policy_only &= std::strcmp(failures[f],"segment-budget")==0;
                }
                report+="\nself_failure_class="+std::string(policy_only ? "inspection-budget" : "load-layout")+"\n";
                return report+"executable_status=rejected\nexecutable_reason=self-load-"+failures.front()+"\n";
            }
            if(!plan.memory) { ++empty_loads; continue; }
            for(const auto &prior:loadplans) if(plan.memory && prior.memory &&
                plan.address<prior.address+prior.memory && prior.address<plan.address+plan.memory)
                return fail("self-overlapping-load-segments");
            loadplans.push_back(plan); ++loads; total+=plan.memory;
            if(total>512u*1024u*1024u) return fail("self-load-plan-budget");
        }
        if(bytes && encryption!=2) {
            if(compression==1) maps.push_back({logical,physical,bytes,{},int(i)});
            else packed.push_back({logical,physical,stored,bytes,int(i)});
        }
    }
    if(!loads) return fail("self-no-load-segments");
    report+="self_empty_load_segments_ignored="+std::to_string(empty_loads)+
        "\nself_load_segments="+std::to_string(loads)+"\nself_public_headers=structurally-valid\n";
    if(encrypted) return report+"self_battery_status=not-run-encryption-required\nexecutable_status=self-decryption-required\n";
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
        maps.push_back({part.logical,0,part.bytes,std::move(output),part.program});
    }
    report+="self_decompressed_segments="+std::to_string(packed.size())+"\n";
    // Multiple public records can describe the same logical ELF bytes: ELF
    // headers inside the first LOAD, or TLS/process metadata inside a LOAD.
    // Accept only identical backing or byte-for-byte matching overlap. Check
    // every overlapping pair, including non-adjacent nested regions.
    auto mapping_read=[&](const Mapping &m,uint64_t logical,std::span<std::byte> out)->bool {
        if(logical<m.logical || !range(logical-m.logical,out.size(),m.bytes)) return false;
        if(!m.decoded.empty()) {
            std::memcpy(out.data(),m.decoded.data()+(logical-m.logical),out.size()); return true;
        }
        try { return file.ReadAt(m.physical+(logical-m.logical),out)==out.size(); }
        catch(const d2::vfs::Error&) { return false; }
    };
    // Independent read-only probes run before strict overlap acceptance.
    // Their results never turn a conflicting strict plan into an accepted one.
    auto battery=[&]() {
        std::string out="self_battery_scope=independent-read-only-public-layout-and-payload-probes\n";
        uint64_t used=0,calls=0; unsigned checks=0; bool limited=false;
        auto sample=[&](const Mapping &m,uint64_t logical,std::span<std::byte> dest) {
            if(logical<m.logical || !range(logical-m.logical,dest.size(),m.bytes)) return false;
            if(calls>=512 || dest.size()>2u*1024u*1024u-used) {limited=true;return false;}
            ++calls; used+=dest.size(); return mapping_read(m,logical,dest);
        };
        auto program_map=[&](int index)->const Mapping* {
            for(const auto &m:maps) if(m.program==index) return &m;
            return nullptr;
        };
        auto logical_source=[&](uint64_t off,uint64_t n)->const Mapping* {
            for(const auto &m:maps) if(m.program>=0 && be(programs.data()+m.program*56,4)==1 &&
                off>=m.logical && range(off-m.logical,n,m.bytes)) return &m;
            return nullptr;
        };
        Read payload=[&](uint64_t offset,std::span<std::byte> dest)->size_t {
            if(!range(offset,dest.size(),elfsize)) return 0;
            size_t done=0;
            while(done<dest.size()) {
                auto m=logical_source(offset,1); if(!m) return done;
                auto n=std::min<uint64_t>(dest.size()-done,m->bytes-(offset-m->logical));
                if(!sample(*m,offset,dest.subspan(done,n))) return done;
                done+=n;offset+=n;
            }
            return done;
        };
        auto guest=[&](uint64_t address,std::span<std::byte> dest,unsigned flags)->bool {
            for(uint64_t i=0;i<count;++i) {
                auto p=programs.data()+i*56; auto base=be(p+16,8),bytes=be(p+32,8);
                if(be(p,4)!=1 || (be(p+4,4)&flags)!=flags || address<base || !range(address-base,dest.size(),bytes)) continue;
                auto m=program_map(i); return m && sample(*m,m->logical+(address-base),dest);
            }
            return false;
        };
        auto unavailable=[&]() {return limited ? std::string("limited") : std::string("unreadable-or-unmapped");};
        auto probe=[&](const char *name,auto run) {
            ++checks;
            try {out+=std::string(name)+"="+run()+"\n";}
            catch(...) {out+=std::string(name)+"=read-error\n";}
        };
        auto compare_copy=[&](uint64_t logical,std::span<const std::byte> expected,const char *prefix) {
            auto m=logical_source(logical,expected.size()); if(!m) return std::string("not-file-backed");
            std::vector<std::byte> actual(expected.size());
            if(!sample(*m,logical,actual)) return unavailable();
            uint64_t different=0,first=0;
            for(size_t i=0;i<expected.size();++i) if(actual[i]!=expected[i]) {if(!different) first=logical+i;++different;}
            out+=std::string(prefix)+"_different_bytes="+std::to_string(different)+"\n";
            if(different) out+=std::string(prefix)+"_first_difference_offset="+std::to_string(first)+"\n";
            out+=std::string(prefix)+"_source_program="+std::to_string(m->program)+"\n";
            if(logical==0 && expected.size()==64) {
                const bool zero=std::all_of(actual.begin(),actual.end(),[](auto v){return v==std::byte{0};});
                const char *hint=zero ? "all-zero" : !std::memcmp(actual.data(),"\x7f" "ELF",4) ? "elf-magic" :
                    !std::memcmp(actual.data(),"SCE\0",4) ? "sce-magic" : "other";
                out+="self_probe_payload_header_hint="+std::string(hint)+"\n";
                // Nine field groups: ident/type+machine/version/entry/phoff/shoff/
                // flags/ehsize+phentsize+phnum/shentsize+shnum+shstrndx.
                const unsigned boundaries[]={0,16,20,24,32,40,48,52,58,64};
                uint64_t mask=0;
                for(unsigned i=0;i<9;++i) if(std::memcmp(actual.data()+boundaries[i],expected.data()+boundaries[i],boundaries[i+1]-boundaries[i])) mask|=uint64_t{1}<<i;
                out+="self_probe_header_difference_mask="+std::to_string(mask)+"\n";
            }
            return std::string(different ? "different" : "identical");
        };
        probe("self_probe_header_copy",[&]{return compare_copy(0,elf,"self_probe_header");});
        probe("self_probe_program_table_copy",[&]{return compare_copy(logicalph,programs,"self_probe_program_table");});
        probe("self_probe_load_samples",[&]{
            unsigned tested=0,zero_heads=0;
            for(uint64_t i=0;i<count;++i) {
                auto p=programs.data()+i*56; auto bytes=be(p+32,8);
                if(be(p,4)!=1 || !bytes) continue;
                auto m=program_map(i); if(!m) return std::string("missing-payload");
                std::array<std::byte,256> first{},last{}; auto n=std::min<uint64_t>(bytes,first.size());
                if(!sample(*m,m->logical,std::span(first).first(n)) ||
                   !sample(*m,m->logical+bytes-n,std::span(last).first(n))) return unavailable();
                zero_heads+=std::all_of(first.begin(),first.begin()+n,[](auto b){return b==std::byte{0};});++tested;
            }
            out+="self_probe_load_samples_checked="+std::to_string(tested)+"\nself_probe_load_zero_heads="+std::to_string(zero_heads)+"\n";
            return std::string("readable");
        });
        auto opd=[&](uint64_t &code,uint64_t &toc) {
            auto entry=be(e+24,8); if(entry>UINT32_MAX || (entry&3)) return false;
            std::array<std::byte,8> value{}; if(!guest(entry,value,4)) return false;
            code=be(value.data(),4);toc=be(value.data()+4,4);return true;
        };
        probe("self_probe_entry_opd",[&]{
            uint64_t code=0,toc=0; if(!opd(code,toc)) return unavailable();
            out+="self_probe_entry_address="+std::to_string(be(e+24,8))+"\nself_probe_code_address="+std::to_string(code)+
                "\nself_probe_toc_address="+std::to_string(toc)+"\n";return std::string("readable");
        });
        probe("self_probe_entry_code",[&]{
            uint64_t code=0,toc=0; if(!opd(code,toc)) return std::string("unavailable-opd");
            if(!code || (code&3)) return std::string("invalid-code-pointer");
            std::array<std::byte,4> instruction{}; return guest(code,instruction,1) ? std::string("file-backed-executable") : unavailable();
        });
        probe("self_probe_entry_toc",[&]{
            uint64_t code=0,toc=0; if(!opd(code,toc)) return std::string("unavailable-opd");
            for(const auto &load:loadplans) if((load.flags&4) && toc>=load.address && toc-load.address<load.memory) {
                if(toc-load.address>=load.bytes) return std::string("mapped-bss");
                std::array<std::byte,1> value{}; return guest(toc,value,4) ? std::string("mapped-file-readable") : unavailable();
            }
            return std::string("outside-readable-loads");
        });
        probe("self_probe_tls",[&]{
            unsigned found=0; uint64_t index=0;
            for(uint64_t i=0;i<count;++i) if(be(programs.data()+i*56,4)==7) {++found;index=i;}
            if(!found) return std::string("absent"); if(found!=1) return std::string("multiple-templates");
            auto p=programs.data()+index*56; auto bytes=be(p+32,8),memory=be(p+40,8),align=be(p+48,8),address=be(p+16,8);
            out+="self_probe_tls_file_bytes="+std::to_string(bytes)+"\nself_probe_tls_memory_bytes="+std::to_string(memory)+"\n";
            if(bytes>memory || address>UINT32_MAX || memory>UINT64_C(0x100000000)-address || (align>1 && (align&(align-1)))) return std::string("invalid-layout");
            if(!bytes) return std::string("bss-only-template");
            if(bytes>64u*1024u) return std::string("template-limit");
            auto m=program_map(index); if(!m) return std::string("missing-payload");
            std::vector<std::byte> own(bytes),loaded(bytes);
            if(!sample(*m,m->logical,own) || !guest(address,loaded,4)) return unavailable();
            return std::string(own==loaded ? "file-template-consistent" : "template-differs-from-load");
        });
        probe("self_probe_process_records",[&]{
            unsigned found=0;
            for(uint64_t i=0;i<count;++i) {
                auto p=programs.data()+i*56; auto type=be(p,4),bytes=be(p+32,8);
                if(type!=0x60000001 && type!=0x60000002) continue;
                if(bytes>4096) return std::string("record-limit");
                auto m=program_map(i); if(!m && bytes) return std::string("missing-payload");
                std::vector<std::byte> own(bytes),loaded(bytes);
                if(bytes && (!sample(*m,m->logical,own) || !guest(be(p+16,8),loaded,4))) return unavailable();
                if(own!=loaded) return std::string("record-differs-from-load");++found;
            }
            out+="self_probe_process_records_checked="+std::to_string(found)+"\n";
            return std::string(found ? "file-records-consistent" : "absent");
        });
        probe("self_probe_read_bounds",[&]{
            for(const auto &m:maps) if(m.program>=0 && m.bytes) {
                std::array<std::byte,1> sentinel{std::byte{0xa5}};
                if(sample(m,m.logical+m.bytes,sentinel) || sentinel[0]!=std::byte{0xa5}) return std::string("failed");
                if(!sample(m,m.logical+m.bytes,std::span(sentinel).first(0))) return unavailable();
                return std::string("passed");
            }
            return std::string("no-file-backed-segment");
        });
        probe("self_probe_split_view_plan",[&]{
            Read metadata=[&](uint64_t off,std::span<std::byte> dest)->size_t {
                if(range(off,dest.size(),64)) {std::memcpy(dest.data(),elf.data()+off,dest.size());return dest.size();}
                if(off>=logicalph && range(off-logicalph,dest.size(),programs.size())) {
                    std::memcpy(dest.data(),programs.data()+off-logicalph,dest.size());return dest.size();
                }
                return payload(off,dest);
            };
            auto result=inspect_elf(elfsize,metadata,"","split-view-plan-valid",payload);
            if(result.find("executable_status=split-view-plan-valid\n")!=std::string::npos) return std::string("structurally-valid-diagnostic-only");
            const std::string key="executable_reason=";auto pos=result.find(key);
            if(pos!=std::string::npos) {auto end=result.find('\n',pos);out+="self_probe_split_failure_reason="+result.substr(pos+key.size(),end-pos-key.size())+"\n";}
            return limited ? std::string("limited") : std::string("rejected");
        });
        out+="self_battery_checks_run="+std::to_string(checks)+"\nself_battery_read_bytes="+std::to_string(used)+
            "\nself_battery_read_calls="+std::to_string(calls)+"\nself_battery_limit_reached="+(limited ? std::string("true") : std::string("false"))+
            "\nself_battery_status=completed\n";
        return out;
    };
    report+=battery();
    std::stable_sort(maps.begin(),maps.end(),[](auto &a,auto &b){return a.logical<b.logical;});
    uint64_t compared=0, alias_regions=0, compared_regions=0;
    constexpr uint64_t overlap_budget=4u*1024u*1024u;
    std::array<std::byte,4096> left{},right{};
    for(size_t i=0;i<maps.size();++i) for(size_t j=i+1;j<maps.size();++j) {
        const auto &a=maps[i], &b=maps[j];
        if(b.logical>=a.logical+a.bytes) break;
        uint64_t begin=b.logical, end=std::min(a.logical+a.bytes,b.logical+b.bytes);
        if(a.decoded.empty() && b.decoded.empty() &&
           a.physical+(begin-a.logical)==b.physical+(begin-b.logical)) {
            ++alias_regions; continue;
        }
        if(end-begin>overlap_budget-compared) return fail("self-overlap-validation-budget");
        for(uint64_t pos=begin;pos<end;) {
            auto n=std::min<uint64_t>(left.size(),end-pos);
            if(!mapping_read(a,pos,std::span(left).first(n)) ||
               !mapping_read(b,pos,std::span(right).first(n))) return fail("self-overlap-read-failed");
            if(std::memcmp(left.data(),right.data(),n)) {
                report+="self_overlap_left_mapping_index="+std::to_string(i)+
                    "\nself_overlap_right_mapping_index="+std::to_string(j)+"\n";
                return fail("self-plain-overlap-content-mismatch");
            }
            compared+=n; pos+=n;
        }
        ++compared_regions;
    }
    report+="self_overlap_alias_regions="+std::to_string(alias_regions)+
        "\nself_overlap_compared_regions="+std::to_string(compared_regions)+
        "\nself_overlap_compared_bytes="+std::to_string(compared)+"\nself_overlap_validation=passed\n";
    Read read=[&](uint64_t offset,std::span<std::byte> out)->size_t {
        if(!range(offset,out.size(),elfsize)) return 0;
        size_t copied=0;
        while(copied<out.size()) {
            auto it=std::find_if(maps.begin(),maps.end(),[&](auto &m){return offset>=m.logical && offset-m.logical<m.bytes;});
            if(it==maps.end()) return copied;
            auto n=std::min<uint64_t>(out.size()-copied,it->bytes-(offset-it->logical));
            if(!mapping_read(*it,offset,out.subspan(copied,n))) return copied;
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
