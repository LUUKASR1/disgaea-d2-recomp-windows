/* Integrated fixture session, not a complete game scheduler.
 * All kernel handlers below are the prepared SDK implementations. */
#include "dispatch_probe.h"
#include "runtime/syscalls/sys_memory.h"
#include "runtime/syscalls/sys_mutex.h"
#include "runtime/syscalls/sys_semaphore.h"
#include "runtime/syscalls/sys_ppu_thread.h"
#include "runtime/platform/guest_poll.h"
#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

int d2_guest_workers_begin_epoch(void);
static lv2_syscall_table table;
static int enabled;
static __thread ppu_context *sdk_worker;
static pthread_rwlock_t ranges_lock=PTHREAD_RWLOCK_INITIALIZER;
typedef struct { uint32_t address,bytes; int writable; } region;
static region ranges[32];

int d2_bundle_range(uint64_t address,uint32_t bytes,int write) {
    int valid=0;
    pthread_rwlock_rdlock(&ranges_lock);
    for(unsigned i=0;i<32;++i) if(ranges[i].bytes && address>=ranges[i].address &&
        address-ranges[i].address<ranges[i].bytes &&
        bytes<=ranges[i].bytes-(address-ranges[i].address) && (!write || ranges[i].writable)) valid=1;
    pthread_rwlock_unlock(&ranges_lock); return valid;
}
static int track(uint32_t address,uint32_t bytes) {
    int added=0; pthread_rwlock_wrlock(&ranges_lock);
    for(unsigned i=0;i<32;++i) if(!ranges[i].bytes) {
        ranges[i]=(region){address,bytes,1}; added=1; break;
    }
    pthread_rwlock_unlock(&ranges_lock); return added;
}
int d2_bundle_map(uint32_t address,uint32_t bytes) {
    // Private test mapping only. Do not accept arbitrary game/host addresses.
    if(!enabled || address!=0x60000000 || bytes!=0x10000 ||
       d2_bundle_range(address,1,0) || vm_commit(address,bytes)!=CELL_OK) return 0;
    return track(address,bytes);
}
int d2_bundle_protect(uint32_t address,int write) {
    int ok=0; pthread_rwlock_wrlock(&ranges_lock);
    for(unsigned i=0;i<32;++i) if(ranges[i].bytes && ranges[i].address==address) {
        ok=vm_protect(address,ranges[i].bytes,1,write,0)==CELL_OK;
        if(ok) ranges[i].writable=write;
    }
    pthread_rwlock_unlock(&ranges_lock); return ok;
}
int d2_bundle_unmap(uint32_t address) {
    int ok=0; pthread_rwlock_wrlock(&ranges_lock);
    for(unsigned i=0;i<32;++i) if(ranges[i].bytes && ranges[i].address==address) {
        uint32_t bytes=ranges[i].bytes;
        ok=vm_protect(address,bytes,0,0,0)==CELL_OK &&
           madvise(vm_base+address,bytes,MADV_DONTNEED)==0;
        if(ok) ranges[i]=(region){0};
    }
    pthread_rwlock_unlock(&ranges_lock); return ok;
}
static void worker_entry(ppu_context *context) {
    sdk_worker=context;
    d2_dispatch_worker_entry(context,sizeof(*context));
    sdk_worker=NULL;
}
int d2_bundle_init(void) {
    if(enabled || !d2_guest_workers_begin_epoch()) return 0;
    lv2_syscall_table_init(&table);
    sys_memory_init(&table); sys_mutex_init(&table); sys_semaphore_init(&table);
    memset(g_ppu_threads,0,sizeof(g_ppu_threads));
    vm_stack_alloc_init(&g_vm_stack_alloc);
    ppu_thread_register_main();
    g_ppu_thread_entry_trampoline=worker_entry;
    lv2_syscall_register(&table,SYS_PPU_THREAD_CREATE,sys_ppu_thread_create);
    lv2_syscall_register(&table,SYS_PPU_THREAD_JOIN,sys_ppu_thread_join);
    lv2_syscall_register(&table,SYS_PPU_THREAD_EXIT,sys_ppu_thread_exit);
    enabled=1; return 1;
}
static int pointer(uint64_t ea,uint32_t bytes,int write,int optional) {
    if(!ea && optional) return 1;
    uint32_t align=bytes==8 ? 8 : 4;
    if(ea%align) { d2_lift_fault(write ? D2_DISPATCH_WRITE : D2_DISPATCH_READ); return 0; }
    return d2_dispatch_guest_range(ea,bytes,write);
}
static void write_result(void *out,int64_t value) {
    memcpy((char*)out+offsetof(ppu_context,gpr)+3*sizeof(uint64_t),&value,sizeof(value));
}
int d2_bundle_syscall(void *out,size_t bytes) {
    if(!enabled || bytes!=sizeof(ppu_context)) return 0;
    ppu_context c; memcpy(&c,out,sizeof(c));
    uint64_t n=c.gpr[11]; int valid=1;
    switch(n) {
    case SYS_MEMORY_ALLOCATE:
        if(c.gpr[3]>0x100000 || !c.gpr[3] || c.gpr[4]!=SYS_MEMORY_PAGE_SIZE_64K) valid=0;
        valid &= pointer(c.gpr[5],4,1,0); break;
    case SYS_MEMORY_FREE: valid=c.gpr[3]<=UINT32_MAX; break;
    case SYS_MEMORY_GET_USER_MEMORY_SIZE: valid=pointer(c.gpr[3],8,1,0); break;
    case SYS_MUTEX_CREATE: valid=pointer(c.gpr[3],4,1,0) && pointer(c.gpr[4],32,0,1); break;
    case SYS_MUTEX_DESTROY: case SYS_MUTEX_UNLOCK: case SYS_MUTEX_TRYLOCK: break;
    case SYS_MUTEX_LOCK: case SYS_SEMAPHORE_WAIT:
        // This fixture has no indefinite waits or arbitrary guest workloads.
        valid=c.gpr[4]>0 && c.gpr[4]<=1000000; break;
    case SYS_SEMAPHORE_CREATE: valid=pointer(c.gpr[3],4,1,0) && pointer(c.gpr[4],32,0,1); break;
    case SYS_SEMAPHORE_DESTROY: case SYS_SEMAPHORE_POST: break;
    case SYS_SEMAPHORE_GET_VALUE: valid=pointer(c.gpr[4],4,1,0); break;
    case SYS_PPU_THREAD_CREATE:
        valid=pointer(c.gpr[3],8,1,0) && c.gpr[4]==0x20050 && c.gpr[7]==0x10000 &&
              c.gpr[8]==1 && c.gpr[9]==0 && pointer(c.gpr[5],20,0,0); break;
    case SYS_PPU_THREAD_JOIN: {
        valid=pointer(c.gpr[4],8,1,0);
        uint64_t id=c.gpr[3];
        if(valid && id>1 && id<=PPU_THREAD_MAX && g_ppu_threads[id-1].state!=PPU_THREAD_STATE_FREE) {
            ppu_thread_info *t=&g_ppu_threads[id-1];
            struct timespec until; clock_gettime(CLOCK_REALTIME,&until); until.tv_sec+=5;
            pthread_mutex_lock(&t->finish_mutex);
            while(!t->finished) if(pthread_cond_timedwait(&t->finish_cond,&t->finish_mutex,&until)!=0) {
                // Never release the VM under a worker whose completion is unknown.
                fputs("D2 bundle worker failed to finish within five seconds\n",stderr); abort();
            }
            pthread_mutex_unlock(&t->finish_mutex);
        }
        break;
    }
    case SYS_PPU_THREAD_EXIT:
        if(!sdk_worker) { write_result(out,(int64_t)(int32_t)CELL_ENOSYS); d2_lift_fault(D2_DISPATCH_SYSCALL_UNAVAILABLE); return 1; }
        // SDK exit longjmps to its host-thread procedure. Synchronize its actual
        // context first; no C++ object with a destructor lies on this worker path.
        memcpy(sdk_worker,&c,sizeof(c)); lv2_syscall_dispatch(&table,sdk_worker);
        write_result(out,(int64_t)sdk_worker->gpr[3]); return 1;
    default: return 0;
    }
    if(!valid) { write_result(out,(int64_t)(int32_t)CELL_EINVAL); return 1; }
    lv2_syscall_dispatch(&table,&c);
    if(n==SYS_MEMORY_ALLOCATE && c.gpr[3]==0) {
        uint64_t args[8]; memcpy(args,(char*)out+offsetof(ppu_context,gpr)+3*8,sizeof(args));
        uint32_t address=d2_lift_read32(args[2]);
        if(!track(address,VM_ALIGN_UP((uint32_t)args[0],0x10000))) abort();
    }
    if(n==SYS_MEMORY_FREE && c.gpr[3]==0) {
        uint64_t address; memcpy(&address,(char*)out+offsetof(ppu_context,gpr)+3*8,8);
        if(address>UINT32_MAX || !d2_bundle_unmap((uint32_t)address)) abort();
    }
    write_result(out,(int64_t)c.gpr[3]); return 1;
}
int d2_bundle_clean(void) {
    int clean=1;
    for(unsigned i=0;i<32;++i) clean &= ranges[i].bytes==0;
    for(unsigned i=0;i<SYS_MEMORY_ALLOC_MAX;++i) clean &= !g_sys_mem_allocs[i].active;
    for(unsigned i=0;i<SYS_MUTEX_MAX;++i) clean &= !g_sys_mutexes[i].active;
    for(unsigned i=0;i<SYS_SEMAPHORE_MAX;++i) clean &= !g_sys_semaphores[i].active;
    for(unsigned i=1;i<PPU_THREAD_MAX;++i) clean &= g_ppu_threads[i].state==PPU_THREAD_STATE_FREE;
    clean &= ps3_guest_workers_stopped();
    if(!clean) abort(); // A dirty session must never release live-worker memory.
    g_ppu_thread_entry_trampoline=NULL; enabled=0; return clean;
}
/* Optional SDK debugger hooks fail closed. No SPU work is registered; the SDK's
 * own default job-completion hook is linked, not a fake SPU scheduler. */
void ppu_log_host_chain(const char *tag) { (void)tag; abort(); }
void lbp_breadcrumb_dump(const char *tag) { (void)tag; abort(); }
