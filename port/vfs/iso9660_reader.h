#pragma once
#include "iso_vfs.h"
#include <functional>
#include <list>

namespace d2::vfs::detail {
struct IsoExtent { std::uint64_t offset, length; };
struct IsoNode {
    std::string name;
    bool directory = false;
    std::uint64_t size = 0;
    unsigned version = 0;
    std::vector<IsoExtent> extents;
};
// ECMA-119 primary descriptor profile: 2048-byte blocks, non-interleaved
// records, ASCII identifiers and consecutive multi-extent file records.
// Called under IsoVfs's volume mutex. Does not interpret Joliet/Rock Ridge.
class Iso9660Reader {
public:
    using Read = std::function<bool(std::uint64_t, void*, std::size_t)>;
    Iso9660Reader(Read read, std::uint64_t imageBytes);
    IsoNode Resolve(const std::string& normalizedPath);
    const std::vector<IsoNode>& Directory(const IsoNode& node);
    std::size_t ReadAt(const IsoNode& node, std::uint64_t offset, std::span<std::byte> output);
private:
    struct Record { IsoNode node; std::string identifier; bool more; bool special; };
    Record Decode(std::span<const std::uint8_t> bytes);
    static std::string Fold(std::string_view name);
    struct CacheEntry { std::string key; std::vector<IsoNode> entries; };
    Read read_;
    std::uint64_t bytes_;
    std::uint64_t volumeBytes_ = 0;
    unsigned sequence_ = 1;
    IsoNode root_;
    std::list<CacheEntry> directories_;
};
}
