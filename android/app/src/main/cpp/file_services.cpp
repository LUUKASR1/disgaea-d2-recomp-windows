// Android descriptor-backed subset. ABI numbers/error constants come from the
// pinned SDK; these bounded adapters are new code, not SDK sys_fs handlers.
#include "file_services.h"
#include "descriptor_file.h"
#include "dispatch_probe.h"
#include "runtime/syscalls/lv2_syscall_table.h"
#include <array>
#include <cstring>
#include <mutex>

namespace {
std::mutex lock;
d2::DescriptorFile file;
std::array<uint64_t,8> positions{};
std::array<uint64_t,8> handles{};
uint64_t next_handle=100;
char mounted_path[256]{};
constexpr size_t max_read=65536;
bool valid_path(const char *p) {
    if(!p || std::strncmp(p,"/dev_bdvd/",10) || std::strlen(p)>=sizeof(mounted_path)) return false;
    const char *part=p+1;
    for(const char *c=part;;++c) {
        if(*c=='\\' || static_cast<unsigned char>(*c)>=128 || (*c && *c<32)) return false;
        if(*c=='/' || !*c) {
            size_t n=static_cast<size_t>(c-part);
            if(!n || (n==1 && *part=='.') || (n==2 && part[0]=='.' && part[1]=='.')) return false;
            if(!*c) return true;
            part=c+1;
        }
    }
}
bool range(uint64_t a,uint32_t n,bool write,unsigned alignment=1) {
    if(a%alignment) return false;
    return d2_dispatch_guest_range(a,n,write);
}
int64_t error(uint32_t e) { return static_cast<int32_t>(e); }
int64_t dispatch(ppu_context &c) {
    const uint64_t *a=&c.gpr[3];
    if(c.gpr[11]==SYS_FS_OPEN) {
        if(a[1]!=0) return error(CELL_EROFS);
        if(a[3] || a[4]) return error(CELL_EINVAL); // optional open args unsupported
        if(!range(a[2],4,true,4)) return error(CELL_EFAULT);
        char name[256]{}; bool terminated=false;
        for(size_t i=0;i<sizeof(name);++i) {
            if(a[0]>UINT32_MAX-i || !range(a[0]+i,1,false)) return error(CELL_EFAULT);
            name[i]=static_cast<char>(d2_lift_read8(a[0]+i));
            if(!name[i]) { terminated=true; break; }
        }
        if(!terminated || !valid_path(name)) return error(CELL_EINVAL);
        if(!file.valid() || std::strcmp(name,mounted_path)) return error(CELL_ENOENT);
        for(size_t i=0;i<handles.size();++i) if(!handles[i]) {
            // Never recycle guest handle IDs across mount epochs in this process.
            if(next_handle>INT32_MAX) return error(CELL_EMFILE);
            handles[i]=next_handle++; positions[i]=0;
            d2_lift_write32(a[2],static_cast<uint32_t>(handles[i])); return 0;
        }
        return error(CELL_EMFILE);
    }
    size_t index=handles.size();
    for(size_t i=0;i<handles.size();++i) if(handles[i] && handles[i]==a[0]) { index=i; break; }
    if(index==handles.size()) return error(CELL_EBADF);
    if(c.gpr[11]==SYS_FS_CLOSE) { handles[index]=0; positions[index]=0; return 0; }
    if(c.gpr[11]==SYS_FS_WRITE) return error(CELL_EROFS);
    if(c.gpr[11]==SYS_FS_FSTAT) {
        // CellFsStat: 3 x u32, padding, then 5 x u64. This read-only
        // descriptor view has synthetic ownership/timestamps, not host metadata.
        if(!range(a[1],56,true,8)) return error(CELL_EFAULT);
        for(unsigned i=0;i<56;++i) d2_lift_write8(a[1]+i,0);
        d2_lift_write32(a[1],0100444);
        d2_lift_write64(a[1]+40,file.size());
        d2_lift_write64(a[1]+48,4096); return 0;
    }
    if(c.gpr[11]==SYS_FS_READ) {
        if(a[2]>max_read) return error(CELL_EINVAL);
        if(!range(a[3],8,true,8) || (a[2] && !range(a[1],static_cast<uint32_t>(a[2]),true)))
            return error(CELL_EFAULT);
        uint64_t remain=file.size()-positions[index];
        size_t count=static_cast<size_t>(a[2]<remain ? a[2] : remain);
        // Stage first: host short read/truncation cannot partially overwrite the
        // guest destination or advance its cursor. All pointers checked first.
        std::array<unsigned char,max_read> buffer{};
        if(!file.read(positions[index],buffer.data(),count)) return error(CELL_EIO);
        for(size_t i=0;i<count;++i) d2_lift_write8(a[1]+i,buffer[i]);
        d2_lift_write64(a[3],count); positions[index]+=count; return 0;
    }
    if(c.gpr[11]==SYS_FS_LSEEK) {
        if(!range(a[3],8,true,8)) return error(CELL_EFAULT);
        if(a[2]>2) return error(CELL_EINVAL);
        uint64_t base=a[2]==0 ? 0 : a[2]==1 ? positions[index] : file.size();
        int64_t delta; std::memcpy(&delta,&a[1],sizeof(delta));
        uint64_t target;
        if(delta<0) {
            uint64_t magnitude=uint64_t(-(delta+1))+1;
            if(magnitude>base) return error(CELL_EINVAL);
            target=base-magnitude;
        } else {
            if(static_cast<uint64_t>(delta)>file.size()-base) return error(CELL_EINVAL);
            target=base+static_cast<uint64_t>(delta);
        }
        positions[index]=target; d2_lift_write64(a[3],target); return 0;
    }
    return error(CELL_ENOSYS);
}
}
int d2_files_mount(int fd,uint64_t base,uint64_t length,const char *path) {
    std::lock_guard<std::mutex> guard(lock);
    for(auto h:handles) if(h) return 0;
    if(!valid_path(path) || !file.attach(fd,base,length)) return 0;
    std::strcpy(mounted_path,path); return 1;
}
void d2_files_unmount() {
    std::lock_guard<std::mutex> guard(lock);
    handles.fill(0); positions.fill(0); mounted_path[0]=0; file.reset();
}
int d2_files_open_count() {
    std::lock_guard<std::mutex> guard(lock); int n=0;
    for(auto h:handles) if(h) ++n;
    return n;
}
int d2_files_syscall(void *context,size_t bytes) {
    if(!context || bytes!=sizeof(ppu_context)) return 0;
    ppu_context ctx; std::memcpy(&ctx,context,sizeof(ctx));
    switch(ctx.gpr[11]) {
    case SYS_FS_OPEN: case SYS_FS_READ: case SYS_FS_WRITE: case SYS_FS_CLOSE: case SYS_FS_LSEEK: case SYS_FS_FSTAT: break;
    default: return 0;
    }
    std::lock_guard<std::mutex> guard(lock);
    int64_t value=dispatch(ctx);
    std::memcpy(static_cast<char*>(context)+offsetof(ppu_context,gpr)+3*sizeof(uint64_t),&value,sizeof(value));
    return 1;
}
