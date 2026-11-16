#include "vulkan_renderer.h"
#include <cmath>
#include <iostream>
#include <limits>
using namespace d2::vulkan;
void require(bool value, const char *reason) {
  if (!value)
    throw std::runtime_error(reason);
}
template <class F> void rejected(F operation) {
  bool failed = false;
  try {
    operation();
  } catch (const std::runtime_error &) {
    failed = true;
  }
  require(failed, "Invalid texture/UV request accepted");
}
int main(int argc, char **argv) {
  Renderer r;
  try {
    r.start(nullptr, argc == 2 && std::string(argv[1]) == "--validation");
    r.offscreen(64, 48);
    Vertex quad[] = {{{-1, -1, .5f, 1}, {1, 1, 1, 1}, {0, 0}},
                     {{1, -1, .5f, 1}, {1, 1, 1, 1}, {1, 0}},
                     {{1, 1, .5f, 1}, {1, 1, 1, 1}, {1, 1}},
                     {{-1, 1, .5f, 1}, {1, 1, 1, 1}, {0, 1}}};
    const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    r.setGeometry(quad, indices);
    // Red, green, transparent magenta, half-transparent red: asymmetry
    // detects swapped channels, flipped UV orientation and missing alpha.
    const uint8_t image[] = {255, 0, 0,   255, 0,   255, 0, 255,
                             255, 0, 255, 0,   255, 0,   0, 128};
    r.setTexture(2, 2, image);
    DrawParameters p;
    p.clear_color = {0, 0, 1, 1};
    r.setParameters(p);
    auto verify = [&](bool cutoff, bool flipped = false) {
      require(r.draw(), "Texture draw failed");
      auto pixels = r.readPixels();
      for (unsigned y = 0; y < 48; y++)
        for (unsigned x = 0; x < 64; x++) {
          unsigned texel = (y >= 24 ? 2 : 0) + (x >= 32 ? 1 : 0);
          if (flipped)
            texel ^= 1;
          std::array<int, 4> expected;
          if (texel == 0)
            expected = {255, 0, 0, 255};
          else if (texel == 1)
            expected = {0, 255, 0, 255};
          else if (texel == 2 || cutoff)
            expected = {0, 0, 255, 255};
          else
            expected = {128, 0, 127, 255};
          for (unsigned c = 0; c < 4; c++)
            require(std::abs(int(pixels[(y * 64 + x) * 4 + c]) - expected[c]) <=
                        1,
                    "Texture color/UV/alpha mismatch");
        }
    };
    verify(false);
    p.alpha_cutoff = .75f;
    r.setParameters(p);
    verify(true);
    for (auto &v : quad)
      v.uv[0] = 1 - v.uv[0];
    r.setGeometry(quad, indices);
    verify(true, true);
    // Invalid requests preserve the previous valid texture and geometry.
    rejected([&] { r.setTexture(2, 3, image); });
    rejected([&] { r.setTexture(0, 2, image); });
    rejected([&] { r.setTexture(UINT32_MAX, 2, image); });
    auto invalid = quad[0];
    invalid.uv[0] = std::numeric_limits<float>::quiet_NaN();
    const Vertex bad[] = {invalid, quad[1], quad[2]};
    rejected([&] { r.setGeometry(bad); });
    verify(true, true);
    // An odd row width detects an assumed padded row pitch during upload.
    const uint8_t odd[] = {11,  22,  33,  255, 44,  55,  66,  255,
                           77,  88,  99,  255, 101, 102, 103, 255,
                           121, 122, 123, 255, 141, 142, 143, 255};
    r.setTexture(3, 2, odd);
    r.draw();
    auto odd_pixels = r.readPixels();
    for (unsigned y = 0; y < 48; y++)
      for (unsigned x = 0; x < 64; x++) {
        unsigned tx = ((127 - 2 * x) * 3) / 128, ty = y >= 24 ? 1 : 0;
        for (unsigned c = 0; c < 4; c++)
          require(odd_pixels[(y * 64 + x) * 4 + c] ==
                      odd[(ty * 3 + tx) * 4 + c],
                  "Odd texture row pitch mismatch");
      }
    // Repeated texture replacement exercises fence, descriptor and staging
    // lifetime.
    for (unsigned i = 0; i < 8; i++) {
      const uint8_t color[] = {uint8_t(i * 31), uint8_t(255 - i * 31), 17, 255};
      r.setTexture(1, 1, color);
      r.draw();
      auto pixels = r.readPixels();
      for (size_t j = 0; j < pixels.size(); j++)
        require(pixels[j] == color[j % 4], "Replacement texture mismatch");
    }
    p.alpha_cutoff = std::numeric_limits<float>::infinity();
    rejected([&] { r.setParameters(p); });
    r.close();
    require(r.info().validation_errors == 0, "Vulkan validation errors");
    std::cout << "RGBA upload, UV orientation, alpha blend/discard and texture "
                 "replacement passed\n";
    return 0;
  } catch (const Unavailable &e) {
    std::cerr << e.what() << '\n';
    return 77;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
