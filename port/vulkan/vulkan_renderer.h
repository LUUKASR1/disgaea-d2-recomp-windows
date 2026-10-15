#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2::vulkan {
struct Unavailable : std::runtime_error {
  using std::runtime_error::runtime_error;
};
// Explicit host clip-space input, not a replacement for RSX vertex programs.
struct Vertex {
  std::array<float, 4> position;
  std::array<float, 4> color;
  std::array<float, 2> uv{};
};
// Expanded float attributes for the experimental RSX shader path.
struct RsxVertex {
  std::array<std::array<float, 4>, 16> attributes{};
};
struct Rect {
  uint32_t x = 0, y = 0, width = 0, height = 0;
};
struct DrawParameters {
  std::array<float, 4> clear_color{.03f, .05f, .1f, 1.f};
  // Zero width/height together means full target; nonzero rectangles must fit.
  Rect viewport{}, scissor{};
  // Negative disables discard; otherwise alpha below this value is discarded.
  float alpha_cutoff = -1.f;
  float clear_depth = 1.f;
};
enum class DepthMode : uint32_t { disabled, read_only, read_write };
struct DrawCommand {
  uint32_t first = 0, count = 0;
  bool indexed = false;
  DepthMode depth =
      DepthMode::disabled; // Compare uses RasterState; default LESS_OR_EQUAL.
  bool blend =
      true; // Uses RasterState blend factors; false writes unblended RGBA.
  Rect viewport{}, scissor{};
  float alpha_cutoff = -1.f;
  // Renderer-local immutable program/constant snapshot handles; zero is
  // default.
  uint32_t rsx_program = 0, rsx_constants = 0;
};
struct Info {
  std::string adapter;
  uint32_t api_version = 0, device_type = 0, present_mode = 0,
           validation_errors = 0;
};
// Immutable pipeline state for an isolated RSX draw. Defaults preserve the
// existing synthetic probes; samplers remain independently configurable.
enum class BlendFactor : uint32_t {
  zero,
  one,
  src_color,
  inv_src_color,
  dst_color,
  inv_dst_color,
  src_alpha,
  inv_src_alpha,
  dst_alpha,
  inv_dst_alpha,
  src_alpha_saturate
};
enum class BlendOperation : uint32_t {
  add,
  subtract,
  reverse_subtract,
  minimum,
  maximum
};
struct RasterState {
  uint32_t cull = 0; // 0 none, 1 front, 2 back
  bool front_ccw = true, depth_clamp = false;
  // Explicitly mirror a captured pipeline with fewer bound targets than
  // declared FP outputs; default remains strict for ordinary probe callers.
  bool discard_unbound_outputs = false;
  uint32_t color_mask = 15, depth_compare = 3; // Vulkan ordering: never..always
  BlendFactor src_color = BlendFactor::src_alpha,
              dst_color = BlendFactor::inv_src_alpha;
  BlendFactor src_alpha = BlendFactor::one,
              dst_alpha = BlendFactor::inv_src_alpha;
  BlendOperation color_op = BlendOperation::add, alpha_op = BlendOperation::add;
};
class Renderer {
  class Impl;
  std::unique_ptr<Impl> impl_;

public:
  Renderer();
  ~Renderer();
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  void start(void *native_window, bool validation = false);
  void close() noexcept;
  // 1..4 RGBA8 color attachments, shared depth. MRT requires independentBlend.
  // All declared RSX fragment outputs must have a bound attachment.
  void setRasterState(const RasterState &state); // Before target creation.
  void setSamplerUnit(uint32_t unit, bool linear,
                      bool wrap); // RSX sampler 0..15.
  void offscreen(uint32_t width, uint32_t height, uint32_t color_targets = 1);
  bool resize(uint32_t width, uint32_t height, bool vsync);
  bool needsResize() const;
  void setGeometry(std::span<const Vertex>,
                   std::span<const uint32_t> indices = {});
  // Trusted SPIR-V from the audited compiler only. Install before creating a
  // target. Programs remain immutable until close. VS: set0/b0,
  // FP: set1/b1,t0[16],s0[16].
  // This bounded contract does not support vertex textures, MRT or game replay.
  void setRsxProgram(std::span<const uint32_t> vertex,
                     std::span<const uint32_t> fragment);
  // Register before target creation. Exact duplicate modules reuse a handle.
  // Bounded to 32 distinct programs per renderer.
  uint32_t addRsxProgram(std::span<const uint32_t> vertex,
                         std::span<const uint32_t> fragment);
  // Capture constants in independent GPU buffers, so different draws in the
  // same submission cannot overwrite each other's values. At most 64 snapshots
  // including mutable default 0. Textures remain renderer-global in this stage.
  uint32_t createRsxConstants(std::span<const std::array<float, 4>> vertex,
                              std::span<const std::array<float, 4>> fragment);
  void setRsxGeometry(std::span<const RsxVertex>,
                      std::span<const uint32_t> indices = {});
  // VS requires exactly 514 float4 rows (512 constants + scale + offset).
  // FP is a nonempty packed cbuffer, at most 1024 float4 rows.
  void setRsxConstants(std::span<const std::array<float, 4>> vertex,
                       std::span<const std::array<float, 4>> fragment);
  void setParameters(const DrawParameters &);
  // Tightly packed linear RGBA8, nearest sampling, clamp-to-edge, one mip.
  void setTexture(uint32_t width, uint32_t height,
                  std::span<const uint8_t> rgba);
  // Fragment texture units are independent. Unbound RSX units sample white.
  void setTextureUnit(uint32_t unit, uint32_t width, uint32_t height,
                      std::span<const uint8_t> rgba);
  // Native compressed blocks; selectors 0..5 mean R,G,B,A,zero,one.
  void setTextureUnitBlocks(uint32_t unit, uint32_t width, uint32_t height,
                            uint32_t bc_format, uint32_t mapping,
                            std::span<const uint8_t> blocks);
  bool draw();
  bool draw(std::span<const DrawCommand> commands);
  std::vector<uint8_t>
  readPixels(uint32_t attachment = 0); // RGBA8; offscreen only
  Info info() const;
};
} // namespace d2::vulkan
