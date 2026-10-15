#pragma once
#include <cstdint>
#include <type_traits>
namespace d2 {
// Little-endian Windows x64 diagnostic packet, version 1. Fixed names only;
// no pointers, paths, guest addresses or platform structs are serialized.
struct DrawPacket {
  uint32_t magic = 0x31524444, version = 1;
  uint32_t width = 0, height = 0, vertices = 0, fragment_bytes = 0;
  uint32_t depth_enabled = 0, depth_write = 0, depth_compare = 4;
  uint32_t blend = 0, src_color = 5, dst_color = 6, color_op = 1;
  uint32_t src_alpha = 2, dst_alpha = 6, alpha_op = 1;
  uint32_t cull = 1, front_ccw = 0, depth_clip = 1, color_mask = 15;
  uint32_t palette_clamp = 0;
  uint32_t texture_width[4]{}, texture_height[4]{};
  uint32_t reserved[3]{};
};
static_assert(sizeof(DrawPacket) == 128);
static_assert(std::is_trivially_copyable_v<DrawPacket>);
} // namespace d2
