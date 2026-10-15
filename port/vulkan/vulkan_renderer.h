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
  DepthMode depth = DepthMode::disabled; // Compare LESS_OR_EQUAL when enabled.
  bool blend = true; // Straight alpha; false writes unblended RGBA.
  Rect viewport{}, scissor{};
  float alpha_cutoff = -1.f;
};
struct Info {
  std::string adapter;
  uint32_t api_version = 0, device_type = 0, present_mode = 0,
           validation_errors = 0;
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
  void offscreen(uint32_t width, uint32_t height);
  bool resize(uint32_t width, uint32_t height, bool vsync);
  bool needsResize() const;
  void setGeometry(std::span<const Vertex>,
                   std::span<const uint32_t> indices = {});
  void setParameters(const DrawParameters &);
  // Tightly packed linear RGBA8, nearest sampling, clamp-to-edge, one mip.
  void setTexture(uint32_t width, uint32_t height,
                  std::span<const uint8_t> rgba);
  bool draw();
  bool draw(std::span<const DrawCommand> commands);
  std::vector<uint8_t> readPixels(); // RGBA8; offscreen targets only
  Info info() const;
};
} // namespace d2::vulkan
