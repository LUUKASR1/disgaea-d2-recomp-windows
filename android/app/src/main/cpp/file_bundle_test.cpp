#include "file_services.h"
#include "descriptor_file.h"
#include "dispatch_probe.h"
#include "runtime/syscalls/lv2_syscall_table.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr uint32_t path_addr=0x21000, fd_addr=0x21200, data_addr=0x22000, count_addr=0x21400;
constexpr const char *virtual_path="/dev_bdvd/PS3_GAME/USRDIR/d2-owned-fixture.bin";
uint64_t call(uint64_t n,std::initializer_list<uint64_t> v={}) {
    std::array<uint64_t,8> args{}; size_t i=0;
    for(auto a:v) args.at(i++)=a;
    uint64_t out=0;
    int fault=d2_fixture_syscall(n,args.data(),1,&out);
    if(fault) throw std::runtime_error("file bridge fault");
    return out;
}
void require(bool ok,const char *what) { if(!ok) throw std::runtime_error(what); }
bool is_error(uint64_t value,uint32_t e) { return static_cast<uint32_t>(value)==e; }
void name(const char *s) {
    for(size_t i=0;i<=std::strlen(s);++i) d2_lift_write8(path_addr+i,static_cast<uint8_t>(s[i]));
}
uint32_t open_guest() {
    name(virtual_path);
    require(call(SYS_FS_OPEN,{path_addr,0,fd_addr})==0,"guest open");
    return d2_lift_read32(fd_addr);
}
struct Cleanup {
    int fd=-1; std::string path;
    ~Cleanup() { d2_files_unmount(); if(fd>=0) close(fd); if(!path.empty()) unlink(path.c_str()); }
};
}
int d2_file_bundle_run(const char *directory,char *report,size_t capacity) {
    std::string text="files_scope=read-only-descriptor-slice-and-six-lv2-adapters\n"
        "files_input=internal-synthetic-fixture\nfiles_sdk_fs_handlers_linked=false\n"
        "files_document_picker_integrated=false\nfiles_iso_udf_available=false\n"
        "files_game_loaded=false\n";
    unsigned failures=0;
    try {
        Cleanup cleanup;
        std::string pattern=std::string(directory)+"/d2-files-XXXXXX";
        std::vector<char> p(pattern.begin(),pattern.end()); p.push_back(0);
        cleanup.fd=mkstemp(p.data()); require(cleanup.fd>=0,"private fixture"); cleanup.path=p.data();
        std::vector<unsigned char> bytes(8192);
        for(size_t i=0;i<bytes.size();++i) bytes[i]=static_cast<unsigned char>((i*31+7)&255);
        require(write(cleanup.fd,bytes.data(),bytes.size())==static_cast<ssize_t>(bytes.size()),"write fixture");
        constexpr uint64_t base=31, length=4096;
        {
            d2::DescriptorFile view;
            require(view.attach(cleanup.fd,base,length),"descriptor slice");
            require(!view.attach(cleanup.fd,UINT64_MAX,2) && view.valid(),"offset overflow rejected");
            require(!view.attach(cleanup.fd,8000,1000),"slice outside file rejected");
            int pipes[2]; require(pipe(pipes)==0,"pipe fixture");
            bool rejected=!view.attach(pipes[0],0,0); close(pipes[0]); close(pipes[1]);
            require(rejected,"nonseekable descriptor rejected");
            int write_only=open(cleanup.path.c_str(),O_WRONLY|O_CLOEXEC);
            require(write_only>=0,"write-only fixture");
            rejected=!view.attach(write_only,0,1); close(write_only);
            require(rejected,"unreadable descriptor rejected");
            require(lseek(cleanup.fd,13,SEEK_SET)==13,"caller cursor");
            bool first=true, second=true;
            auto read_many=[&](bool &ok,size_t offset) {
                std::array<unsigned char,127> buffer{};
                for(unsigned i=0;i<50;++i) ok &= view.read(offset,buffer.data(),buffer.size()) &&
                    std::memcmp(buffer.data(),bytes.data()+base+offset,buffer.size())==0;
            };
            std::thread t([&] { read_many(second,200); }); read_many(first,30); t.join();
            require(first && second && lseek(cleanup.fd,0,SEEK_CUR)==13,"parallel pread preserves cursor");
            std::array<unsigned char,8> sent{}; sent.fill(0xa5);
            require(!view.read(length-2,sent.data(),sent.size()) && sent[0]==0xa5,"read outside slice");
            require(view.read(length,nullptr,0) && !view.read(UINT64_MAX,nullptr,0),"empty read boundaries");
        }
        text+="files_descriptor_bounds_and_parallel_reads=passed\n";
        require(d2_files_mount(cleanup.fd,base,length,virtual_path),"mount fixture slice");
        close(cleanup.fd); cleanup.fd=-1; // mount owns its duplicate
        uint32_t a=open_guest(), b=open_guest();
        require(a!=b && d2_files_open_count()==2,"independent guest handles");
        require(call(SYS_FS_FSTAT,{a,data_addr})==0 && d2_lift_read32(data_addr)==0100444 &&
            d2_lift_read64(data_addr+40)==length && d2_lift_read64(data_addr+48)==4096,"guest metadata");
        require(call(SYS_FS_READ,{a,data_addr,32,count_addr})==0 && d2_lift_read64(count_addr)==32,"guest read");
        for(size_t i=0;i<32;++i) require(d2_lift_read8(data_addr+i)==bytes[base+i],"asset bytes");
        require(call(SYS_FS_READ,{b,data_addr,1,count_addr})==0 && d2_lift_read8(data_addr)==bytes[base],"independent cursor");
        require(call(SYS_FS_LSEEK,{a,uint64_t(-3),2,count_addr})==0 && d2_lift_read64(count_addr)==length-3,"negative end seek");
        require(call(SYS_FS_READ,{a,data_addr,16,count_addr})==0 && d2_lift_read64(count_addr)==3,"short EOF read");
        require(call(SYS_FS_READ,{a,data_addr,16,count_addr})==0 && d2_lift_read64(count_addr)==0,"EOF");
        require(call(SYS_FS_READ,{a,0,0,count_addr})==0,"zero read");
        require(is_error(call(SYS_FS_LSEEK,{a,UINT64_C(0x8000000000000000),1,count_addr}),CELL_EINVAL),"INT64_MIN seek");
        require(is_error(call(SYS_FS_LSEEK,{a,1,2,count_addr}),CELL_EINVAL),"out-of-slice seek");
        require(is_error(call(SYS_FS_WRITE,{a,data_addr,1,count_addr}),CELL_EROFS),"writes denied");
        require(is_error(call(SYS_FS_READ,{a,data_addr,65537,count_addr}),CELL_EINVAL),"bounded transfer");
        text+="files_lifted_open_read_seek_stat_close_and_eof=passed\n";
        // Invalid output must not consume input or touch the valid destination.
        require(call(SYS_FS_LSEEK,{a,0,0,count_addr})==0,"rewind");
        d2_lift_write8(data_addr,0xaa); d2_lift_write64(count_addr,0x1234);
        std::array<uint64_t,8> bad{{a,data_addr,1,UINT64_C(0x100021400)}}; uint64_t out=0;
        require(d2_fixture_syscall(SYS_FS_READ,bad.data(),1,&out)==D2_DISPATCH_WRITE,"full pointer checked before narrowing");
        require(call(SYS_TIME_GET_TIMEBASE_FREQUENCY)==79800000,"recover pointer fault");
        require(d2_lift_read8(data_addr)==0xaa && d2_lift_read64(count_addr)==0x1234,"no output on invalid pointer");
        require(call(SYS_FS_READ,{a,data_addr,1,count_addr})==0 && d2_lift_read8(data_addr)==bytes[base],"cursor unchanged after error");
        name("/dev_bdvd/../escape"); require(is_error(call(SYS_FS_OPEN,{path_addr,0,fd_addr}),CELL_EINVAL),"traversal rejected");
        name("/dev_bdvd/PS3_GAME/missing"); require(is_error(call(SYS_FS_OPEN,{path_addr,0,fd_addr}),CELL_ENOENT),"unmounted file rejected");
        name(virtual_path); require(is_error(call(SYS_FS_OPEN,{path_addr,1,fd_addr}),CELL_EROFS),"write open denied");
        require(call(SYS_FS_CLOSE,{a})==0,"close");
        require(is_error(call(SYS_FS_CLOSE,{a}),CELL_EBADF),"double close");
        std::vector<uint32_t> all{b};
        for(unsigned i=0;i<7;++i) all.push_back(open_guest());
        require(is_error(call(SYS_FS_OPEN,{path_addr,0,fd_addr}),CELL_EMFILE),"descriptor exhaustion");
        for(auto h:all) require(call(SYS_FS_CLOSE,{h})==0,"close all");
        uint32_t fresh=open_guest(); require(fresh!=a && fresh!=b,"no recycled IDs");
        require(is_error(call(SYS_FS_READ,{a,data_addr,1,count_addr}),CELL_EBADF),"stale handle");
        require(call(SYS_FS_CLOSE,{fresh})==0 && d2_files_open_count()==0,"descriptor audit");
        text+="files_errors_recovery_and_handle_audit=passed\n";
        // External truncation is surfaced; staged reads protect guest memory.
        cleanup.fd=open(cleanup.path.c_str(),O_RDWR|O_CLOEXEC); require(cleanup.fd>=0,"reopen fixture");
        fresh=open_guest(); require(ftruncate(cleanup.fd,base+2)==0,"truncate fixture");
        d2_lift_write8(data_addr,0xa5); d2_lift_write64(count_addr,0x1234);
        require(is_error(call(SYS_FS_READ,{fresh,data_addr,32,count_addr}),CELL_EIO),"truncation error");
        require(d2_lift_read8(data_addr)==0xa5 && d2_lift_read64(count_addr)==0x1234,"failed read guest transaction");
        d2_files_unmount();
        require(is_error(call(SYS_FS_CLOSE,{fresh}),CELL_EBADF) && d2_files_open_count()==0,"unmount invalidates handles");
        require(unlink(cleanup.path.c_str())==0,"fixture removed"); cleanup.path.clear();
        text+="files_truncation_transaction_and_cleanup=passed\n";
    } catch(const std::exception &e) { ++failures; text+="files_error="+std::string(e.what())+"\n"; }
    text+="files_failures="+std::to_string(failures)+"\nfiles_status="+(failures ? std::string("failed") : std::string("passed"))+"\n";
    if(text.size()+1>capacity) return 1;
    std::memcpy(report,text.c_str(),text.size()+1); return failures ? 1 : 0;
}
