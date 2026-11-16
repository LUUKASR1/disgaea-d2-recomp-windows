#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace d2::vfs {
enum class ErrorCode { InvalidPath, InvalidImage, NotFound, IsDirectory,
                       NotDirectory, Io, TooLarge };
class Error : public std::runtime_error {
public:
    Error(ErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code(code) {}
    ErrorCode code;
};
struct FileInfo { bool directory; std::uint64_t size; };
struct DirectoryEntry { std::string name; bool directory; };
struct ImageLayout {
    std::uint64_t bytes = 0;
    bool aligned = false;
    bool iso9660Primary = false;
    bool udfRecognition = false;
    bool udfAnchor = false;
    std::vector<std::string> descriptors;
    std::string Description() const;
};
// Read-only metadata inspection, independent of whether the UDF mount succeeds.
ImageLayout InspectImage(const std::filesystem::path& image);

// Read-only UDF mount. Assets are never extracted or written into the ISO.
// All public operations are serialized at the volume; file handles retain
// the volume so destroying IsoVfs cannot invalidate a pending read.
class IsoVfs {
    struct State;
public:
    class File {
        friend class IsoVfs;
        struct Handle;
        explicit File(std::shared_ptr<Handle> handle);
        std::shared_ptr<Handle> handle_;
    public:
        std::uint64_t Size() const noexcept;
        // Reads from an explicit position. No shared seek cursor.
        std::size_t ReadAt(std::uint64_t offset,
                           std::span<std::byte> destination) const;
    };

    explicit IsoVfs(const std::filesystem::path& image);
    std::string BackendName() const;
    std::shared_ptr<File> Open(std::string_view internalPath) const;
    FileInfo Stat(std::string_view internalPath) const;
    std::vector<DirectoryEntry> ListDirectory(std::string_view internalPath) const;
    // Validates SFO table bounds before returning the disc's TITLE_ID.
    std::string TitleId() const;
    std::vector<std::byte> GetFileBytes(
        std::string internalPath,
        std::size_t maxBytes = 64u * 1024u * 1024u) const;
    static std::string NormalizePath(std::string_view path);
private:
    std::shared_ptr<State> state_;
};
} // namespace d2::vfs
