#include "vulkan_renderer.h"
#include <algorithm>
#include <iostream>
#include <numeric>
using namespace d2::vulkan;
void require(bool value, const char *reason) {
  if (!value)
    throw std::runtime_error(reason);
}
template <class F> void rejected(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::runtime_error &) {
    failed = true;
  }
  require(failed, "Invalid draw accepted");
}
int main(int argc, char **argv) {
  Renderer r;
  try {
    r.start(nullptr, argc == 2 && std::string(argv[1]) == "--validation");
    r.offscreen(64, 48);
    std::vector<Vertex> vertices;
    auto quad = [&](float left, float top, float right, float bottom, float z,
                    std::array<float, 4> color) {
      Vertex q[] = {{{left, top, z, 1}, color, {0, 0}},
                    {{right, top, z, 1}, color, {1, 0}},
                    {{right, bottom, z, 1}, color, {1, 1}},
                    {{left, bottom, z, 1}, color, {0, 1}}};
      for (unsigned i : {0, 1, 2, 0, 2, 3})
        vertices.push_back(q[i]);
    };
    quad(-1, -1, 1, 1, .8f, {1, 0, 0, 1});
    quad(-.5f, -.5f, .5f, .5f, .2f, {0, 1, 0, 1});
    quad(-1, -1, 1, 1, .5f, {0, 0, 1, 1});
    quad(-1, -1, 1, 1, .95f, {0, 0, 1, 1});
    std::vector<uint32_t> indices(vertices.size());
    std::iota(indices.begin(), indices.end(), 0);
    r.setGeometry(vertices, indices);
    DrawCommand far, near, middle, hud;
    far.count = 6;
    far.depth = DepthMode::read_write;
    far.blend = false;
    near = far;
    near.first = 6;
    near.indexed = true;
    middle = far;
    middle.first = 12;
    hud = far;
    hud.first = 18;
    hud.depth = DepthMode::disabled;
    hud.scissor = {0, 0, 16, 12};
    auto verify = [&](std::span<const DrawCommand> commands, auto oracle) {
      require(r.draw(commands), "Draw list failed");
      auto pixels = r.readPixels();
      for (unsigned y = 0; y < 48; y++)
        for (unsigned x = 0; x < 64; x++) {
          auto expected = oracle(x, y);
          for (unsigned c = 0; c < 4; c++)
            require(pixels[(y * 64 + x) * 4 + c] == expected[c],
                    "Multi-draw/depth pixel mismatch");
        }
    };
    const std::array<uint8_t, 4> red = {255, 0, 0, 255},
                                 green = {0, 255, 0, 255},
                                 blue = {0, 0, 255, 255},
                                 black = {0, 0, 0, 255};
    DrawParameters params;
    params.clear_color = {0, 0, 0, 1};
    r.setParameters(params);
    auto center = [](unsigned x, unsigned y) {
      return x >= 16 && x < 48 && y >= 12 && y < 36;
    };
    auto occluded = [&](unsigned x, unsigned y) {
      return center(x, y) ? green : red;
    };
    DrawCommand pair[] = {far, near};
    verify(pair, occluded);
    std::swap(pair[0], pair[1]);
    verify(
        pair,
        occluded); // Reversed submission must produce identical depth result.
    near.depth = DepthMode::read_only;
    DrawCommand readOnly[] = {far, near, middle};
    verify(readOnly, [&](auto, auto) { return blue; });
    near.depth = DepthMode::read_write;
    DrawCommand writes[] = {far, near, middle};
    verify(writes,
           [&](unsigned x, unsigned y) { return center(x, y) ? green : blue; });
    DrawCommand withHud[] = {far, near, hud};
    verify(withHud, [&](unsigned x, unsigned y) {
      return x < 16 && y < 12 ? blue : occluded(x, y);
    });
    params.clear_depth = .1f;
    r.setParameters(params);
    verify(writes, [&](auto, auto) { return black; });
    params.clear_depth = 1;
    r.setParameters(params);
    // Discarded foreground fragments must not occlude later opaque geometry.
    for (size_t i = 6; i < 12; i++)
      vertices[i].color[3] = 0;
    r.setGeometry(vertices, indices);
    near.alpha_cutoff = .5f;
    DrawCommand discard[] = {near, far};
    verify(discard, [&](auto, auto) { return red; });
    DrawCommand bad = far;
    bad.first = UINT32_MAX;
    rejected([&] { r.draw(std::span<const DrawCommand>(&bad, 1)); });
    bad = far;
    bad.depth = static_cast<DepthMode>(99);
    rejected([&] { r.draw(std::span<const DrawCommand>(&bad, 1)); });
    bad = far;
    bad.scissor = {60, 0, 8, 8};
    rejected([&] { r.draw(std::span<const DrawCommand>(&bad, 1)); });
    verify(discard, [&](auto, auto) {
      return red;
    }); // Failed list never partially submits.
    verify({}, [&](auto, auto) { return black; });
    r.close();
    require(!r.info().validation_errors, "Vulkan validation errors");
    std::cout << "Draw lists, order-independent depth, read-only depth, HUD "
                 "overlay, clear and alpha discard passed\n";
    return 0;
  } catch (const Unavailable &e) {
    std::cerr << e.what() << '\n';
    return 77;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
