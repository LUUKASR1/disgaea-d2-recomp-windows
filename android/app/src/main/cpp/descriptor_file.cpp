#include "descriptor_file.h"
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace d2 {
DescriptorFile::~DescriptorFile() { reset(); }
void DescriptorFile::reset() {
    if(fd_ >= 0) close(fd_);
    fd_ = -1; base_ = length_ = 0;
}
bool DescriptorFile::attach(int descriptor, uint64_t base, uint64_t length) {
    // Build the replacement before releasing the old view. Failed attach leaves
    // a valid previous view intact. Holding a dup also prevents caller fd reuse.
    int copy = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    if(copy < 0) return false;
    struct stat st{};
    int flags = fcntl(copy, F_GETFL);
#ifdef O_PATH
    if(flags >= 0 && (flags & O_PATH)) { close(copy); return false; }
#endif
    const uint64_t max = static_cast<uint64_t>(std::numeric_limits<off_t>::max());
    bool ok = flags >= 0 && (flags & O_ACCMODE) != O_WRONLY &&
        fstat(copy, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 &&
        base <= max && length <= max - base &&
        base <= static_cast<uint64_t>(st.st_size) &&
        length <= static_cast<uint64_t>(st.st_size) - base;
    if(!ok) { close(copy); return false; }
    reset(); fd_ = copy; base_ = base; length_ = length; return true;
}
bool DescriptorFile::read(uint64_t offset, void *destination, size_t count) const {
    if(fd_ < 0 || offset > length_ || count > length_ - offset ||
       (count && !destination)) return false;
    auto out = static_cast<unsigned char*>(destination);
    size_t done = 0;
    while(done < count) {
        size_t chunk = count - done;
        if(chunk > 1024*1024) chunk = 1024*1024;
        ssize_t n = pread(fd_, out + done, chunk,
                          static_cast<off_t>(base_ + offset + done));
        if(n < 0 && errno == EINTR) continue;
        if(n <= 0) return false; // EOF before the fixed slice ends is an I/O error.
        done += static_cast<size_t>(n);
    }
    return true;
}
}
