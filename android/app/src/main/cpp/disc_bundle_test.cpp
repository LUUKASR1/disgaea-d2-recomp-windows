// Own synthetic ECMA-119 image: no game bytes, code or assets.
#include "file_services.h"
#include "dispatch_probe.h"
#include "document_probe.h"
#include "runtime/syscalls/lv2_syscall_table.h"
#include <array>
#include <algorithm>
#include <zlib.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include <initializer_list>
#include <fcntl.h>
#include <unistd.h>
namespace {
constexpr uint32_t path_addr=0x21000, fd_addr=0x21200, data_addr=0x22000, count_addr=0x21400;
constexpr size_t block=2048;
void need(bool ok,const char *why) { if(!ok) throw std::runtime_error(why); }
void le(unsigned char *p,uint64_t v,unsigned n) { for(unsigned i=0;i<n;++i) p[i]=v>>(8*i); }
void be_write(unsigned char *p,uint64_t v,unsigned n) { for(unsigned i=0;i<n;++i) p[i]=v>>(8*(n-1-i)); }
void both(unsigned char *p,uint32_t v,unsigned n) {
    le(p,v,n); for(unsigned i=0;i<n;++i) p[n+i]=v>>(8*(n-1-i));
}
size_t record(unsigned char *p,const std::string &name,unsigned lba,unsigned size,bool directory=false) {
    size_t n=33+name.size()+(name.size()%2==0); p[0]=n;
    both(p+2,lba,4); both(p+10,size,4); p[25]=directory ? 2 : 0;
    both(p+28,1,2); p[32]=name.size(); std::memcpy(p+33,name.data(),name.size()); return n;
}
std::vector<unsigned char> image() {
    std::vector<unsigned char> bytes(64*block);
    auto at=[&](unsigned sector) { return bytes.data()+sector*block; };
    auto p=at(16); p[0]=1; std::memcpy(p+1,"CD001",5); p[6]=1;
    both(p+80,64,4); both(p+120,1,2); both(p+124,1,2); both(p+128,block,2);
    record(p+156,std::string(1,'\0'),20,block,true);
    p=at(17); p[0]=255; std::memcpy(p+1,"CD001",5); p[6]=1;
    for(unsigned sector=20;sector<=22;++sector) {
        p=at(sector); size_t n=record(p,std::string(1,'\0'),sector,block,true);
        n+=record(p+n,std::string(1,'\1'),sector==20 ? 20 : sector-1,block,true);
        if(sector==20) record(p+n,"PS3_GAME",21,block,true);
        if(sector==21) { n+=record(p+n,"PARAM.SFO;1",24,58); record(p+n,"USRDIR",22,block,true); }
        if(sector==22) { n+=record(p+n,"FIXTURE.BIN;1",25,4099); n+=record(p+n,"EMPTY.BIN;1",0,0); record(p+n,"EBOOT.BIN;1",30,544); }
    }
    p=at(24); std::memcpy(p,"\0PSF",4); le(p+4,0x101,4); le(p+8,36,4); le(p+12,48,4); le(p+16,1,4);
    le(p+22,0x0204,2); le(p+24,10,4); le(p+28,16,4);
    std::memcpy(p+36,"TITLE_ID",9); std::memcpy(p+48,"BLUS31313",10);
    for(unsigned i=0;i<4099;++i) at(25)[i]=static_cast<unsigned char>((i*29+11)&255);
    p=at(30); std::memcpy(p,"\x7f" "ELF\x02\x02\x01",7);
    be_write(p+16,2,2); be_write(p+18,21,2); be_write(p+20,1,4);
    be_write(p+24,0x20000,8); be_write(p+32,64,8); be_write(p+52,64,2);
    be_write(p+54,56,2); be_write(p+56,2,2);
    for(unsigned i=0;i<2;++i) {
        auto ph=p+64+56*i; be_write(ph,1,4); be_write(ph+4,i ? 6 : 5,4);
        be_write(ph+8,i ? 512 : 256,8); be_write(ph+16,i ? 0x20000 : 0x10000,8);
        be_write(ph+32,i ? 32 : 8,8); be_write(ph+40,i ? 64 : 8,8); be_write(ph+48,4,8);
    }
    be_write(p+512,0x10000,4); be_write(p+516,0x20010,4);
    return bytes;
}
// Compact public-layout SELF containing the same two synthetic ELF segments.
std::vector<unsigned char> self_image(const std::vector<unsigned char> &base,unsigned encryption) {
    auto bytes=base;
    auto p=bytes.data()+30*block;
    std::array<unsigned char,544> elf{}; std::memcpy(elf.data(),p,elf.size());
    std::memset(p,0,1024);
    std::memcpy(p,"SCE\0",4); be_write(p+4,2,4); be_write(p+10,1,2);
    be_write(p+12,416,4); be_write(p+16,512,8); be_write(p+24,544,8);
    be_write(p+32,3,8); be_write(p+40,112,8); be_write(p+48,144,8);
    be_write(p+56,208,8); be_write(p+72,320,8); be_write(p+124,4,4);
    std::memcpy(p+144,elf.data(),64); std::memcpy(p+208,elf.data()+64,112);
    for(unsigned i=0;i<2;++i) {
        auto record=p+320+i*32;
        be_write(record,i ? 768 : 512,8); be_write(record+8,i ? 32 : 8,8);
        be_write(record+16,1,4); be_write(record+24,encryption,8);
    }
    std::memcpy(p+512,elf.data()+256,8); std::memcpy(p+768,elf.data()+512,32);
    const char marker[]="EBOOT.BIN;1";
    auto it=std::search(bytes.begin()+22*block,bytes.begin()+23*block,marker,marker+sizeof(marker)-1);
    need(it!=bytes.begin()+23*block,"self fixture directory record");
    both(&*it-33+10,800,4);
    return bytes;
}
uint64_t call(uint64_t number,std::initializer_list<uint64_t> values={}) {
    std::array<uint64_t,8> args{}; size_t i=0; for(auto v:values) args.at(i++)=v;
    uint64_t result=0; need(!d2_fixture_syscall(number,args.data(),1,&result),"disc bridge fault"); return result;
}
bool err(uint64_t value,uint32_t code) { return static_cast<uint32_t>(value)==code; }
void path(const char *value) {
    for(size_t i=0;i<=std::strlen(value);++i) d2_lift_write8(path_addr+i,value[i]);
}
uint32_t open_file(const char *name) {
    path(name); need(call(SYS_FS_OPEN,{path_addr,0,fd_addr})==0,"disc guest open"); return d2_lift_read32(fd_addr);
}
struct Cleanup {
    int fd=-1; std::string path;
    ~Cleanup() { d2_files_unmount(); if(fd>=0) close(fd); if(!path.empty()) unlink(path.c_str()); }
};
}
int d2_disc_bundle_run(const char *directory,char *report,size_t capacity) {
    std::string text="disc_guest_scope=synthetic-iso9660-through-lifted-lv2-adapters\n"
        "disc_guest_game_executed=false\ndisc_guest_mount_persistent=false\n";
    unsigned failures=0;
    try {
        Cleanup cleanup; auto bytes=image();
        std::string pattern=std::string(directory)+"/d2-disc-XXXXXX";
        std::vector<char> name(pattern.begin(),pattern.end()); name.push_back(0);
        cleanup.fd=mkstemp(name.data()); need(cleanup.fd>=0,"disc fixture create"); cleanup.path=name.data();
        // Nonzero provider offset exercises the exact Android descriptor route.
        std::array<unsigned char,37> prefix{};
        need(write(cleanup.fd,prefix.data(),prefix.size())==ssize_t(prefix.size()),"disc prefix");
        need(write(cleanup.fd,bytes.data(),bytes.size())==ssize_t(bytes.size()),"disc fixture write");
        auto inspection=d2_inspect_document(cleanup.fd,prefix.size(),bytes.size());
        need(inspection.find("executable_status=elf-plan-valid\n")!=std::string::npos,"disc executable plan");
        need(inspection.find("executable_guest_memory_written=false\n")!=std::string::npos,"disc plan no guest writes");
        text+="disc_guest_executable_plan=passed\n";
        auto inspect_fixture=[&](const std::vector<unsigned char> &candidate,const char *expected) {
            need(pwrite(cleanup.fd,candidate.data(),candidate.size(),prefix.size())==ssize_t(candidate.size()),"self fixture write");
            auto result=d2_inspect_document(cleanup.fd,prefix.size(),candidate.size());
            need(result.find(expected)!=std::string::npos,"self fixture status");
            need(result.find("executable_guest_memory_written=false\n")!=std::string::npos,"self no guest write");
            need(result.find("executable_code_executed=false\n")!=std::string::npos,"self no execution");
        };
        inspect_fixture(self_image(bytes,0),"executable_status=self-plain-plan-valid\n");
        inspect_fixture(self_image(bytes,2),"executable_status=self-decryption-required\n");
        auto zipped=self_image(bytes,0);
        std::array<unsigned char,32> packed{}; uLongf packed_size=packed.size();
        std::array<unsigned char,8> zeros{};
        need(compress(packed.data(),&packed_size,zeros.data(),zeros.size())==Z_OK,"self synthetic zlib");
        std::memcpy(zipped.data()+30*block+512,packed.data(),packed_size);
        be_write(zipped.data()+30*block+328,packed_size,8); be_write(zipped.data()+30*block+336,2,4);
        inspect_fixture(zipped,"executable_status=self-plain-plan-valid\n");
        zipped[30*block+512]=0;
        inspect_fixture(zipped,"executable_reason=self-invalid-compressed-segment\n");
        auto malformed=self_image(bytes,0); be_write(malformed.data()+30*block+72,UINT64_MAX,8);
        inspect_fixture(malformed,"executable_reason=self-program-or-segment-table-bounds\n");
        auto misaligned=self_image(bytes,0);
        be_write(misaligned.data()+30*block+208+48,512,8);
        inspect_fixture(misaligned,"executable_reason=self-load-offset-address-incongruent\n");
        auto detail=d2_inspect_document(cleanup.fd,prefix.size(),bytes.size());
        need(detail.find("self_failure_segment_index=0\n")!=std::string::npos,"self failure index");
        need(detail.find("self_failure_checks=offset-address-incongruent\n")!=std::string::npos,"self exact condition");
        need(detail.find("self_segment_1=")!=std::string::npos,"self full public table");
        text+="disc_guest_self_failure_details=passed\n";
        inspect_fixture(bytes,"executable_status=elf-plan-valid\n");
        text+="disc_guest_self_plain_plan=passed\ndisc_guest_self_encrypted_gate=passed\n"
            "disc_guest_self_rejection_and_recovery=passed\ndisc_guest_self_zlib_plan=passed\n";
        need(d2_files_mount_disc(cleanup.fd,prefix.size(),bytes.size()),"disc mount");
        constexpr const char *asset="/dev_bdvd/PS3_GAME/USRDIR/FIXTURE.BIN";
        uint32_t a=open_file(asset), b=open_file(asset), sfo=open_file("/dev_bdvd/PS3_GAME/PARAM.SFO");
        need(a!=b && b!=sfo && d2_files_open_count()==3,"disc independent handles");
        need(call(SYS_FS_FSTAT,{a,data_addr})==0 && d2_lift_read64(data_addr+40)==4099,"disc stat");
        need(call(SYS_FS_READ,{a,data_addr,64,count_addr})==0 && d2_lift_read64(count_addr)==64,"disc read");
        for(unsigned i=0;i<64;++i) need(d2_lift_read8(data_addr+i)==((i*29+11)&255),"disc asset bytes");
        need(call(SYS_FS_READ,{b,data_addr,1,count_addr})==0 && d2_lift_read8(data_addr)==11,"disc independent cursor");
        need(call(SYS_FS_READ,{sfo,data_addr,4,count_addr})==0 && d2_lift_read32(data_addr)==0x00505346,"disc second file");
        need(call(SYS_FS_LSEEK,{a,uint64_t(-3),2,count_addr})==0,"disc end seek");
        need(call(SYS_FS_READ,{a,data_addr,16,count_addr})==0 && d2_lift_read64(count_addr)==3,"disc short read");
        need(call(SYS_FS_READ,{a,data_addr,16,count_addr})==0 && !d2_lift_read64(count_addr),"disc EOF");
        auto empty=open_file("/dev_bdvd/PS3_GAME/USRDIR/EMPTY.BIN");
        need(call(SYS_FS_READ,{empty,data_addr,1,count_addr})==0 && !d2_lift_read64(count_addr),"disc empty file");
        text+="disc_guest_assets_stat_seek_and_eof=passed\n";
        path("/dev_bdvd/PS3_GAME/USRDIR/missing"); need(err(call(SYS_FS_OPEN,{path_addr,0,fd_addr}),CELL_ENOENT),"disc missing");
        path("/dev_bdvd/PS3_GAME/USRDIR"); need(err(call(SYS_FS_OPEN,{path_addr,0,fd_addr}),CELL_EISDIR),"disc directory");
        path("/dev_bdvd/PS3_GAME/../escape"); need(err(call(SYS_FS_OPEN,{path_addr,0,fd_addr}),CELL_EINVAL),"disc traversal");
        path(asset); need(err(call(SYS_FS_OPEN,{path_addr,1,fd_addr}),CELL_EROFS),"disc read only");
        need(!d2_files_mount_disc(cleanup.fd,37,bytes.size()),"disc replacement with live handles rejected");
        need(call(SYS_FS_CLOSE,{empty})==0 && call(SYS_FS_CLOSE,{sfo})==0 && call(SYS_FS_CLOSE,{b})==0,"disc close");
        d2_lift_write8(data_addr,0xaa); d2_lift_write64(count_addr,0x1234);
        need(err(call(SYS_FS_READ,{a,data_addr,65537,count_addr}),CELL_EINVAL),"disc read bound");
        need(d2_lift_read8(data_addr)==0xaa && d2_lift_read64(count_addr)==0x1234,"disc error transaction");
        need(call(SYS_FS_CLOSE,{a})==0,"disc close last");
        need(!d2_files_mount_disc(-1,0,bytes.size()),"disc invalid remount");
        a=open_file(asset); need(a!=b,"disc stale IDs"); // Failed remount preserved old session.
        need(call(SYS_FS_READ,{a,data_addr,1,count_addr})==0 && d2_lift_read8(data_addr)==11,"disc recovery");
        text+="disc_guest_errors_and_mount_transaction=passed\n";
        close(cleanup.fd); cleanup.fd=-1;
        need(call(SYS_FS_READ,{a,data_addr,1,count_addr})==0 && d2_lift_read8(data_addr)==40,"disc owns duplicate");
        d2_files_unmount(); need(d2_files_open_count()==0 && err(call(SYS_FS_CLOSE,{a}),CELL_EBADF),"disc unmount audit");
        need(unlink(cleanup.path.c_str())==0,"disc remove"); cleanup.path.clear();
        text+="disc_guest_descriptor_lifetime_and_cleanup=passed\n";
    } catch(const std::exception &e) { ++failures; text+="disc_guest_error="+std::string(e.what())+"\n"; }
    text+="disc_guest_failures="+std::to_string(failures)+"\ndisc_guest_status="+(failures ? std::string("failed") : std::string("passed"))+"\n";
    if(text.size()+1>capacity) return 1;
    std::memcpy(report,text.c_str(),text.size()+1); return failures ? 1 : 0;
}
