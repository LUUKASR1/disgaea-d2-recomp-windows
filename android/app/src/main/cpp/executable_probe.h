#pragma once
#include "iso_vfs.h"
#include <string>
// Bounded SELF public headers and plain/zlib ELF structural plan only: no guest mapping, decryption or execution.
std::string d2_inspect_executable(const d2::vfs::IsoVfs::File &file);
