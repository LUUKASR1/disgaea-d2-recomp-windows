#include "iso_runtime.h"
#include "iso_vfs.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace {
using namespace d2::vfs;
constexpr std::uint32_t FirstHandle = 0x004D0000;
constexpr std::uint32_t HandleLimit = FirstHandle + 512;
constexpr std::uint32_t Ok = 0, Invalid = 0x80010002, NoMemory = 0x80010004,
    NotFound = 0x80010006, Fault = 0x8001000d, IsDir = 0x80010012,
    ReadOnly = 0x80010026, BadFd = 0x8001002a, Io = 0x8001002b,
    NotDir = 0x8001002e;
struct GuestError { std::uint32_t code; };
struct Stream {
    std::shared_ptr<IsoVfs::File> file;
    bool directory = false;
    std::vector<DirectoryEntry> entries;
    std::uint64_t position = 0;
    std::mutex mutex;
};
std::mutex tableMutex;
std::shared_ptr<IsoVfs> disc;
std::unordered_map<std::uint32_t, std::shared_ptr<Stream>> handles;
std::byte* memory = nullptr;
std::uint64_t memorySize = 0;
thread_local std::string lastError;

std::span<std::byte> guest(std::uint64_t address, std::uint64_t length) {
    if (!memory || !address || address > UINT32_MAX || address > memorySize ||
        length > memorySize - address || length > SIZE_MAX)
        throw GuestError{Fault};
    return {memory + address, static_cast<std::size_t>(length)};
}
void store(std::uint64_t address, std::uint64_t value, std::size_t bytes) {
    auto out = guest(address, bytes);
    for (std::size_t i = 0; i < bytes; ++i)
        out[i] = static_cast<std::byte>(value >> ((bytes - 1 - i) * 8));
}
std::uint64_t load(std::uint64_t address, std::size_t bytes) {
    auto input = guest(address, bytes); std::uint64_t value = 0;
    for (auto byte : input) value = (value << 8) | std::to_integer<unsigned>(byte);
    return value;
}
std::string pathAt(std::uint64_t address) {
    std::string path;
    for (std::uint64_t i = 0; i < 1024; ++i) {
        auto byte = std::to_integer<char>(guest(address + i, 1)[0]);
        if (!byte) return path;
        path += byte;
    }
    throw GuestError{Invalid};
}
bool mapPath(std::string path, std::string& internal) {
    std::replace(path.begin(), path.end(), '\\', '/');
    for (const std::string_view mount : {"/dev_bdvd", "/app_home"}) {
        if (path == mount) { internal = "/"; return true; }
        if (path.starts_with(std::string(mount) + "/")) {
            internal = path.substr(mount.size());
            // Device/directory paths commonly end with a slash.
            while (internal.size() > 1 && internal.back() == '/') internal.pop_back();
            return true;
        }
    }
    return false;
}
bool virtualFd(std::uint64_t value) {
    auto fd = static_cast<std::uint32_t>(value);
    return fd >= FirstHandle && fd < HandleLimit;
}
std::shared_ptr<Stream> getHandle(std::uint64_t value) {
    std::scoped_lock lock(tableMutex);
    auto it = handles.find(static_cast<std::uint32_t>(value));
    if (it == handles.end()) throw GuestError{BadFd};
    return it->second;
}
std::uint32_t addHandle(std::shared_ptr<Stream> stream) {
    std::scoped_lock lock(tableMutex);
    for (auto fd = FirstHandle; fd < HandleLimit; ++fd)
        if (!handles.contains(fd)) { handles.emplace(fd, std::move(stream)); return fd; }
    throw GuestError{NoMemory};
}
void writeStat(std::uint64_t address, bool directory, std::uint64_t size) {
    guest(address, 52); // CellFsStat is packed, 52 bytes, big-endian.
    store(address, (directory ? 0x4000u | 0555u : 0x8000u | 0444u), 4);
    store(address + 4, 0, 4); store(address + 8, 0, 4);
    for (unsigned offset : {12, 20, 28}) store(address + offset, 0, 8);
    store(address + 36, size, 8); store(address + 44, 2048, 8);
}
std::size_t readFile(Stream& stream, std::uint64_t offset,
                     std::uint64_t address, std::uint64_t length) {
    if (stream.directory) throw GuestError{IsDir};
    if (!length) return 0;
    auto output = guest(address, length);
    // Commit reserved guest pages before a Windows kernel read touches them.
    auto* touch = reinterpret_cast<volatile unsigned char*>(output.data());
    for (std::size_t i = 0; i < output.size(); i += 4096) touch[i] = touch[i];
    touch[output.size() - 1] = touch[output.size() - 1];
    return stream.file->ReadAt(offset, output);
}
std::int64_t status(std::uint32_t code) { return static_cast<std::int32_t>(code); }
}

extern "C" int d2_iso_mount(const char* image) {
    try {
        if (!image || !*image) throw std::runtime_error("Caminho da ISO vazio.");
        std::u8string imagePath;
        for (const auto* p = image; *p; ++p) imagePath += static_cast<char8_t>(*p);
        auto next = std::make_shared<IsoVfs>(std::filesystem::path(imagePath));
        std::scoped_lock lock(tableMutex);
        if (!handles.empty()) throw std::runtime_error("Feche os arquivos antes de trocar a ISO.");
        disc = std::move(next); lastError.clear(); return 0;
    } catch (const std::exception& error) { lastError = error.what(); return -1; }
}
extern "C" const char* d2_iso_last_error(void) { return lastError.c_str(); }
extern "C" int d2_iso_read_host(const char* path, std::uint64_t offset,
    std::uint64_t limit, unsigned char** bytes, std::uint64_t* count) {
    std::shared_ptr<IsoVfs> volume;
    { std::scoped_lock lock(tableMutex); volume = disc; }
    std::string internal;
    if (!volume || !path || !mapPath(path, internal)) return 0;
    if (!bytes || !count) return -1;
    *bytes = nullptr; *count = 0;
    unsigned char* allocated = nullptr;
    try {
        auto file = volume->Open(internal);
        if (offset > file->Size()) throw std::runtime_error("Offset fora do arquivo ISO.");
        auto size = file->Size() - offset;
        if (limit) size = std::min(size, limit);
        if (size > 64u * 1024u * 1024u) throw std::runtime_error("Imagem ISO excede limite de memoria.");
        allocated = static_cast<unsigned char*>(std::malloc(size ? static_cast<std::size_t>(size) : 1));
        if (!allocated) throw std::bad_alloc();
        auto output = std::span(reinterpret_cast<std::byte*>(allocated), static_cast<std::size_t>(size));
        if (file->ReadAt(offset, output) != size) throw std::runtime_error("Leitura ISO incompleta.");
        *bytes = allocated; *count = size; return 1;
    } catch (const std::exception& error) {
        std::free(allocated); lastError = error.what(); return -1;
    }
}
extern "C" int d2_iso_check_title(const char* expected) {
    try {
        std::shared_ptr<IsoVfs> volume;
        { std::scoped_lock lock(tableMutex); volume = disc; }
        if (!volume || !expected) throw std::runtime_error("ISO nao montada.");
        const auto title = volume->TitleId();
        if (title != expected) throw std::runtime_error("ISO " + title + "; este port requer " + expected + ".");
        return 0;
    } catch (const std::exception& error) { lastError = error.what(); return -1; }
}
extern "C" void d2_iso_bind_memory(void* base, std::uint64_t length) {
    memory = static_cast<std::byte*>(base);
    memorySize = std::min<std::uint64_t>(length, UINT64_C(0x100000000));
}

extern "C" int d2_iso_is_handle(std::uint32_t fd) {
    std::scoped_lock lock(tableMutex); return disc && virtualFd(fd);
}
extern "C" int d2_iso_read_at_guest(std::uint32_t fd, std::uint64_t offset,
    std::uint32_t buffer, std::uint64_t length, std::uint64_t* count, std::int64_t* result) {
    if (!d2_iso_is_handle(fd)) return 0;
    *count = 0;
    try {
        auto stream = getHandle(fd); std::scoped_lock lock(stream->mutex);
        *count = readFile(*stream, offset, buffer, length); *result = 0;
    } catch (const GuestError& error) { *result = status(error.code);
    } catch (const std::bad_alloc&) { *result = status(NoMemory);
    } catch (const std::exception& error) { lastError = error.what(); *result = status(Io); }
    return 1;
}

extern "C" int d2_iso_request(int op, const std::uint64_t a[6], std::int64_t* result) {
    std::shared_ptr<IsoVfs> volume;
    { std::scoped_lock lock(tableMutex); volume = disc; }
    if (!volume) return 0;
    bool owned = false;
    try {
        std::string path;
        const bool byPath = op == D2_ISO_OPEN || op == D2_ISO_STAT || op == D2_ISO_OPENDIR ||
            op == D2_ISO_PATH_BLOCK_SIZE || op == D2_ISO_MKDIR || op == D2_ISO_RMDIR ||
            op == D2_ISO_UNLINK || op == D2_ISO_RENAME;
        if (op == D2_ISO_AIO_READ || op == D2_ISO_AIO_WRITE) {
            auto fd = load(a[0], 4);
            if (!virtualFd(fd)) return 0;
            owned = true;
            // The PPU scaffold owns guest callback scheduling. Its AIO body
            // is adapted separately to call D2_ISO_READ_AT atomically.
            throw GuestError{Io};
        }
        if (byPath) {
            if (!mapPath(pathAt(a[0]), path)) {
                if (op != D2_ISO_RENAME || !mapPath(pathAt(a[1]), path)) return 0;
            }
        } else if (!virtualFd(a[0])) return 0;
        owned = true;
        switch (op) {
        case D2_ISO_OPEN: {
            guest(a[2], 4);
            if ((a[1] & 3u) || (a[1] & (000100u | 001000u | 002000u))) throw GuestError{ReadOnly};
            auto stream = std::make_shared<Stream>(); stream->file = volume->Open(path);
            store(a[2], addHandle(std::move(stream)), 4); break;
        }
        case D2_ISO_OPENDIR: {
            guest(a[1], 4);
            auto stream = std::make_shared<Stream>(); stream->directory = true;
            stream->entries = volume->ListDirectory(path);
            store(a[1], addHandle(std::move(stream)), 4); break;
        }
        case D2_ISO_CLOSE: case D2_ISO_CLOSEDIR: {
            auto stream = getHandle(a[0]);
            if (stream->directory != (op == D2_ISO_CLOSEDIR)) throw GuestError{BadFd};
            std::scoped_lock lock(tableMutex); handles.erase(static_cast<std::uint32_t>(a[0])); break;
        }
        case D2_ISO_READ: case D2_ISO_READ_AT: {
            const bool positioned = op == D2_ISO_READ_AT;
            auto stream = getHandle(a[0]); std::scoped_lock lock(stream->mutex);
            const auto countAddress = a[positioned ? 4 : 3];
            if (countAddress) guest(countAddress, 8);
            const auto offset = positioned ? a[1] : stream->position;
            auto count = readFile(*stream, offset, a[positioned ? 2 : 1], a[positioned ? 3 : 2]);
            if (!positioned) stream->position += count;
            if (countAddress) store(countAddress, count, 8); break;
        }
        case D2_ISO_SEEK: {
            guest(a[3], 8);
            auto stream = getHandle(a[0]); std::scoped_lock lock(stream->mutex);
            if (stream->directory) throw GuestError{BadFd};
            std::uint64_t base;
            if (a[2] == 0) base = 0;
            else if (a[2] == 1) base = stream->position;
            else if (a[2] == 2) base = stream->file->Size();
            else throw GuestError{Invalid};
            const auto delta = static_cast<std::int64_t>(a[1]);
            if (delta < 0) {
                auto magnitude = std::uint64_t(-(delta + 1)) + 1;
                if (magnitude > base) throw GuestError{Invalid};
                base -= magnitude;
            } else {
                if (base > INT64_MAX - std::uint64_t(delta)) throw GuestError{Invalid};
                base += static_cast<std::uint64_t>(delta);
            }
            stream->position = base; store(a[3], base, 8); break;
        }
        case D2_ISO_STAT: {
            auto info = volume->Stat(path); writeStat(a[1], info.directory, info.size); break;
        }
        case D2_ISO_FSTAT: {
            auto stream = getHandle(a[0]);
            writeStat(a[1], stream->directory, stream->directory ? 0 : stream->file->Size()); break;
        }
        case D2_ISO_READDIR: {
            auto stream = getHandle(a[0]); std::scoped_lock lock(stream->mutex);
            if (!stream->directory) throw GuestError{BadFd};
            guest(a[1], 258); if (a[2]) guest(a[2], 8);
            if (stream->position >= stream->entries.size()) { if (a[2]) store(a[2], 0, 8); break; }
            const auto& entry = stream->entries[static_cast<std::size_t>(stream->position)];
            if (entry.name.size() > 255) throw GuestError{Invalid};
            auto output = guest(a[1], 258); std::fill(output.begin(), output.end(), std::byte{});
            output[0] = static_cast<std::byte>(entry.directory ? 1 : 2);
            output[1] = static_cast<std::byte>(entry.name.size());
            std::memcpy(output.data() + 2, entry.name.data(), entry.name.size());
            if (a[2]) store(a[2], 258, 8); ++stream->position; break;
        }
        case D2_ISO_BLOCK_SIZE: getHandle(a[0]); [[fallthrough]];
        case D2_ISO_PATH_BLOCK_SIZE:
            if (a[1]) store(a[1], 2048, 8); if (a[2]) store(a[2], 2048, 8); break;
        case D2_ISO_FSYNC: getHandle(a[0]); break;
        case D2_ISO_WRITE: case D2_ISO_TRUNCATE:
            getHandle(a[0]); throw GuestError{ReadOnly};
        case D2_ISO_MKDIR: case D2_ISO_RMDIR: case D2_ISO_UNLINK: case D2_ISO_RENAME:
            throw GuestError{ReadOnly};
        default: throw GuestError{Invalid};
        }
        *result = status(Ok); return 1;
    } catch (const GuestError& error) {
        *result = status(error.code); return 1;
    } catch (const Error& error) {
        lastError = error.what();
        std::uint32_t code = Io;
        if (error.code == ErrorCode::NotFound) code = NotFound;
        else if (error.code == ErrorCode::InvalidPath) code = Invalid;
        else if (error.code == ErrorCode::IsDirectory) code = IsDir;
        else if (error.code == ErrorCode::NotDirectory) code = NotDir;
        *result = status(code); return 1;
    } catch (const std::bad_alloc&) { *result = status(NoMemory); return 1;
    } catch (const std::exception& error) {
        lastError = error.what(); *result = status(Io); return owned ? 1 : 0;
    }
}
