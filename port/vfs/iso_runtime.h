#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum d2_iso_operation {
    D2_ISO_OPEN = 1, D2_ISO_CLOSE, D2_ISO_READ, D2_ISO_WRITE,
    D2_ISO_SEEK, D2_ISO_STAT, D2_ISO_FSTAT, D2_ISO_OPENDIR,
    D2_ISO_READDIR, D2_ISO_CLOSEDIR, D2_ISO_TRUNCATE, D2_ISO_FSYNC,
    D2_ISO_BLOCK_SIZE, D2_ISO_PATH_BLOCK_SIZE, D2_ISO_MKDIR,
    D2_ISO_RMDIR, D2_ISO_UNLINK, D2_ISO_RENAME, D2_ISO_READ_AT,
    D2_ISO_AIO_READ, D2_ISO_AIO_WRITE
};
// UTF-8 image path; host path conversion uses filesystem::u8path.
int d2_iso_mount(const char* image_utf8);
int d2_iso_check_title(const char* expected_title_id);
const char* d2_iso_last_error(void);
void d2_iso_bind_memory(void* base, uint64_t length);
// Return 0 to preserve the existing host backend (updates, DLCs, saves).
// Return 1 when the ISO owns the request, including missing files/errors.
int d2_iso_request(int operation, const uint64_t args[6], int64_t* result);
int d2_iso_is_handle(uint32_t fd);
int d2_iso_read_at_guest(uint32_t fd, uint64_t offset, uint32_t buffer,
                         uint64_t length, uint64_t* count, int64_t* result);
#ifdef __cplusplus
}
#endif
