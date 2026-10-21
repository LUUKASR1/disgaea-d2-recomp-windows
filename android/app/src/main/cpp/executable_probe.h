#pragma once
#include "iso_vfs.h"
#include <string>
// Bounded structural plan only: no guest mapping, decryption or execution.
std::string d2_inspect_executable(const d2::vfs::IsoVfs::File &file);
