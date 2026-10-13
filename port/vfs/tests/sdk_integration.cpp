/* Executes the real, adapted filesystem entry points with synthetic assets.
 * CPU dispatch/NPDRM/PRX services are stubs: these tests do not run game code. */
#include "iso_runtime.h"
#include "ppu_context.h"
#include "cellFs.h"
#include "ps3emu/guest_call.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

static std::array<std::uint8_t, 65536> ram{};
static std::unordered_map<std::string, void(*)(ppu_context*)> hle;
static bool aioCompleted = false;
static void callback(std::uint32_t opd, std::uint64_t aio, std::uint64_t error,
    std::uint64_t, std::uint64_t count, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) {
    aioCompleted = opd == 0x888 && aio == 1500 && error == 0 && count == 200;
}

extern "C" {
std::uint8_t* vm_base = ram.data();
std::uint32_t ppu_vm_size = ram.size();
int g_resv_store_active = 0;
std::uint32_t g_ww_lo = 0, g_ww_hi = 0;
ps3_guest_caller_fn g_ps3_guest_caller = callback;
extern const char* ppu_vfs_root;
void ppu_resv_break_store(std::uint64_t) {}
int spu_coh_is_reserved(std::uint32_t) { return 0; }
void spu_coh_notify_write(std::uint32_t) {}
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
void ps3_ww_report_inline(std::uint32_t, std::uint64_t, int) {}
unsigned ppu_prof_resolve_host(void*) { return 0; }
void ppu_guest_caller(char* out, std::size_t size) { if (size) out[0] = 0; }
void ydkj_host_bt(const char*) {}
const char* edat_resolve(const char* path, char*, std::size_t) { return path; }
const char* edat_resolve_key(const char* path, char*, std::size_t, const std::uint8_t*) { return path; }
int edat_is_npd(const char*) { return 0; }
std::uint32_t prx_loaded_module_id(const char*) { return 0; }
void ps3_hle_register_ctx(std::uint32_t, const char* name, void(*fn)(ppu_context*)) { hle[name] = fn; }
void vm_write32(std::uint64_t address, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) ram.at(address + i) = static_cast<std::uint8_t>(value >> ((3 - i) * 8));
}
void vm_write64(std::uint64_t address, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) ram.at(address + i) = static_cast<std::uint8_t>(value >> ((7 - i) * 8));
}
void ppu_fs_register(void);
std::int64_t sys_fs_open(ppu_context*);
std::int64_t sys_fs_read(ppu_context*);
std::int64_t sys_fs_close(ppu_context*);
std::int64_t sys_fs_lseek(ppu_context*);
std::int64_t sys_fs_stat(ppu_context*);
}

static unsigned checks = 0;
static void check(bool ok, const char* name) {
    if (!ok) throw std::runtime_error(name); ++checks;
}
static std::uint64_t be(unsigned address, unsigned count) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value = (value << 8) | ram.at(address + i);
    return value;
}
static ppu_context context(std::initializer_list<std::uint64_t> args) {
    ppu_context ctx{}; unsigned reg = 3;
    for (auto arg : args) ctx.gpr[reg++] = arg;
    return ctx;
}
static void path(const char* text) { std::strcpy(reinterpret_cast<char*>(ram.data() + 64), text); }
static std::uint32_t call(const char* name, std::initializer_list<std::uint64_t> args) {
    auto ctx = context(args); hle.at(name)(&ctx); return static_cast<std::uint32_t>(ctx.gpr[3]);
}

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3) return 2;
        std::filesystem::path root(argv[1]);
        auto imagePath = argc == 3 ? root / argv[2] : root / std::filesystem::path(u8"RETRÔ-ação") / "D2.iso";
        auto image = imagePath.u8string();
        std::string imageUtf8(reinterpret_cast<const char*>(image.data()), image.size());
        d2_iso_bind_memory(ram.data(), ram.size());
        check(d2_iso_mount(imageUtf8.c_str()) == 0, "mount");
        ppu_fs_register();
        path("/dev_bdvd/PS3_GAME/USRDIR/Data/fragment.bin");
        check(call("cellFsOpen", {64, 0, 512}) == 0, "real PPU cellFsOpen hook");
        auto fd = be(512, 4);
        check(call("cellFsRead", {fd, 2048, 200, 520}) == 0 && be(520, 8) == 200, "real PPU read hook");
        check(ram[2048] == 3 && ram[2049] == 20, "PPU payload");
        check(call("cellFsLseek", {fd, 4000, 0, 520}) == 0 && be(520, 8) == 4000, "PPU seek hook");
        check(call("cellFsFstat", {fd, 1000}) == 0 && be(1036, 8) == 4865, "PPU fstat hook");
        vm_write32(1500, static_cast<std::uint32_t>(fd)); vm_write64(1508, 4000);
        vm_write32(1516, 2048); vm_write64(1524, 200);
        check(call("cellFsAioRead", {1500, 1600, 0x888}) == 0 && aioCompleted, "real AIO callback and byte count");
        check(call("cellFsLseek", {fd, 0, 1, 520}) == 0 && be(520, 8) == 4000, "AIO preserves cursor");
        check(call("cellFsClose", {fd}) == 0, "PPU close");
        auto ctx = context({64, 0, 512});
        check(sys_fs_open(&ctx) == 0, "real LV2 open hook"); fd = be(512, 4);
        ctx = context({fd, 2048, 200, 520});
        check(sys_fs_read(&ctx) == 0 && be(520, 8) == 200, "real LV2 read hook");
        ctx = context({fd, UINT64_C(0x100000000), 0, 520});
        check(sys_fs_lseek(&ctx) == 0 && be(520, 8) == UINT64_C(0x100000000), "LV2 64-bit seek hook");
        ctx = context({fd}); check(sys_fs_close(&ctx) == 0, "LV2 close hook");
        check(cellFsOpen(reinterpret_cast<const char*>(64), 0, reinterpret_cast<CellFsFd*>(512), nullptr, 0) == 0,
            "real direct cellFs library hook"); fd = be(512, 4);
        check(cellFsRead(static_cast<CellFsFd>(fd), reinterpret_cast<void*>(2048), 200,
            reinterpret_cast<u64*>(520)) == 0 && be(520, 8) == 200, "direct library read hook");
        check(cellFsClose(static_cast<CellFsFd>(fd)) == 0, "direct library close hook");

        // All three entry routes must preserve the real writable HDD backend.
        auto hdd = root / "hdd"; std::filesystem::create_directories(hdd);
        { std::ofstream file(hdd / "save.bin", std::ios::binary); file << "SAVE"; }
#ifdef _WIN32
        _putenv_s("PS3_HDD0_ROOT", hdd.string().c_str());
#else
        setenv("PS3_HDD0_ROOT", hdd.string().c_str(), 1);
#endif
        path("/dev_hdd0/save.bin");
        check(call("cellFsOpen", {64, 0, 512}) == 0, "PPU native HDD fallback"); fd = be(512, 4);
        check(call("cellFsRead", {fd, 2048, 4, 520}) == 0 && ram[2048] == 'S', "PPU HDD bytes");
        check(call("cellFsClose", {fd}) == 0, "PPU HDD close");
        ctx = context({64, 0, 512}); check(sys_fs_open(&ctx) == 0, "LV2 native HDD fallback"); fd = be(512, 4);
        ctx = context({fd, 2048, 4, 520}); check(sys_fs_read(&ctx) == 0 && ram[2048] == 'S', "LV2 HDD bytes");
        ctx = context({fd}); check(sys_fs_close(&ctx) == 0, "LV2 HDD close");
        check(cellFsOpen(reinterpret_cast<const char*>(64), 0, reinterpret_cast<CellFsFd*>(512), nullptr, 0) == 0,
            "direct library HDD fallback"); fd = be(512, 4);
        check(cellFsRead(static_cast<CellFsFd>(fd), reinterpret_cast<void*>(2048), 4,
            reinterpret_cast<u64*>(520)) == 0 && ram[2048] == 'S', "direct library HDD bytes");
        check(cellFsClose(static_cast<CellFsFd>(fd)) == 0, "direct library HDD close");
        std::cout << checks << " SDK integration checks passed; PPU/LV2/direct library/AIO/HDD.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
