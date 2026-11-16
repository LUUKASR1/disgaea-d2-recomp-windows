// Synthetic check for the draw-list path of the Vulkan renderer: per-draw
// texture objects, per-frame constant blocks and frame-to-frame recycling.
// No game files needed. Exit 0 = pass, 1 = fail, 77 = no Vulkan device.
#include "compiler.h"
#include "fixtures.h"
#include "vulkan_renderer.h"
#include <cmath>
#include <iostream>
using namespace d2::vulkan;
using namespace d2::vulkan::shader;
namespace {
using Row = std::array<float, 4>;
using Pixel = std::array<int, 4>;
constexpr unsigned W = 64, H = 48;
void require(bool v, const char *m) {
  if (!v)
    throw std::runtime_error(m);
}
template <class F> void rejected(F f, const char *what) {
  bool failed = false;
  try {
    f();
  } catch (const std::runtime_error &) {
    failed = true;
  }
  require(failed, what);
}
// Two side-by-side quads: left covers x in [-1,0], right x in [0,1].
std::array<RsxVertex, 8> quads() {
  std::array<RsxVertex, 8> q{};
  const Row uv[] = {{0, 0, 0, 1}, {1, 0, 0, 1}, {1, 1, 0, 1}, {0, 1, 0, 1}};
  for (unsigned half = 0; half < 2; half++) {
    float x0 = half ? 0.f : -1.f, x1 = half ? 1.f : 0.f;
    const Row pos[] = {{x0, 1, .5f, 1}, {x1, 1, .5f, 1}, {x1, -1, .5f, 1},
                       {x0, -1, .5f, 1}};
    for (unsigned i = 0; i < 4; i++) {
      auto &v = q[half * 4 + i];
      v.attributes[0] = pos[i];
      v.attributes[3] = {1, 1, 1, 1};
      v.attributes[8] = uv[i];
    }
  }
  return q;
}
std::vector<uint8_t> solid(uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> d;
  for (unsigned i = 0; i < 4; i++)
    d.insert(d.end(), {r, g, b, 255});
  return d;
}
void expect(Renderer &r, Pixel left, Pixel right, const char *what) {
  auto px = r.readPixels();
  require(px.size() == W * H * 4, "Readback size mismatch");
  for (unsigned y = 0; y < H; y++)
    for (unsigned x = 0; x < W; x++) {
      auto &e = x < W / 2 ? left : right;
      for (unsigned c = 0; c < 4; c++)
        if (std::abs(int(px[(y * W + x) * 4 + c]) - e[c]) > 1)
          throw std::runtime_error(
              std::string(what) + ": pixel mismatch x=" + std::to_string(x) +
              " y=" + std::to_string(y) + " c=" + std::to_string(c) +
              " got=" + std::to_string(px[(y * W + x) * 4 + c]) +
              " want=" + std::to_string(e[c]));
    }
}
} // namespace
int main() {
  Renderer r;
  try {
    try {
      r.start(nullptr, false);
    } catch (const Unavailable &e) {
      std::cout << "[frame] unavailable: " << e.what() << "\n";
      return 77;
    }
    Compiler compiler;
    auto vs = compiler.compile(fixtures::vertex(), Stage::vertex);
    auto fs = compiler.compile(fixtures::fragment(true), Stage::fragment);
    r.setRsxProgram(vs.words, fs.words);
    r.offscreen(W, H);
    DrawParameters params;
    params.clear_color = {0, 0, 0, 1};
    r.setParameters(params);
    auto verts = quads();
    const uint32_t idx[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    r.setRsxGeometry(verts, idx);
    std::array<Row, 514> vc{};
    vc[512] = {1, 1, 1, 1};
    std::array<Row, 2> fc{};

    auto draws = [&](uint32_t texL, uint32_t texR, uint32_t vb, uint32_t fb) {
      std::array<DrawCommand, 2> d{};
      for (unsigned i = 0; i < 2; i++) {
        d[i].first = i * 6;
        d[i].count = 6;
        d[i].indexed = true;
        d[i].vs_block = vb;
        d[i].fp_block = fb;
        d[i].textures[3] = i ? texR : texL;
      }
      return d;
    };

    // Frame 1: two different textures on the same frame (the old renderer
    // could only bind one global texture per submission).
    auto red = r.createTexture(2, 2, 0, 0x688, solid(255, 0, 0), false, false);
    auto green = r.createTexture(2, 2, 0, 0x688, solid(0, 255, 0), false, false);
    require(red && green && red != green, "Texture handles invalid");
    auto vb = r.frameVsConstants(vc);
    auto fb = r.frameFpConstants(fc);
    require(r.frameVsConstants(vc) == vb && r.frameFpConstants(fc) == fb,
            "Identical constant blocks were not deduplicated");
    auto d = draws(red, green, vb, fb);
    require(r.draw(d), "Frame 1 draw failed");
    expect(r, {255, 0, 0, 255}, {0, 255, 0, 255}, "frame 1");

    // Blocks are frame-scoped: reusing them after submission must be rejected.
    rejected([&] { r.draw(d); }, "Stale frame blocks accepted");

    // Frame 2: destroy one texture, create another, swap the order. Texture
    // slots and descriptor pools are recycled without leaking handles.
    r.destroyTexture(red);
    auto blue = r.createTexture(2, 2, 0, 0x688, solid(0, 0, 255), true, true);
    vb = r.frameVsConstants(vc);
    fb = r.frameFpConstants(fc);
    d = draws(green, blue, vb, fb);
    require(r.draw(d), "Frame 2 draw failed");
    expect(r, {0, 255, 0, 255}, {0, 0, 255, 255}, "frame 2");

    // Frame 3: unbound unit samples white; unknown handles are rejected.
    vb = r.frameVsConstants(vc);
    fb = r.frameFpConstants(fc);
    d = draws(0, blue, vb, fb);
    require(r.draw(d), "Frame 3 draw failed");
    expect(r, {255, 255, 255, 255}, {0, 0, 255, 255}, "frame 3");
    vb = r.frameVsConstants(vc);
    fb = r.frameFpConstants(fc);
    auto gone = r.createTexture(2, 2, 0, 0x688, solid(9, 9, 9), false, false);
    r.destroyTexture(gone);
    d = draws(gone, blue, vb, fb);
    rejected([&] { r.draw(d); }, "Destroyed texture accepted");
    rejected([&] { r.destroyTexture(gone); }, "Double destroy accepted");
    rejected([&] { r.frameVsConstants(std::span<const Row>(vc).first(3)); },
             "Short VS block accepted");

    // Frame 4: a texture created and destroyed inside the same frame never
    // reaches the GPU, and many draws share one set of blocks.
    auto temp = r.createTexture(2, 2, 0, 0x688, solid(9, 9, 9), false, false);
    r.destroyTexture(temp);
    d = draws(green, green, vb, fb);
    require(r.draw(d), "Frame 4 draw failed");
    expect(r, {0, 255, 0, 255}, {0, 255, 0, 255}, "frame 4");

    r.close();
    auto info = r.info();
    require(info.validation_errors == 0, "Vulkan validation errors");
    std::cout << "[frame] PASS adapter=" << info.adapter << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cout << "[frame] FAIL: " << e.what() << "\n";
    r.close();
    return 1;
  }
}
