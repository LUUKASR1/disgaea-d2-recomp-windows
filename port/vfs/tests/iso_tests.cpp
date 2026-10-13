#include "iso_vfs.h"
#include "iso_runtime.h"
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

using namespace d2::vfs;
namespace {
unsigned checks = 0;
void check(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(name);
    ++checks;
}
template<class F> void expect(ErrorCode code, F&& fn, const char* name) {
    try { fn(); } catch (const Error& error) { check(error.code == code, name); return; }
    throw std::runtime_error(std::string("Expected failure: ") + name);
}
std::vector<std::byte> readHost(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(input)), {});
    std::vector<std::byte> result(raw.size()); std::memcpy(result.data(), raw.data(), raw.size());
    return result;
}
std::string utf8(const std::filesystem::path& path) {
    auto bytes = path.u8string(); return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
std::array<std::byte, 65536> vm{};
void putPath(const char* path) { std::memcpy(vm.data() + 64, path, std::strlen(path) + 1); }
std::uint64_t be(std::size_t at, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) value = (value << 8) | std::to_integer<unsigned>(vm[at + i]);
    return value;
}
std::int64_t request(int op, std::array<std::uint64_t, 6> args) {
    std::int64_t result = 123;
    check(d2_iso_request(op, args.data(), &result) == 1, "ISO must own request"); return result;
}
bool code(std::int64_t result, std::uint32_t expected) {
    return static_cast<std::uint32_t>(result) == expected;
}
}

int main(int count, char** args) {
    try {
        if (count != 2) return 2;
        const std::filesystem::path root(args[1]);
        const auto image = root / std::filesystem::path(u8"RETRÔ-ação") / "D2.iso";
        const auto expected = readHost(root / "expected.bin");
        IsoVfs disc(image);
        const auto udfLayout = InspectImage(image);
        check(udfLayout.udfRecognition && udfLayout.udfAnchor && !udfLayout.iso9660Primary, "UDF format inspection");
        const auto isoLayout = InspectImage(root / "iso9660.iso");
        check(isoLayout.iso9660Primary && !isoLayout.udfRecognition && !isoLayout.udfAnchor, "ISO9660-only inspection");
        check(isoLayout.Description().find("nao comprova criptografia") != std::string::npos, "format evidence does not diagnose encryption");
        const auto bridgeLayout = InspectImage(root / "bridge.iso");
        check(bridgeLayout.iso9660Primary && bridgeLayout.udfRecognition && bridgeLayout.udfAnchor, "ISO9660/UDF bridge inspection");
        const auto unknownLayout = InspectImage(root / "bad.iso");
        check(!unknownLayout.iso9660Primary && !unknownLayout.udfRecognition && !unknownLayout.udfAnchor, "unrecognized format stays unknown");
        check(disc.Stat("/").directory, "root directory");
        check(disc.Stat("/PS3_GAME/USRDIR/Data").directory, "nested directory");
        check(disc.Stat("/PS3_GAME/PARAM.SFO").size == 58, "inline file size");
        check(disc.TitleId() == "BLUS31313", "bounded SFO TITLE_ID parser");
        check(disc.GetFileBytes("/PS3_GAME/PARAM.SFO")[1] == std::byte{'P'}, "inline file bytes");
        check(disc.GetFileBytes("/PS3_GAME/USRDIR/Data/fragment.bin") == expected, "short fragmented extents");
        check(disc.GetFileBytes("PS3_GAME\\USRDIR\\Data\\long.bin") == expected, "long fragmented extents and separators");
        check(disc.GetFileBytes("/PS3_GAME/USRDIR/Data/empty.bin").empty(), "empty file is valid");
        check(disc.GetFileBytes("/PS3_GAME/USRDIR/Data/Olá.bin").size() == 4, "CS0 Unicode name");
        check(disc.ListDirectory("/PS3_GAME/USRDIR/Data").size() == 5, "directory enumeration");
        auto file = disc.Open("/PS3_GAME/USRDIR/Data/fragment.bin");
        std::array<std::byte, 200> range{};
        check(file->ReadAt(4000, range) == range.size(), "unaligned read across fragments");
        check(std::equal(range.begin(), range.end(), expected.begin() + 4000), "cross-fragment bytes");
        check(file->ReadAt(4800, range) == 65, "partial EOF read");
        check(file->ReadAt(UINT64_MAX, range) == 0, "huge EOF offset without overflow");
        check(file->ReadAt(0, {}) == 0, "empty buffer");
        expect(ErrorCode::TooLarge, [&]{ disc.GetFileBytes("/PS3_GAME/USRDIR/Data/fragment.bin", 64); }, "size cap");
        expect(ErrorCode::NotFound, [&]{ disc.Open("/missing.bin"); }, "missing file");
        expect(ErrorCode::IsDirectory, [&]{ disc.Open("/PS3_GAME"); }, "directory opened as file");
        expect(ErrorCode::NotFound, [&]{ disc.Open("/ps3_game/PARAM.SFO"); }, "case is preserved");
        for (const auto* path : {"", "../PS3_GAME/PARAM.SFO", "/PS3_GAME/../PARAM.SFO", "/PS3_GAME/./PARAM.SFO", "//PS3_GAME"})
            expect(ErrorCode::InvalidPath, [&]{ disc.Open(path); }, "invalid/traversal path");
        expect(ErrorCode::InvalidPath, [&]{ disc.Open(std::string("/PS3_GAME\0suffix", 16)); }, "embedded NUL");
        expect(ErrorCode::InvalidImage, [&]{ IsoVfs invalid(root / "bad.iso"); }, "invalid image");
        expect(ErrorCode::InvalidImage, [&]{ IsoVfs invalid(root / "unaligned.iso"); }, "unaligned image");
        expect(ErrorCode::InvalidImage, [&]{ IsoVfs invalid(root / "bad-sfo.iso"); invalid.TitleId(); }, "SFO offsets checked");
        IsoVfs large(root / "large.iso");
        auto late = large.GetFileBytes("/PS3_GAME/USRDIR/Data/late.bin");
        check(late.size() == 4 && late[0] == std::byte{'L'} && late[3] == std::byte{'E'}, "physical ISO offset >4GiB");
        std::shared_ptr<IsoVfs::File> survivor;
        { IsoVfs temporary(image); survivor = temporary.Open("/PS3_GAME/USRDIR/Data/fragment.bin"); }
        check(survivor->ReadAt(0, range) == range.size(), "file retains mounted volume lifetime");
        std::atomic<bool> parallelOk{true}; std::vector<std::thread> workers;
        for (unsigned t = 0; t < 8; ++t) workers.emplace_back([&, t] {
            try {
                for (unsigned i = 0; i < 100; ++i) {
                    std::array<std::byte, 127> out{}; auto offset = (t * 97 + i * 31) % 4700;
                    if (file->ReadAt(offset, out) != out.size() ||
                        !std::equal(out.begin(), out.end(), expected.begin() + offset)) parallelOk = false;
                }
            } catch (...) { parallelOk = false; }
        });
        for (auto& worker : workers) worker.join();
        check(parallelOk, "800 concurrent positioned reads");

        d2_iso_bind_memory(vm.data(), vm.size());
        std::array<std::uint64_t, 6> noMount{}; std::int64_t result = 99;
        check(d2_iso_request(D2_ISO_STAT, noMount.data(), &result) == 0 && result == 99, "unmounted backend unchanged");
        check(d2_iso_mount(utf8(image).c_str()) == 0, "mount Unicode path through C bridge");
        check(d2_iso_check_title("BLUS31313") == 0 && d2_iso_check_title("BLES00000") != 0, "runtime title compatibility gate");
        putPath("/dev_hdd0/home/00000001/savedata/D2");
        check(d2_iso_request(D2_ISO_STAT, std::array<std::uint64_t, 6>{64, 1000}.data(), &result) == 0, "save backend unchanged");
        putPath("/dev_hdd0/game/BLUS31313/USRDIR/Data/START_7.dat");
        check(d2_iso_request(D2_ISO_STAT, std::array<std::uint64_t, 6>{64, 1000}.data(), &result) == 0, "update backend unchanged");
        putPath("/dev_bdvd/PS3_GAME/USRDIR/Data/fragment.bin");
        check(request(D2_ISO_OPEN, {64, 0, 512}) == 0, "guest open");
        auto fd = be(512, 4);
        check(d2_iso_mount(utf8(image).c_str()) != 0, "no remount with live handles");
        check(request(D2_ISO_READ, {fd, 2048, 200, 520}) == 0 && be(520, 8) == 200, "guest read count big-endian");
        check(std::equal(vm.begin() + 2048, vm.begin() + 2248, expected.begin()), "guest read payload");
        check(request(D2_ISO_READ_AT, {fd, 4000, 2048, 200, 520}) == 0, "guest positioned read");
        std::uint64_t aioCount = 0; std::int64_t aioResult = 99;
        check(d2_iso_read_at_guest(static_cast<std::uint32_t>(fd), 4000, 2048, 200, &aioCount, &aioResult)
            && aioCount == 200 && aioResult == 0, "AIO positioned adapter");
        check(request(D2_ISO_SEEK, {fd, 0, 1, 520}) == 0 && be(520, 8) == 200, "ReadAt preserves stream cursor");
        check(request(D2_ISO_SEEK, {fd, UINT64_C(0x100000000), 0, 520}) == 0 && be(520, 8) == UINT64_C(0x100000000), "64-bit guest seek");
        check(request(D2_ISO_READ, {fd, 2048, 200, 520}) == 0 && be(520, 8) == 0, "seek beyond EOF");
        check(code(request(D2_ISO_SEEK, {fd, UINT64_MAX, 0, 520}), 0x80010002), "negative absolute seek rejected");
        check(request(D2_ISO_SEEK, {fd, 0, 0, 520}) == 0, "rewind");
        check(code(request(D2_ISO_READ, {fd, vm.size() - 1, 200, 520}), 0x8001000d), "guest buffer bounds");
        check(code(request(D2_ISO_READ, {fd, 2048, 200, vm.size() - 1}), 0x8001000d), "count bounds before read");
        check(request(D2_ISO_SEEK, {fd, 0, 1, 520}) == 0 && be(520, 8) == 0, "failed read does not advance");
        std::fill(vm.begin() + 1000, vm.begin() + 1060, std::byte{0x55});
        check(request(D2_ISO_FSTAT, {fd, 1000}) == 0 && be(1036, 8) == expected.size(), "packed guest fstat");
        check(vm[1052] == std::byte{0x55}, "stat does not overwrite following field");
        check(code(request(D2_ISO_WRITE, {fd, 2048, 1, 520}), 0x80010026), "disc writes rejected");
        check(code(request(D2_ISO_OPEN, {64, 2, 512}), 0x80010026), "read-write opens rejected");
        check(request(D2_ISO_CLOSE, {fd}) == 0, "close file");
        check(code(request(D2_ISO_READ, {fd, 2048, 10, 520}), 0x8001002a), "closed descriptor rejected");
        putPath("/app_home/PS3_GAME/USRDIR/Data/");
        check(request(D2_ISO_OPENDIR, {64, 512}) == 0, "guest directory trailing slash");
        auto dir = be(512, 4);
        for (unsigned i = 0; i < 5; ++i)
            check(request(D2_ISO_READDIR, {dir, 2048, 520}) == 0 && be(520, 8) == 258, "guest directory entry");
        check(request(D2_ISO_READDIR, {dir, 2048, 520}) == 0 && be(520, 8) == 0, "guest directory EOF");
        check(request(D2_ISO_CLOSEDIR, {dir}) == 0, "close directory");
        putPath("/dev_bdvd/PS3_GAME/../PARAM.SFO");
        check(code(request(D2_ISO_OPEN, {64, 0, 512}), 0x80010002), "guest traversal rejected");
        putPath("/dev_bdvd/missing.bin");
        check(code(request(D2_ISO_OPEN, {64, 0, 512}), 0x80010006), "missing ISO file cannot fall back to stale host file");
        check(code(request(D2_ISO_UNLINK, {64}), 0x80010026), "disc removal rejected");
        check(request(D2_ISO_PATH_BLOCK_SIZE, {64, 1000, 1008}) == 0 && be(1000, 8) == 2048, "disc block size");
        check(d2_iso_mount(utf8(image).c_str()) == 0, "remount after all handles close");
        std::cout << checks << " checks passed; 800 concurrent reads; >4GiB physical offsets.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
