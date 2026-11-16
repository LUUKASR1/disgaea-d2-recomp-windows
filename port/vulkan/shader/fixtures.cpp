#include "fixtures.h"
#include "rsx_fp_decompiler.h"
#include "rsx_vp_decompiler.h"
#include <stdexcept>
#include <vector>
namespace d2::vulkan::shader::fixtures {
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace
void word(std::vector<uint8_t> &bytes, size_t offset, uint32_t value,
          bool fragment) {
  if (fragment) {
    value = (value << 16) | (value >> 16);
    for (unsigned n = 0; n < 4; n++)
      bytes[offset + n] = uint8_t(value >> (24 - 8 * n));
  } else
    for (unsigned n = 0; n < 4; n++)
      bytes[offset + n] = uint8_t(value >> (8 * n));
}
std::string vertex(bool texture, unsigned position_attribute) {
  require(position_attribute < 16, "Invalid synthetic position attribute");
  std::vector<uint8_t> code(48);
  // MOV o0, selected ATTR; MOV o1, ATTR3; MOV o7, ATTR8 (TEXCOORD0).
  const uint32_t source = 2 | (3 << 8) | (2 << 10) | (1 << 12);
  for (unsigned n = 0; n < 3; n++) {
    word(code, n * 16, (1u << 30) | (0x3fu << 15), false);
    word(code, n * 16 + 4,
         ((n == 1 && texture ? 25u : 1u) << 22) |
             ((n == 2 ? 8u
               : n    ? 3u
                      : position_attribute)
              << 8) |
             (source >> 9),
         false);
    word(code, n * 16 + 8, (source & 511u) << 23, false);
    word(code, n * 16 + 12,
         (15u << 13) | ((n == 2 ? 7u : n) << 2) | (n == 2 ? 1u : 0u), false);
  }
  std::vector<char> hlsl(256 * 1024);
  require(rsx_vp_decompile_ex(code.data(), uint32_t(code.size()),
                              texture ? 1u : 0u, hlsl.data(),
                              uint32_t(hlsl.size())) == 3,
          "Synthetic VP decode failed");
  return hlsl.data();
}
std::string fragment(bool texture) {
  std::vector<uint8_t> code(16);
  uint32_t instruction = ((texture ? 0x17u : 1u) << 24) | (15u << 9) |
                         ((texture ? 4u : 1u) << 13) | 1u;
  if (texture)
    instruction |= 3u << 17;
  word(code, 0, instruction, true);
  const uint32_t swizzle = (1u << 11) | (2u << 13) | (3u << 15);
  const uint32_t always = (7u << 18) | (1u << 23) | (2u << 25) | (3u << 27);
  word(code, 4, 1 | swizzle | always, true);
  std::vector<char> hlsl(256 * 1024);
  require(rsx_fp_decompile(code.data(), uint32_t(code.size()), 0x40,
                           hlsl.data(), uint32_t(hlsl.size())) == 1,
          "Synthetic FP decode failed");
  return hlsl.data();
}
std::string bufferedFragment() {
  std::vector<uint8_t> code(32);
  const uint32_t swizzle = (1u << 11) | (2u << 13) | (3u << 15);
  const uint32_t always = (7u << 18) | (1u << 23) | (2u << 25) | (3u << 27);
  word(code, 0, (1u << 24) | (15u << 9) | 1u, true);
  word(code, 4, 2u | swizzle | always, true); // MOV r0, inline CONST
  for (unsigned i = 0; i < 4; i++)
    word(code, 16 + i * 4, 0x3f000000u, true);
  std::vector<char> hlsl(256 * 1024);
  uint32_t constants = 0;
  require(rsx_fp_decompile_buffered_ex(code.data(), uint32_t(code.size()), 0x40,
                                       0, hlsl.data(), uint32_t(hlsl.size()),
                                       &constants) == 1 &&
              constants == 1,
          "Buffered FP decode failed");
  require(rsx_fp_apply_alpha_test_buffered(hlsl.data(), uint32_t(hlsl.size()),
                                           0x204) == 1,
          "Buffered alpha injection failed");
  return hlsl.data();
}
} // namespace d2::vulkan::shader::fixtures
