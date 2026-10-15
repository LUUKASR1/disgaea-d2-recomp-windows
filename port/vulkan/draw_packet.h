#pragma once
#include <cstdint>
#include <type_traits>
namespace d2 {
// Little-endian Windows x64 diagnostic packet, version 3 (reader also accepts
// versions 1 and 2). Fixed names only; no pointers, paths, guest addresses or
// platform structs are serialized.
struct DrawPacket {
  uint32_t magic = 0x31524444, version = 3;
  uint32_t width = 0, height = 0, vertices = 0, fragment_bytes = 0;
  uint32_t depth_enabled = 0, depth_write = 0, depth_compare = 4;
  uint32_t blend = 0, src_color = 5, dst_color = 6, color_op = 1;
  uint32_t src_alpha = 2, dst_alpha = 6, alpha_op = 1;
  uint32_t cull = 1, front_ccw = 0, depth_clip = 1, color_mask = 15;
  uint32_t palette_clamp = 0;
  uint32_t texture_width[4]{}, texture_height[4]{};
  uint32_t fragment_output_mask =
      1; // v2: reflected output locations, target 0 bound.
  uint32_t reserved[2]{};
  // v3: 0 = remapped RGBA8, 1/2/3 = native BC1/BC2/BC3 UNORM.
  uint32_t texture_format[4]{};
  // Four packed 3-bit selectors: R,G,B,A,zero,one. RGBA8 is pre-remapped.
  uint32_t texture_mapping[4]{};
};
static_assert(sizeof(DrawPacket) == 160);
static_assert(std::is_trivially_copyable_v<DrawPacket>);
} // namespace d2
