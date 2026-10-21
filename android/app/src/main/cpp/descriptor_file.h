#pragma once
#include <cstddef>
#include <cstdint>

namespace d2 {
// Owns a duplicate, never the caller's descriptor. A fixed read-only slice of a
// regular, seekable file; compatible with seekable Android document descriptors.
// No path lookup, ISO parsing, writable saves or streaming-provider support.
class DescriptorFile {
public:
    DescriptorFile() = default;
    ~DescriptorFile();
    DescriptorFile(const DescriptorFile&) = delete;
    DescriptorFile& operator=(const DescriptorFile&) = delete;
    bool attach(int descriptor, uint64_t base, uint64_t size);
    void reset();
    bool read(uint64_t offset, void *destination, size_t count) const;
    uint64_t size() const { return length_; }
    bool valid() const { return fd_ >= 0; }
private:
    int fd_ = -1;
    uint64_t base_ = 0, length_ = 0;
};
}
