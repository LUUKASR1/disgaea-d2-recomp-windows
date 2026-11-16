#include "iso_vfs.h"
#include "iso9660_reader.h"
#include "udfread.h"
#include "blockinput.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <limits>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace d2::vfs {
namespace {
constexpr std::uint64_t BlockSize = UDF_BLOCK_SIZE;
static_assert(sizeof(std::size_t) >= 8, "ISO VFS requires a 64-bit host.");

// Callback wrapper is standard-layout; API is its first member.
struct Input {
    udfread_block_input api{};
    void* owner{};
};
class NativeImage {
public:
    explicit NativeImage(const std::filesystem::path& path) {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_RANDOM_ACCESS, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw Error(ErrorCode::Io, "Nao foi possivel abrir a ISO.");
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(handle_, &length) || length.QuadPart < 0) {
            CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE;
            throw Error(ErrorCode::Io, "Nao foi possivel consultar o tamanho da ISO.");
        }
        size_ = static_cast<std::uint64_t>(length.QuadPart);
#else
        handle_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (handle_ < 0) throw Error(ErrorCode::Io, "Nao foi possivel abrir a ISO.");
        struct stat info{};
        if (::fstat(handle_, &info) || !S_ISREG(info.st_mode) || info.st_size < 0) {
            ::close(handle_); handle_ = -1;
            throw Error(ErrorCode::Io, "ISO deve ser um arquivo regular.");
        }
        size_ = static_cast<std::uint64_t>(info.st_size);
#endif
    }
    ~NativeImage() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (handle_ >= 0) ::close(handle_);
#endif
    }
    NativeImage(const NativeImage&) = delete;
    NativeImage& operator=(const NativeImage&) = delete;
    std::uint64_t Size() const noexcept { return size_; }

    bool ReadExact(std::uint64_t offset, void* buffer, std::size_t size) noexcept {
        if (offset > size_ || size > size_ - offset) return false;
        auto* output = static_cast<std::byte*>(buffer);
        while (size) {
            const auto count = std::min<std::size_t>(size, 1024u * 1024u);
#ifdef _WIN32
            OVERLAPPED request{};
            request.Offset = static_cast<DWORD>(offset);
            request.OffsetHigh = static_cast<DWORD>(offset >> 32);
            request.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!request.hEvent) return false;
            DWORD got = 0;
            BOOL success = ReadFile(handle_, output, static_cast<DWORD>(count), &got, &request);
            if (!success && GetLastError() == ERROR_IO_PENDING)
                success = GetOverlappedResult(handle_, &request, &got, TRUE);
            CloseHandle(request.hEvent);
            if (!success || !got) return false;
#else
            ssize_t got;
            do { got = ::pread(handle_, output, count, static_cast<off_t>(offset)); }
            while (got < 0 && errno == EINTR);
            if (got <= 0) return false;
#endif
            offset += static_cast<std::size_t>(got);
            output += got; size -= static_cast<std::size_t>(got);
        }
        return true;
    }
private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
    std::uint64_t size_ = 0;
};
}

std::string ImageLayout::Description() const {
    if (iso9660Primary && !udfRecognition && !udfAnchor)
        return "ISO9660 identificado; UDF nao identificado nos descritores inspecionados. "
               "O leitor tentara a estrutura ISO9660. Isso nao comprova criptografia.";
    if (udfRecognition || udfAnchor)
        return "Descritores UDF presentes. Uma falha de montagem requer verificar estruturas/particoes "
               "e o suporte do leitor; este diagnostico nao determina criptografia dos assets.";
    return "Formato nao identificado nos descritores inspecionados. Pode ser formato nao suportado, "
           "imagem truncada/danificada ou dados criptografados; a causa nao foi determinada.";
}

ImageLayout InspectImage(const std::filesystem::path& image) {
    NativeImage input(image);
    ImageLayout layout;
    layout.bytes = input.Size(); layout.aligned = layout.bytes % BlockSize == 0;
    std::array<std::uint8_t, UDF_BLOCK_SIZE> block{};
    for (std::uint64_t sector = 16; sector < 64 && sector < layout.bytes / BlockSize; ++sector) {
        if (!input.ReadExact(sector * BlockSize, block.data(), block.size()))
            throw Error(ErrorCode::Io, "Falha ao ler descritores da ISO.");
        if (!std::memcmp(block.data() + 1, "CD001", 5) && block[6] == 1) {
            auto text = "sector=" + std::to_string(sector) + " ISO9660 CD001 type=" + std::to_string(block[0]);
            if (block[0] == 1) {
                const auto logicalBlock = std::uint16_t(block[128]) | (std::uint16_t(block[129]) << 8);
                const auto blockBigEndian = (std::uint16_t(block[130]) << 8) | block[131];
                if (logicalBlock == 2048 && blockBigEndian == logicalBlock) {
                    layout.iso9660Primary = true;
                    const auto rootLba = std::uint32_t(block[158]) | (std::uint32_t(block[159]) << 8) |
                        (std::uint32_t(block[160]) << 16) | (std::uint32_t(block[161]) << 24);
                    text += " logical_block=2048 root_lba=" + std::to_string(rootLba);
                } else text += " invalid_logical_block=" + std::to_string(logicalBlock);
            }
            layout.descriptors.push_back(std::move(text));
        }
        for (const char* signature : {"BEA01", "NSR02", "NSR03", "TEA01"}) {
            if (!std::memcmp(block.data() + 1, signature, 5) && block[0] == 0 && block[6] == 1) {
                layout.descriptors.push_back("sector=" + std::to_string(sector) + " UDF " + signature);
                if (!std::strcmp(signature, "NSR02") || !std::strcmp(signature, "NSR03"))
                    layout.udfRecognition = true;
            }
        }
    }
    const auto sectors = layout.bytes / BlockSize;
    if (sectors > 256) {
        for (const auto sector : {UINT64_C(256), sectors - 257, sectors - 1}) {
            if (!input.ReadExact(sector * BlockSize, block.data(), block.size())) continue;
            if (block[0] != 2 || block[1] != 0) continue;
            unsigned sum = 0;
            for (unsigned i = 0; i < 16; ++i) if (i != 4) sum += block[i];
            if ((sum & 255) != block[4]) continue;
            layout.udfAnchor = true;
            layout.descriptors.push_back("sector=" + std::to_string(sector) + " UDF anchor tag checksum OK");
        }
    }
    return layout;
}

struct IsoVfs::State {
    explicit State(const std::filesystem::path& path) : image(path) {
        if (image.Size() % BlockSize || image.Size() / BlockSize > UINT32_MAX ||
            image.Size() < 17 * BlockSize)
            throw Error(ErrorCode::InvalidImage, "Tamanho de imagem ISO invalido.");
        input.owner = &image;
        input.api.size = [](udfread_block_input* p) -> std::uint32_t {
            return static_cast<std::uint32_t>(
                static_cast<NativeImage*>(reinterpret_cast<Input*>(p)->owner)->Size() / BlockSize);
        };
        input.api.read = [](udfread_block_input* p, std::uint32_t lba,
                            void* buffer, std::uint32_t blocks, int) -> int {
            auto& source = *static_cast<NativeImage*>(reinterpret_cast<Input*>(p)->owner);
            if (blocks > INT_MAX) return -1;
            return source.ReadExact(std::uint64_t(lba) * BlockSize, buffer,
                static_cast<std::size_t>(blocks) * BlockSize) ? static_cast<int>(blocks) : -1;
        };
        volume = udfread_init();
        if (!volume) throw Error(ErrorCode::Io, "Falha ao alocar leitor UDF.");
        if (udfread_open_input(volume, &input.api) < 0) {
            udfread_close(volume); volume = nullptr;
            try {
                iso9660 = std::make_unique<detail::Iso9660Reader>(
                    [this](std::uint64_t offset, void* buffer, std::size_t size) {
                        return image.ReadExact(offset, buffer, size);
                    }, image.Size());
            } catch (const Error& error) {
                throw Error(ErrorCode::InvalidImage,
                    std::string("UDF nao montado; alternativa ISO9660 falhou: ") + error.what());
            }
        }
    }
    ~State() { if (volume) udfread_close(volume); }
    NativeImage image;
    Input input;
    udfread* volume = nullptr;
    std::mutex mutex;
    std::unique_ptr<detail::Iso9660Reader> iso9660;
};
struct IsoVfs::File::Handle {
    std::shared_ptr<State> state;
    UDFFILE* file = nullptr;
    std::uint64_t size = 0;
    detail::IsoNode isoNode;
    ~Handle() { if (file) { std::scoped_lock lock(state->mutex); udfread_file_close(file); } }
};

std::string IsoVfs::NormalizePath(std::string_view input) {
    if (input.empty() || input.size() > 4096 || input.find('\0') != input.npos)
        throw Error(ErrorCode::InvalidPath, "Caminho interno invalido.");
    std::string path(input);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.front() != '/') path.insert(path.begin(), '/');
    if (path == "/") return path;
    for (std::size_t start = 1; start <= path.size();) {
        auto end = path.find('/', start);
        auto part = std::string_view(path).substr(start,
            end == path.npos ? path.size() - start : end - start);
        if (part.empty() || part == "." || part == "..")
            throw Error(ErrorCode::InvalidPath, "Componente de caminho invalido.");
        if (end == path.npos) break;
        start = end + 1;
    }
    return path;
}

IsoVfs::IsoVfs(const std::filesystem::path& path) : state_(std::make_shared<State>(path)) {}
std::string IsoVfs::BackendName() const { return state_->iso9660 ? "ISO9660" : "UDF"; }
IsoVfs::File::File(std::shared_ptr<Handle> h) : handle_(std::move(h)) {}
std::uint64_t IsoVfs::File::Size() const noexcept { return handle_->size; }

std::shared_ptr<IsoVfs::File> IsoVfs::Open(std::string_view input) const {
    const auto path = NormalizePath(input);
    auto handle = std::make_shared<File::Handle>();
    handle->state = state_;
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->iso9660) {
            handle->isoNode = state_->iso9660->Resolve(path);
            if (handle->isoNode.directory) throw Error(ErrorCode::IsDirectory, "Caminho aponta para diretorio: " + path);
            handle->size = handle->isoNode.size;
        } else {
        if (UDFDIR* dir = udfread_opendir(state_->volume, path.c_str())) {
            udfread_closedir(dir);
            throw Error(ErrorCode::IsDirectory, "Caminho aponta para diretorio: " + path);
        }
        UDFFILE* file = udfread_file_open(state_->volume, path.c_str());
        if (!file) throw Error(ErrorCode::NotFound, "Arquivo nao encontrado: " + path);
        const auto size = udfread_file_size(file);
        if (size < 0) {
            udfread_file_close(file);
            throw Error(ErrorCode::Io, "Falha ao consultar arquivo: " + path);
        }
        handle->file = file; handle->size = static_cast<std::uint64_t>(size);
        }
    }
    return std::shared_ptr<File>(new File(std::move(handle)));
}

std::size_t IsoVfs::File::ReadAt(std::uint64_t offset, std::span<std::byte> out) const {
    if (offset >= Size() || out.empty()) return 0;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(out.size(), Size() - offset));
    std::scoped_lock lock(handle_->state->mutex);
    if (handle_->state->iso9660)
        return handle_->state->iso9660->ReadAt(handle_->isoNode, offset, out);
    if (udfread_file_seek(handle_->file, static_cast<std::int64_t>(offset), UDF_SEEK_SET)
        != static_cast<std::int64_t>(offset)) throw Error(ErrorCode::Io, "Falha ao posicionar leitura.");
    std::size_t done = 0;
    while (done < count) {
        const auto request = std::min<std::size_t>(count - done, 1024u * 1024u);
        const auto got = udfread_file_read(handle_->file, out.data() + done, request);
        if (got <= 0) throw Error(ErrorCode::Io, "Erro de leitura ou ISO truncada.");
        done += static_cast<std::size_t>(got);
    }
    return done;
}

FileInfo IsoVfs::Stat(std::string_view input) const {
    const auto path = NormalizePath(input);
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->iso9660) {
            const auto node = state_->iso9660->Resolve(path);
            return {node.directory, node.directory ? 0 : node.size};
        }
        if (UDFDIR* dir = udfread_opendir(state_->volume, path.c_str())) {
            udfread_closedir(dir); return {true, 0};
        }
    }
    return {false, Open(path)->Size()};
}

std::vector<DirectoryEntry> IsoVfs::ListDirectory(std::string_view input) const {
    const auto path = NormalizePath(input);
    std::scoped_lock lock(state_->mutex);
    if (state_->iso9660) {
        const auto node = state_->iso9660->Resolve(path);
        std::vector<DirectoryEntry> result;
        for (const auto& entry : state_->iso9660->Directory(node)) result.push_back({entry.name, entry.directory});
        return result;
    }
    std::unique_ptr<UDFDIR, decltype(&udfread_closedir)> dir(
        udfread_opendir(state_->volume, path.c_str()), udfread_closedir);
    if (!dir) throw Error(ErrorCode::NotDirectory, "Nao foi possivel abrir diretorio: " + path);
    std::vector<DirectoryEntry> entries;
    udfread_dirent entry{};
    while (udfread_readdir(dir.get(), &entry)) {
        if (entries.size() >= 10000) throw Error(ErrorCode::TooLarge, "Diretorio excede limite de entradas.");
        entries.push_back({entry.d_name, entry.d_type == UDF_DT_DIR});
    }
    return entries;
}

std::vector<std::byte> IsoVfs::GetFileBytes(std::string path, std::size_t maxBytes) const {
    auto file = Open(path);
    if (file->Size() > maxBytes || file->Size() > SIZE_MAX)
        throw Error(ErrorCode::TooLarge, "Arquivo grande; utilize Open/ReadAt.");
    std::vector<std::byte> bytes(static_cast<std::size_t>(file->Size()));
    file->ReadAt(0, bytes);
    return bytes;
}

std::string IsoVfs::TitleId() const {
    const auto bytes = GetFileBytes("/PS3_GAME/PARAM.SFO", 1024u * 1024u);
    const auto invalid = [] { throw Error(ErrorCode::InvalidImage, "PARAM.SFO invalido."); };
    if (bytes.size() < 20 || bytes[0] != std::byte{0} || bytes[1] != std::byte{'P'} ||
        bytes[2] != std::byte{'S'} || bytes[3] != std::byte{'F'}) invalid();
    const auto le = [&](std::size_t offset, unsigned length) -> std::uint32_t {
        if (offset > bytes.size() || length > bytes.size() - offset) invalid();
        std::uint32_t value = 0;
        for (unsigned i = 0; i < length; ++i)
            value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
        return value;
    };
    const std::size_t keys = le(8, 4), data = le(12, 4), count = le(16, 4);
    if (count > 4096 || 20 + count * 16 > keys || keys > data || data > bytes.size()) invalid();
    for (std::size_t i = 0; i < count; ++i) {
        const auto record = 20 + i * 16;
        const auto keyOffset = le(record, 2);
        if (keyOffset >= data - keys) invalid();
        const auto key = keys + keyOffset;
        auto end = key;
        while (end < data && bytes[end] != std::byte{}) ++end;
        if (end == data) invalid();
        std::string_view name(reinterpret_cast<const char*>(bytes.data() + key), end - key);
        if (name != "TITLE_ID") continue;
        const auto length = le(record + 4, 4), maximum = le(record + 8, 4), offset = le(record + 12, 4);
        if (le(record + 2, 2) != 0x0204 || length != 10 || length > maximum ||
            offset > bytes.size() - data || length > bytes.size() - data - offset) invalid();
        const auto start = data + offset;
        if (bytes[start + 9] != std::byte{}) invalid();
        std::string title(reinterpret_cast<const char*>(bytes.data() + start), 9);
        for (char c : title) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) invalid();
        return title;
    }
    throw Error(ErrorCode::InvalidImage, "TITLE_ID ausente no PARAM.SFO.");
}
} // namespace d2::vfs
