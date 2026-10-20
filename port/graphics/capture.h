#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int d2_capture_next(uint32_t* index, void* window);
void d2_capture_shutdown(void);
int d2_capture_shader_enabled(void);
void d2_capture_shader(const char* stage, const char* hlsl, size_t bytes);
void d2_capture_shader_program(const char* stage, const char* hlsl, size_t bytes,
    const void* raw, size_t raw_bytes, uint32_t start_slot, uint32_t control, uint32_t cube_mask);
const char* d2_capture_directory(void);
const char* d2_performance_csv_path(void);
FILE* d2_capture_fopen(const char* path, const char* mode);
void d2_capture_mkdir(const char* path);
void d2_capture_finish(const char* directory, uint64_t frame, uint32_t draws);
#ifdef __cplusplus
}
#endif
