#pragma once
#include <cstdint>
#include <string>
// Bounded metadata/sample inspection only. Mounts discs read-only; never executes user code.
std::string d2_inspect_document(int fd, int64_t base, int64_t declared_length);
