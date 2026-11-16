#include "../../graphics/backend_choice.h"
#include "vulkan_renderer.h"
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
  require(failed, "Invalid request was accepted");
}
int main(int argc, char **argv) {
  Renderer renderer;
  try {
    renderer.start(nullptr,
                   argc == 2 && std::string(argv[1]) == "--validation");
    renderer.offscreen(64, 48);
    const Vertex quad[] = {{{-1, -1, .5f, 1}, {1, 0, 0, 1}},
                           {{1, -1, .5f, 1}, {1, 0, 0, 1}},
                           {{1, 1, .5f, 1}, {1, 0, 0, 1}},
                           {{-1, 1, .5f, 1}, {1, 0, 0, 1}}};
    // Vertices are deliberately not a triangle list. Six indices must form
    // two triangles; ignoring indices cannot pass the exact pixel oracle.
    const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    renderer.setGeometry(quad, indices);
    auto verify = [&](Rect expected, std::array<uint8_t, 4> inside,
                      std::array<uint8_t, 4> outside) {
      require(renderer.draw(), "Offscreen draw failed");
      auto pixels = renderer.readPixels();
      require(pixels.size() == 64 * 48 * 4, "Readback size mismatch");
      for (unsigned y = 0; y < 48; y++)
        for (unsigned x = 0; x < 64; x++) {
          bool covered = x >= expected.x && x < expected.x + expected.width &&
                         y >= expected.y && y < expected.y + expected.height;
          auto color = covered ? inside : outside;
          for (unsigned c = 0; c < 4; c++)
            require(pixels[(y * 64 + x) * 4 + c] == color[c],
                    "Pixel color/coverage mismatch");
        }
    };
    DrawParameters p;
    p.clear_color = {0, 0, 1, 1};
    renderer.setParameters(p);
    verify({0, 0, 64, 48}, {255, 0, 0, 255}, {0, 0, 255, 255});
    p.viewport = {8, 4, 40, 32};
    p.scissor = {16, 12, 24, 16};
    renderer.setParameters(p);
    verify(p.scissor, {255, 0, 0, 255}, {0, 0, 255, 255});
    // Non-indexed uploads replace in-flight geometry only after completion.
    const Vertex triangles[] = {quad[0], quad[1], quad[2],
                                quad[0], quad[2], quad[3]};
    renderer.setGeometry(triangles);
    verify(p.scissor, {255, 0, 0, 255}, {0, 0, 255, 255});
    const uint32_t invalid[] = {0, 1, 4};
    rejected([&] { renderer.setGeometry(quad, invalid); });
    auto nan = triangles[0];
    nan.position[0] = std::numeric_limits<float>::quiet_NaN();
    const Vertex invalid_vertices[] = {nan, triangles[1], triangles[2]};
    rejected([&] { renderer.setGeometry(invalid_vertices); });
    // Failed uploads preserve the last valid geometry.
    verify(p.scissor, {255, 0, 0, 255}, {0, 0, 255, 255});
    p.scissor = {60, 0, 8, 8};
    renderer.setParameters(p);
    rejected([&] { renderer.draw(); });
    p.scissor = {};
    renderer.setParameters(p);
    verify(p.viewport, {255, 0, 0, 255}, {0, 0, 255, 255});
    renderer.setGeometry({});
    verify({}, {255, 0, 0, 255}, {0, 0, 255, 255});
    renderer.close();
    require(renderer.info().validation_errors == 0, "Vulkan validation errors");
    std::cout << "Indexed/non-indexed uploads, viewport, scissor, clear and "
                 "rejection recovery passed\n";
    return 0;
  } catch (const Unavailable &e) {
    std::cerr << e.what() << '\n';
    return 77;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
