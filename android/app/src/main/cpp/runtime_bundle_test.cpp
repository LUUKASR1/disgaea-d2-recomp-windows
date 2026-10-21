#include "dispatch_probe.h"
#include "runtime/syscalls/lv2_syscall_table.h"
#include "runtime/syscalls/sys_memory.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
uint64_t call(uint64_t number,std::initializer_list<uint64_t> values={}) {
    std::array<uint64_t,8> args{}; unsigned i=0;
    for(auto value:values) args.at(i++)=value;
    uint64_t result=0;
    int error=d2_fixture_syscall(number,args.data(),1,&result);
    if(error) throw std::runtime_error("syscall bridge fault "+std::to_string(number)+":"+std::to_string(error));
    return result;
}
void require(bool ok,const char *why) { if(!ok) throw std::runtime_error(why); }
void ok(uint64_t n,std::initializer_list<uint64_t> args={}) { require(call(n,args)==0,"SDK syscall returned error"); }
void reset_fault() { require(call(SYS_TIME_GET_TIMEBASE_FREQUENCY)==79800000,"clock recovery"); }
struct Session {
    uint32_t allocation=0,mutex=0,done=0,gate=0;
    std::array<uint64_t,4> workers{};
    void join() {
        for(auto &id:workers) if(id) {
            ok(SYS_PPU_THREAD_JOIN,{id,0x20400});
            require(d2_lift_read64(0x20400)==7,"worker exit status"); id=0;
        }
    }
    ~Session() {
        // Every exit path must reap known workers before releasing their VM.
        try {
            reset_fault();
            for(auto &id:workers) if(id) { ok(SYS_PPU_THREAD_JOIN,{id,0x20400}); id=0; }
            if(done) ok(SYS_SEMAPHORE_DESTROY,{done});
            if(gate) ok(SYS_SEMAPHORE_DESTROY,{gate});
            if(mutex) ok(SYS_MUTEX_DESTROY,{mutex});
            if(allocation) ok(SYS_MEMORY_FREE,{allocation});
        } catch(...) { std::fputs("bundle cleanup could not safely finish\n",stderr); std::abort(); }
    }
};
void round() {
    Session s;
    ok(SYS_MEMORY_GET_USER_MEMORY_SIZE,{0x20300});
    uint32_t available=d2_lift_read32(0x20304);
    ok(SYS_MEMORY_ALLOCATE,{0x10000,SYS_MEMORY_PAGE_SIZE_64K,0x20308});
    s.allocation=d2_lift_read32(0x20308);
    require(s.allocation>=0x40000000 && !(s.allocation&0xffff),"allocation alignment");
    require(d2_lift_read64(s.allocation)==0 && d2_lift_read64(s.allocation+0xfff8)==0,"fresh allocation zero fill");
    ok(SYS_MEMORY_GET_USER_MEMORY_SIZE,{0x20300});
    require(d2_lift_read32(0x20304)==available-0x10000,"allocation accounting");
    d2_lift_write64(s.allocation+0xfff8,0x123456789abcdef0ULL);
    require(d2_lift_read64(s.allocation+0xfff8)==0x123456789abcdef0ULL,"dynamic guest memory access");

    ok(SYS_MUTEX_CREATE,{0x20310,0}); s.mutex=d2_lift_read32(0x20310);
    ok(SYS_SEMAPHORE_CREATE,{0x20314,0,0,4}); s.done=d2_lift_read32(0x20314);
    ok(SYS_SEMAPHORE_CREATE,{0x20318,0,1,1}); s.gate=d2_lift_read32(0x20318);
    require(call(SYS_SEMAPHORE_WAIT,{s.done,1000})==(uint64_t)(int64_t)(int32_t)CELL_ETIMEDOUT,"semaphore timeout");
    require(call(SYS_SEMAPHORE_POST,{s.gate,1})==(uint64_t)(int64_t)(int32_t)CELL_EBUSY,"semaphore maximum");
    ok(SYS_MUTEX_LOCK,{s.mutex,100000});
    require(call(SYS_MUTEX_LOCK,{s.mutex,1000})==(uint64_t)(int64_t)(int32_t)CELL_EDEADLK,"mutex self-deadlock rejection");
    bool timed_out=false;
    std::thread contender([&] {
        std::array<uint64_t,8> args{}; args[0]=s.mutex; args[1]=1000;
        uint64_t result=0;
        timed_out=d2_fixture_syscall(SYS_MUTEX_LOCK,args.data(),777,&result)==0 &&
            result==(uint64_t)(int64_t)(int32_t)CELL_ETIMEDOUT;
    });
    contender.join(); require(timed_out,"mutex timeout on another thread");
    ok(SYS_MUTEX_UNLOCK,{s.mutex});
    require(call(SYS_MUTEX_LOCK,{0xffffffff,1000})==(uint64_t)(int64_t)(int32_t)CELL_ESRCH,"unknown mutex");

    uint32_t counter=s.allocation+0x80,arg=s.allocation+0x100;
    d2_lift_write32(counter,0);
    d2_lift_write32(arg,s.mutex); d2_lift_write32(arg+4,counter);
    d2_lift_write32(arg+8,s.done); d2_lift_write32(arg+12,s.gate); d2_lift_write32(arg+16,0);
    for(unsigned i=0;i<4;++i) {
        ok(SYS_PPU_THREAD_CREATE,{0x20320+i*8,0x20050,arg,1000,0x10000,1,0});
        s.workers[i]=d2_lift_read64(0x20320+i*8);
        require(s.workers[i]>1,"unique worker thread ID");
    }
    for(unsigned i=0;i<4;++i) ok(SYS_SEMAPHORE_WAIT,{s.done,1000000});
    s.join();
    require(d2_lift_read32(counter)==4,"shared counter with SDK mutex");
    require(d2_lift_read32(arg+16)==0,"sys_ppu_thread_exit returned into guest code");
    ok(SYS_SEMAPHORE_GET_VALUE,{s.gate,0x20360});
    require(d2_lift_read32(0x20360)==1,"admission gate balance");

    uint32_t released=s.allocation;
    ok(SYS_MEMORY_FREE,{released}); s.allocation=0;
    ok(SYS_MEMORY_GET_USER_MEMORY_SIZE,{0x20300});
    require(d2_lift_read32(0x20304)==available,"free accounting");
    d2_lift_read32(released);
    require(d2_lift_faulted()==D2_DISPATCH_READ,"freed allocation remains accessible"); reset_fault();
    require(call(SYS_MEMORY_FREE,{released})==(uint64_t)(int64_t)(int32_t)CELL_EINVAL,"double free rejection");
}
void mapping() {
    constexpr uint32_t at=0x60000000;
    require(d2_bundle_map(at,0x10000),"explicit SDK VM commit");
    d2_lift_write32(at,0x10203040);
    require(d2_bundle_protect(at,0),"read-only protection");
    require(d2_lift_read32(at)==0x10203040,"protected read");
    d2_lift_write32(at,0xdeadbeef);
    require(d2_lift_faulted()==D2_DISPATCH_WRITE,"read-only write accepted"); reset_fault();
    require(d2_lift_read32(at)==0x10203040,"read-only contents changed");
    require(d2_bundle_protect(at,1),"restore write permission");
    d2_lift_write32(at,123); require(d2_lift_read32(at)==123,"restored mapping write");
    require(d2_bundle_unmap(at),"release mapping pages");
    d2_lift_read32(at); require(d2_lift_faulted()==D2_DISPATCH_READ,"released mapping read accepted"); reset_fault();
}
}

int d2_runtime_bundle_run(char *report,size_t capacity) {
    if(!report || capacity<2048) return 1;
    report[0]=0;
    bool initialized=false;
    try {
        require(d2_bundle_init(),"worker epoch or SDK initialization"); initialized=true;
        mapping();
        for(unsigned i=0;i<3;++i) round();
        require(d2_bundle_clean(),"resource audit"); initialized=false;
        snprintf(report,capacity,
            "bundle_scope=fixed-lifted-workers-with-sdk-memory-thread-and-sync-services\n"
            "bundle_rounds=3\nbundle_workers_created_and_joined=12\n"
            "bundle_memory_allocate_free=passed\nbundle_mapping_protection_release=passed\n"
            "bundle_mutex_and_shared_counter=passed\nbundle_semaphore_wait_post_timeout=passed\n"
            "bundle_mutex_cross_thread_timeout=passed\n"
            "bundle_thread_create_exit_join=passed\nbundle_no_post_exit_execution=passed\n"
            "bundle_error_paths_and_recovery=passed\nbundle_resource_audit=passed\n"
            "bundle_cross_thread_reservations_validated=false\nbundle_raw_spu_interrupts_available=false\n"
            "bundle_game_boot=false\nbundle_failures=0\nbundle_status=passed\n");
        return 0;
    } catch(const std::exception &e) {
        if(initialized) d2_bundle_clean();
        snprintf(report,capacity,"bundle_error=%s\nbundle_failures=1\nbundle_status=failed\n",e.what());
        return 1;
    }
}
