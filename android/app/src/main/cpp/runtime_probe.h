#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Executes game-independent SDK primitives once; 0 means every check passed.
int d2_runtime_probe_run(char *report, size_t capacity);
#ifdef __cplusplus
}
#endif
