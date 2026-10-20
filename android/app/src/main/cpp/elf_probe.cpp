// Asset-free tests of the pinned SDK's ELF validation/segment-copy subset.
#include "elf_probe.h"
#include "runtime/memory/vm.h"
#include "runtime/ppu/ppu_memory.h"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>
#include <unistd.h>

extern "C" uint32_t d2_fixture_load_elf(const char *path);
extern "C" void d2_elf_tls_metadata(uint32_t *, uint32_t *, uint32_t *);

namespace {
constexpr uint32_t base = 0x10000, opd = 0x461000, toc = 0x47df98;
constexpr size_t sentinel_bytes = 0x500000;
struct Report {
    char *data; size_t capacity, used = 0; unsigned failures = 0, rejected = 0;
    void line(const char *fmt, ...) {
        if (used >= capacity) return;
        va_list args; va_start(args, fmt);
        int n = vsnprintf(data + used, capacity - used, fmt, args);
        va_end(args);
        if (n > 0) used += static_cast<size_t>(n) < capacity-used ? n : capacity-used;
    }
    void check(const char *name, bool ok) {
        line("%s=%s\n", name, ok ? "passed" : "failed");
        if (!ok) ++failures;
    }
};
void put(std::vector<uint8_t> &b, size_t off, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) b.at(off+i) = value >> ((bytes-1-i)*8);
}
std::vector<uint8_t> fixture() {
    std::vector<uint8_t> b(0x400);
    memcpy(b.data(), "\x7f" "ELF\x02\x02\x01", 7);
    put(b,16,2,2); put(b,18,21,2); put(b,20,1,4); put(b,24,opd,8);
    put(b,32,64,8); put(b,52,64,2); put(b,54,56,2); put(b,56,2,2);
    for (unsigned i=0; i<2; ++i) {
        size_t ph=64+i*56;
        put(b,ph,1,4); put(b,ph+4,i ? 6 : 5,4);
        put(b,ph+8,0x200+i*16,8); put(b,ph+16,i ? opd : base,8);
        put(b,ph+32,i ? 8 : 4,8); put(b,ph+40,i ? 8 : 16,8); put(b,ph+48,4,8);
    }
    put(b,0x200,0x4e800020,4); // BLR bytes only: this fixture never executes them.
    put(b,0x210,base,4); put(b,0x214,toc,4);
    return b;
}
bool write_image(const char *path, const std::vector<uint8_t> &b) {
    FILE *f=fopen(path,"wb"); if (!f) return false;
    bool ok=fwrite(b.data(),1,b.size(),f)==b.size();
    if (fclose(f)) ok=false;
    return ok;
}
struct Cleanup {
    std::string path;
    uint32_t previous_limit;
    ~Cleanup() {
        if (!path.empty()) unlink(path.c_str());
        vm_shutdown(); ppu_vm_size=previous_limit;
    }
};
bool reject_without_writes(const char *path, const std::vector<uint8_t> &b) {
    uint32_t va, fs, ms, after_va, after_fs, after_ms;
    d2_elf_tls_metadata(&va,&fs,&ms);
    memset(vm_base+base,0xa5,sentinel_bytes);
    if (!write_image(path,b) || d2_fixture_load_elf(path)!=0) return false;
    d2_elf_tls_metadata(&after_va,&after_fs,&after_ms);
    if (va!=after_va || fs!=after_fs || ms!=after_ms) return false;
    for (size_t i=0; i<sentinel_bytes; ++i) if (vm_base[base+i]!=0xa5) return false;
    return true;
}
void exercise(const char *path, Report &r) {
    auto b=fixture();
    bool loaded=write_image(path,b) && d2_fixture_load_elf(path)==opd;
    r.check("elf_segments_and_bss", loaded && vm_read32(base)==0x4e800020 &&
            vm_read32(base+4)==0 && vm_read32(base+12)==0);
    r.check("elf_entry_opd_and_toc", loaded && vm_read32(opd)==base && vm_read32(opd+4)==toc);

    // PT_TLS records a template; allocation/initialization of thread TLS is
    // explicitly outside this subset. Inspect metadata instead of claiming boot.
    b=fixture(); put(b,56,3,2);
    const size_t ph=64+112;
    put(b,ph,7,4); put(b,ph+8,0x200,8); put(b,ph+16,base,8);
    put(b,ph+32,4,8); put(b,ph+40,16,8); put(b,ph+48,4,8);
    uint32_t va=0,fs=0,ms=0;
    bool tls=write_image(path,b) && d2_fixture_load_elf(path)==opd;
    d2_elf_tls_metadata(&va,&fs,&ms);
    r.check("elf_tls_template_metadata", tls && va==base && fs==4 && ms==16);

    struct Mutation { size_t offset; unsigned bytes; uint64_t value; };
    const std::array<Mutation,21> mutations{{
        {0,1,0}, {4,1,1}, {5,1,1}, {16,2,3}, {18,2,62}, {20,4,0},
        {52,2,63}, {54,2,0}, {56,2,0xffff}, {32,8,UINT64_MAX-20},
        {32,8,0x3f0}, {24,8,UINT64_C(0x100461000)}, {24,8,opd+1},
        {64+8,8,UINT64_MAX-2}, {64+32,8,17}, {64+16,8,UINT64_MAX-3},
        {64+16,8,0x10fffffc}, {64+48,8,3}, {64+4,4,4},
        {0x210,4,0x12345678}, {0x214,4,0}
    }};
    bool rejected=true;
    for (const auto &m:mutations) {
        b=fixture(); put(b,m.offset,m.value,m.bytes);
        bool ok=reject_without_writes(path,b);
        if (ok) ++r.rejected;
        else { rejected=false; r.line("elf_failed_mutation_offset=%zu\n",m.offset); }
    }
    r.check("elf_invalid_images_no_monitored_writes", rejected);
    bool short_ok=true;
    for (size_t length : {size_t(0),size_t(7),size_t(63),size_t(80),size_t(0x215)}) {
        b=fixture(); b.resize(length);
        bool ok=reject_without_writes(path,b);
        short_ok &= ok; if (ok) ++r.rejected;
    }
    r.check("elf_truncated_images_rejected", short_ok);
    b=fixture(); put(b,56,3,2); put(b,ph,7,4); put(b,ph+8,0x200,8);
    put(b,ph+16,base,8); put(b,ph+32,4,8); put(b,ph+40,0x1000000,8);
    bool tls_overflow=reject_without_writes(path,b); if (tls_overflow) ++r.rejected;
    r.check("elf_tls_destination_overflow_rejected", tls_overflow);
    put(b,ph+40,4,8); put(b,ph+16,0x20000,8);
    bool tls_unmapped=reject_without_writes(path,b); if(tls_unmapped) ++r.rejected;
    r.check("elf_unmapped_tls_rejected", tls_unmapped);
    b=fixture(); put(b,40,UINT64_MAX-10,8); put(b,58,64,2); put(b,60,1,2);
    bool sections=reject_without_writes(path,b); if(sections) ++r.rejected;
    r.check("elf_overflowing_sections_rejected", sections);
    b=fixture();
    r.check("elf_valid_reload_after_rejections", write_image(path,b) &&
            d2_fixture_load_elf(path)==opd && vm_read32(base)==0x4e800020 && vm_read32(base+4)==0);
    d2_elf_tls_metadata(&va,&fs,&ms);
    r.check("elf_absent_tls_resets_metadata", va==0 && fs==0 && ms==0);
}
} // namespace

int d2_elf_probe_run(const char *directory, char *report, size_t capacity) {
    if (!report || capacity<2048) return 1;
    report[0]=0;
    Report r{report,capacity};
    r.line("elf_scope=pinned-sdk-validation-and-segment-copy-subset\n"
           "elf_input=internal-synthetic-fixture\nelf_full_ppu_loader_linked=false\n"
           "elf_guest_dispatch_available=false\nelf_code_executed=false\nelf_game_loaded=false\n");
    // A busy VM belongs to another owner: do not touch or release it.
    if (VM_READY || !directory || !*directory) {
        r.line("elf_error=vm-busy-or-no-private-directory\nelf_failures=1\nelf_status=failed\n");
        return 1;
    }
    try {
        Cleanup cleanup{"",ppu_vm_size};
        bool vm=vm_init()==CELL_OK;
        r.check("elf_private_vm",vm);
        if (vm) {
            // Commit the complete range accepted by this private fixture loader.
            // No uncommitted holes may be accepted by its memory-limit check.
            bool committed=vm_commit(0,0x10000)==CELL_OK &&
                           vm_commit(0x10010000,0x00ff0000)==CELL_OK;
            r.check("elf_fixture_range_committed",committed);
            if (committed) {
                ppu_vm_size=0x11000000;
                std::string pattern=std::string(directory)+"/d2-elf-fixture-XXXXXX";
                std::vector<char> name(pattern.begin(),pattern.end()); name.push_back(0);
                int fd=mkstemp(name.data());
                r.check("elf_private_fixture_file",fd>=0);
                if (fd>=0) {
                    cleanup.path=name.data();
                    bool closed=close(fd)==0; r.check("elf_fixture_fd_closed",closed);
                    if(closed) exercise(cleanup.path.c_str(),r);
                    bool removed=unlink(cleanup.path.c_str())==0;
                    r.check("elf_fixture_file_removed",removed);
                    if (removed) cleanup.path.clear();
                }
            }
        }
    } catch (const std::exception &e) {
        ++r.failures; r.line("elf_error=%s\n",e.what());
    }
    r.check("elf_vm_release",vm_base==nullptr);
    r.line("elf_rejected_fixtures=%u\nelf_failures=%u\nelf_status=%s\n",
           r.rejected,r.failures,r.failures ? "failed" : "passed");
    return r.failures ? 1 : 0;
}
