#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Serial diagnostic only: call after the core probe released the guest VM.
 * Creates and removes its own synthetic ELF in a private writable directory.
 * Does not accept a game image or execute PowerPC instructions. */
int d2_elf_probe_run(const char *private_directory, char *report, size_t capacity);
#ifdef __cplusplus
}
#endif
