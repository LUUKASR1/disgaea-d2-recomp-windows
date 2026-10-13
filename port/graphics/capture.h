#pragma once
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
int d2_capture_next(uint32_t* index, void* window);
void d2_capture_shutdown(void);
const char* d2_capture_directory(void);
FILE* d2_capture_fopen(const char* path, const char* mode);
void d2_capture_mkdir(const char* path);
void d2_capture_finish(const char* directory, uint64_t frame, uint32_t draws);
#ifdef __cplusplus
}
#endif
