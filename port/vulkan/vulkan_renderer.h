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
  // Draw-list path: when vs_block/fp_block are not UINT32_MAX, the draw uses
  // blocks returned by frameVsConstants()/frameFpConstants() for the CURRENT
  // frame and the persistent texture objects in `textures` (0 = white) instead
  // of the renderer-global units. rsx_constants must stay 0.
  std::array<uint32_t, 16> textures{};
  uint32_t vs_block = UINT32_MAX, fp_block = UINT32_MAX;
};
struct Info {
  std::string adapter;
  uint32_t api_version = 0, device_type = 0, present_mode = 0,
           validation_errors = 0;
  uint32_t rsx_pipeline_entries = 0;
  uint64_t rsx_pipeline_cache_hits = 0, rsx_pipeline_cache_misses = 0;
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
  // Stencil (ignored when the depth format has no stencil aspect). Faces are
  // packed as compare(3 bits) | fail<<3 | zfail<<6 | zpass<<9 using Vulkan
  // VkCompareOp / VkStencilOp numbering.
  bool stencil_enable = false;
  uint32_t stencil_front = 0, stencil_back = 0;
  uint32_t stencil_read_mask = 255, stencil_write_mask = 255, stencil_ref = 0;
  bool operator==(const RasterState &) const = default;
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
  // Internal resolution: the scene is rendered at window size * scale (0.25..4) and scaled to the window. Takes
  // effect at the next resize(). renderExtent() is the size draw commands (viewports/scissors) must use.
  void setRenderScale(float scale);
  void renderExtent(uint32_t &width, uint32_t &height) const;
  // Optional post-processing (before the first resize): SPIR-V for a fullscreen-triangle vertex shader and a
  // fragment shader reading the scene from t0/s0 and constants from b1 (float4[4]: [0] = scene size, output size,
  // [1..2] = `params`). `active` creates/drops the intermediate scene image on the next resize; the values may
  // change every frame.
  void setPostProgram(std::span<const uint32_t> vertex, std::span<const uint32_t> fragment);
  void setPostParams(const std::array<float, 8> &params, bool active);
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
  // Register immutable shader+raster combinations after target creation.
  // Handles remain valid across frames and swapchain resize until close().
  // Exact matches reuse existing pipelines without a GPU wait or recompilation.
  // Misses compile synchronously on the owning render thread; bounded to 128
  // entries (six depth/blend variants each), with no eviction of in-flight
  // work.
  uint32_t acquireRsxPipeline(std::span<const uint32_t> vertex,
                              std::span<const uint32_t> fragment,
                              const RasterState &state);
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
  // Synchronized diagnostic replacement, sole-program offscreen replay only.
  void replaceReplayPipeline(std::span<const uint32_t> vertex,
                             std::span<const uint32_t> fragment,
                             const RasterState &state);
  // ---- Draw-list path (RSX program installed, offscreen/window target) ----
  // Persistent texture object (1-based handle, 0 is reserved for white).
  // Linear RGBA8 (bc_format 0) or BC1/2/3 blocks (1..3); one mip. Data is
  // copied immediately and uploaded inside the next draw() submission.
  uint32_t createTexture(uint32_t width, uint32_t height, uint32_t bc_format,
                         uint32_t mapping, std::span<const uint8_t> data,
                         bool linear, bool wrap);
  // Resources are released after the in-flight frame completes.
  void destroyTexture(uint32_t handle);
  // Per-frame constant blocks (deduplicated by content). Ids are valid only
  // until the next draw() submission. VS needs exactly 514 rows.
  uint32_t frameVsConstants(std::span<const std::array<float, 4>> rows);
  uint32_t frameFpConstants(std::span<const std::array<float, 4>> rows);
  bool draw();
  bool draw(std::span<const DrawCommand> commands);
  // Offscreen only. Append after a completed draw, retaining color and depth.
  bool drawAppend(std::span<const DrawCommand> commands);
  std::vector<uint8_t>
  readPixels(uint32_t attachment = 0); // RGBA8; offscreen only
  Info info() const;
};
} // namespace d2::vulkan
