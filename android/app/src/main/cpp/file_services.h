#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Serialized mount lifecycle; guest requests are mutex protected. Only one
// read-only, explicitly registered virtual file; not a complete PS3 filesystem.
int d2_files_mount(int descriptor,uint64_t base,uint64_t length,const char *virtual_path);
void d2_files_unmount(void);
int d2_files_open_count(void);
int d2_files_syscall(void *context,size_t bytes);
int d2_file_bundle_run(const char *directory,char *report,size_t capacity);
#ifdef __cplusplus
}
#endif
