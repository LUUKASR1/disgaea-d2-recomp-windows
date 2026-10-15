#include "compiler.h"
#include "fixtures.h"
#include "vulkan_renderer.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
using namespace d2::vulkan;
using namespace d2::vulkan::shader;
namespace {
void require(bool v, const char *message) {
  if (!v)
    throw std::runtime_error(message);
}
template <class F> void rejected(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::runtime_error &) {
    failed = true;
  }
  require(failed, "Unsupported request was accepted");
}
using Row = std::array<float, 4>;
using Pixel = std::array<int, 4>;
constexpr unsigned width = 64, height = 48;
std::array<RsxVertex, 4> quad() {
  std::array<RsxVertex, 4> q{};
  const Row pos[] = {
      {-1, 1, .5f, 1}, {1, 1, .5f, 1}, {1, -1, .5f, 1}, {-1, -1, .5f, 1}};
  const Row uv[] = {{0, 0, 0, 1}, {1, 0, 0, 1}, {1, 1, 0, 1}, {0, 1, 0, 1}};
  for (unsigned i = 0; i < 4; i++) {
    q[i].attributes[0] = pos[i];
    q[i].attributes[3] = {1, 0, 0, 1};
    q[i].attributes[8] = uv[i];
  }
  return q;
}
struct Checks {
  uint64_t pixels = 0;
  unsigned cases = 0, multi_cases = 0, mrt_cases = 0, validation_errors = 0;
  std::string adapter;
  template <class Oracle>
  void verify(Renderer &r, Oracle expected,
              std::span<const DrawCommand> commands = {},
              uint32_t attachment = 0) {
    require(commands.empty() ? r.draw() : r.draw(commands),
            "RSX GPU draw failed");
    auto rgba = r.readPixels(attachment);
    require(rgba.size() == width * height * 4, "Readback size mismatch");
    for (unsigned y = 0; y < height; y++)
      for (unsigned x = 0; x < width; x++) {
        auto p = expected(x, y);
        for (unsigned c = 0; c < 4; c++) {
          if (std::abs(int(rgba[(y * width + x) * 4 + c]) - p[c]) > 1) {
            throw std::runtime_error(
                "RSX pixel mismatch x=" + std::to_string(x) +
                " y=" + std::to_string(y) + " channel=" + std::to_string(c) +
                " expected=" + std::to_string(p[c]) +
                " actual=" + std::to_string(rgba[(y * width + x) * 4 + c]));
          }
        }
        pixels++;
      }
    cases++;
  }
  void finish(Renderer &r) {
    r.close();
    auto info = r.info();
    adapter = info.adapter;
    validation_errors += info.validation_errors;
    require(validation_errors == 0, "Vulkan validation errors");
  }
};
} // namespace
int main(int argc, char **argv) {
  std::filesystem::path output = "rsx-gpu-check";
  bool validation = false;
  Checks checks;
  std::string error;
  int code = 0;
  try {
    for (int i = 1; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--output" && i + 1 < argc)
        output = argv[++i];
      else if (arg == "--validation")
        validation = true;
      else
        throw std::runtime_error(
            "Usage: DisgaeaD2RsxGpuProbe [--validation] [--output directory]");
    }
    Compiler compiler;
    auto vs = compiler.compile(fixtures::vertex(), Stage::vertex);
    auto plain = compiler.compile(fixtures::fragment(false), Stage::fragment);
    auto tex = compiler.compile(fixtures::fragment(true), Stage::fragment);
    auto buffered =
        compiler.compile(fixtures::bufferedFragment(), Stage::fragment);
    auto unsupported = compiler.compile(fixtures::vertex(true), Stage::vertex);
    auto alternate =
        compiler.compile(fixtures::vertex(false, 1), Stage::vertex);
    require(std::find(alternate.metadata.inputs.begin(),
                      alternate.metadata.inputs.end(),
                      1) != alternate.metadata.inputs.end() &&
                std::find(alternate.metadata.inputs.begin(),
                          alternate.metadata.inputs.end(),
                          0) == alternate.metadata.inputs.end(),
            "Alternate vertex fixture did not select ATTR1");
    std::array<Row, 514> vc{};
    vc[512] = {1, 1, 1, 1};
    std::array<Row, 2> fc{};
    const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    auto q = quad();
    DrawParameters params;
    params.clear_color = {0, 0, 1, 1};
    {
      Renderer r;
      r.start(nullptr, validation);
      // Unsupported vertex texture contract must fail before allocating GPU
      // resources.
      rejected([&] { r.setRsxProgram(unsupported.words, plain.words); });
      r.setRsxProgram(vs.words, plain.words);
      r.offscreen(width, height);
      r.setParameters(params);
      r.setRsxGeometry(q, indices);
      rejected([&] {
        r.draw();
      }); // constants required, never read uninitialized memory
      r.setRsxConstants(vc, fc);
      checks.verify(r,
                    [](unsigned, unsigned) { return Pixel{255, 0, 0, 255}; });
      vc[512] = {.5f, .5f, 1, 1};
      vc[513] = {.25f, .25f, 0, 0};
      r.setRsxConstants(vc, fc);
      checks.verify(r, [](unsigned x, unsigned y) {
        return x >= 24 && x < 56 && y >= 6 && y < 30 ? Pixel{255, 0, 0, 255}
                                                     : Pixel{0, 0, 255, 255};
      });
      auto bad = vc;
      bad[0][0] = std::numeric_limits<float>::quiet_NaN();
      rejected([&] { r.setRsxConstants(bad, fc); });
      rejected(
          [&] { r.setRsxConstants(std::span<const Row>(vc).first(513), fc); });
      rejected([&] { r.setGeometry({}); });
      rejected([&] { r.setRsxProgram(vs.words, plain.words); });
      std::array<RsxVertex, 6> flat;
      for (unsigned i = 0; i < 6; i++)
        flat[i] = q[indices[i]];
      r.setRsxGeometry(flat); // Same oracle with non-indexed RSX input.
      checks.verify(r, [](unsigned x, unsigned y) {
        return x >= 24 && x < 56 && y >= 6 && y < 30 ? Pixel{255, 0, 0, 255}
                                                     : Pixel{0, 0, 255, 255};
      });
      checks.finish(r);
    }
    vc[512] = {1, 1, 1, 1};
    vc[513] = {0, 0, 0, 0};
    {
      Renderer r;
      r.start(nullptr, validation);
      r.setRsxProgram(vs.words, tex.words);
      r.offscreen(width, height);
      r.setParameters(params);
      r.setRsxGeometry(q, indices);
      r.setRsxConstants(vc, fc);
      const uint8_t black[] = {0, 0, 0, 255};
      r.setTexture(1, 1, black);
      // Unit 3 must still be WHITE, not an alias of legacy unit 0.
      checks.verify(
          r, [](unsigned, unsigned) { return Pixel{255, 255, 255, 255}; });
      const uint8_t image[] = {255, 0, 0,   255, 0,   255, 0, 255,
                               255, 0, 255, 0,   255, 0,   0, 128};
      r.setTextureUnit(3, 2, 2, image);
      checks.verify(r, [](unsigned x, unsigned y) {
        if (y < 24)
          return x < 32 ? Pixel{255, 0, 0, 255} : Pixel{0, 255, 0, 255};
        return x < 32 ? Pixel{0, 0, 255, 255} : Pixel{128, 0, 127, 255};
      });
      rejected([&] { r.setTextureUnit(16, 1, 1, black); });
      rejected([&] { r.setTextureUnit(3, 2, 3, image); });
      for (unsigned i = 0; i < 8; i++) {
        const uint8_t color[] = {uint8_t(i * 31), uint8_t(255 - i * 31), 17,
                                 255};
        r.setTextureUnit(3, 1, 1, color);
        checks.verify(r, [&](unsigned, unsigned) {
          return Pixel{color[0], color[1], 17, 255};
        });
      }
      checks.finish(r);
    }
    {
      Renderer r;
      r.start(nullptr, validation);
      r.setRsxProgram(vs.words, buffered.words);
      r.offscreen(width, height);
      r.setParameters(params);
      r.setRsxGeometry(q, indices);
      // Uniform color, not the inline literal in the synthetic instruction.
      fc[0] = {0, 1, 0, 1};
      fc[1] = {.5f, 0, 0, 0};
      rejected(
          [&] { r.setRsxConstants(vc, std::span<const Row>(fc).first(1)); });
      r.setRsxConstants(vc, fc);
      checks.verify(r,
                    [](unsigned, unsigned) { return Pixel{0, 255, 0, 255}; });
      // Same compiled FP, changed uniform reference => all fragments discarded.
      fc[1][0] = 1;
      r.setRsxConstants(vc, fc);
      checks.verify(r,
                    [](unsigned, unsigned) { return Pixel{0, 0, 255, 255}; });
      // Updates after submission wait the fence, even with no readback in
      // between.
      fc[1][0] = .5f;
      r.setRsxConstants(vc, fc);
      r.draw();
      fc[0] = {1, 0, 0, 1};
      r.setRsxConstants(vc, fc);
      checks.verify(r,
                    [](unsigned, unsigned) { return Pixel{255, 0, 0, 255}; });
      checks.finish(r);
    }
    {
      Renderer r;
      r.start(nullptr, validation);
      r.setRsxProgram(vs.words, plain.words);
      require(r.addRsxProgram(vs.words, plain.words) == 0,
              "Duplicate program not reused");
      auto textured = r.addRsxProgram(vs.words, tex.words);
      auto alpha = r.addRsxProgram(alternate.words, buffered.words);
      require(textured != alpha && textured != 0 && alpha != 0,
              "Program handle collision");
      rejected([&] { r.addRsxProgram(unsupported.words, plain.words); });
      r.offscreen(width, height);
      rejected([&] { r.addRsxProgram(vs.words, plain.words); });
      r.setParameters(params);
      auto multi_geometry = q;
      for (auto &v : multi_geometry) {
        v.attributes[1] = v.attributes[0];
        v.attributes[1][0] *= .5f;
      }
      r.setRsxGeometry(multi_geometry, indices);
      std::array<Row, 514> world{};
      world[512] = {1, 1, 1, 1};
      world[513] = {0, 0, .4f, 0}; // Background at z=.9.
      std::array<Row, 2> color{};
      r.setRsxConstants(world, color);
      auto tile = [&](float x, float y, Row rgba, float reference,
                      bool alternate_position = true) {
        auto v = world;
        v[512] = {alternate_position ? 1.f : .5f, .5f, 1, 1};
        v[513] = {x, y, -.1f, 0}; // Foreground at z=.4.
        color[0] = rgba;
        color[1] = {reference, 0, 0, 0};
        return r.createRsxConstants(v, color);
      };
      auto green = tile(-.5f, .5f, {0, 0, 0, 0}, 0, false);
      auto yellow = tile(.5f, .5f, {1, 1, 0, 1}, .5f);
      auto cyan = tile(-.5f, -.5f, {0, 1, 1, 1}, .5f);
      auto invisible = tile(.5f, -.5f, {1, 1, 1, .25f}, .5f);
      auto translucent = tile(.5f, .5f, {1, 1, 0, .5f}, .25f);
      const uint8_t green_tex[] = {0, 255, 0, 255};
      r.setTextureUnit(3, 1, 1, green_tex);
      std::array<DrawCommand, 5> commands{};
      for (auto &c : commands) {
        c.count = 6;
        c.indexed = true;
        c.depth = DepthMode::read_write;
        c.blend = false;
      }
      commands[1].rsx_program = textured;
      commands[1].rsx_constants = green;
      commands[2].rsx_program = alpha;
      commands[2].rsx_constants = yellow;
      commands[3].rsx_program = alpha;
      commands[3].rsx_constants = cyan;
      commands[4].rsx_program = alpha;
      commands[4].rsx_constants = invisible;
      auto oracle = [](unsigned x, unsigned y) {
        if (y < 24)
          return x < 32 ? Pixel{0, 255, 0, 255} : Pixel{255, 255, 0, 255};
        return x < 32 ? Pixel{0, 255, 255, 255} : Pixel{255, 0, 0, 255};
      };
      auto verify = [&](auto expected) {
        checks.verify(r, expected, commands);
        checks.multi_cases++;
      };
      verify(oracle);
      std::reverse(commands.begin(), commands.end());
      verify(oracle); // Depth must survive switches, even with background
                      // submitted last.
      std::reverse(commands.begin(), commands.end());
      world[512] = {.5f, .5f, 1, 1};
      world[513] = {.5f, -.5f, .4f, 0};
      r.setRsxConstants(
          world, color); // Only default 0 changes; snapshots remain intact.
      verify(oracle);
      world[512] = {1, 1, 1, 1};
      world[513] = {0, 0, .4f, 0};
      r.setRsxConstants(world, color);
      const uint8_t magenta[] = {255, 0, 255, 255};
      r.setTextureUnit(3, 1, 1, magenta);
      auto replaced = [&](unsigned x, unsigned y) {
        return y < 24 && x < 32 ? Pixel{255, 0, 255, 255} : oracle(x, y);
      };
      verify(replaced);
      commands[2].rsx_constants = translucent;
      commands[2].blend = true;
      verify([&](unsigned x, unsigned y) {
        return y < 24 && x >= 32 ? Pixel{255, 128, 0, 255} : replaced(x, y);
      });
      commands[2].blend = false;
      verify([&](unsigned x, unsigned y) {
        return y < 24 && x >= 32 ? Pixel{255, 255, 0, 128} : replaced(x, y);
      });
      commands[2].rsx_constants = yellow;
      auto short_constants =
          r.createRsxConstants(world, std::span<const Row>(color).first(1));
      auto previous = r.readPixels();
      auto bad = commands;
      bad.back().rsx_program = UINT32_MAX;
      rejected([&] { r.draw(bad); });
      bad = commands;
      bad.back().rsx_constants = UINT32_MAX;
      rejected([&] { r.draw(bad); });
      bad = commands;
      bad.back().rsx_constants = short_constants;
      rejected([&] { r.draw(bad); });
      require(r.readPixels() == previous,
              "Rejected draw list changed the target");
      verify(replaced);
      // Fill the bounded pool; rejection must preserve already captured states.
      for (unsigned i = short_constants + 1; i < 64; i++)
        require(r.createRsxConstants(world, color) == i,
                "Snapshot handle not stable");
      rejected([&] { r.createRsxConstants(world, color); });
      verify(replaced);
      checks.finish(r);
    }
    // Original fixtures: no captured game shaders or bytecode are embedded.
    // Exercise all MRT outputs, not just pipeline creation or target 0.
    for (unsigned targets : {2u, 3u, 4u}) {
      std::string source =
          "cbuffer PSConstants:register(b1){float4 colors[4];float4 alpha;}\n"
          "struct Out{";
      for (unsigned i = 0; i < targets; i++)
        source += "float4 c" + std::to_string(i) + ":SV_TARGET" +
                  std::to_string(i) + ";";
      source += "};Out main(){Out o;if(colors[0].a<alpha.x)discard;";
      for (unsigned i = 0; i < targets; i++)
        source +=
            "o.c" + std::to_string(i) + "=colors[" + std::to_string(i) + "];";
      source += "return o;}";
      auto mrt = compiler.compile(source, Stage::fragment);
      Renderer r;
      r.start(nullptr, validation);
      r.setRsxProgram(vs.words, mrt.words);
      auto single = r.addRsxProgram(vs.words, plain.words);
      rejected([&] { r.offscreen(width, height, 0); });
      rejected([&] { r.offscreen(width, height, 5); });
      rejected([&] { r.offscreen(width, height, targets - 1); });
      r.offscreen(width, height, targets); // Rejection must leave it usable.
      rejected([&] { r.resize(width, height, false); });
      r.setParameters(params);
      auto vertices = quad();
      for (auto &v : vertices)
        v.attributes[3] = {1, 1, 1, 1};
      r.setRsxGeometry(vertices, indices);
      std::array<Row, 514> world{};
      world[512] = {1, 1, 1, 1};
      std::array<Row, 5> colors{{{1, 0, 0, 1},
                                 {0, 1, 0, 1},
                                 {1, 1, 0, 1},
                                 {0, 1, 1, 1},
                                 {.25f, 0, 0, 0}}};
      const std::array<Pixel, 4> opaque{{{255, 0, 0, 255},
                                         {0, 255, 0, 255},
                                         {255, 255, 0, 255},
                                         {0, 255, 255, 255}}};
      DrawCommand command{};
      command.count = 6;
      command.indexed = true;
      command.blend = false;
      auto verify = [&](auto expected, std::span<const DrawCommand> commands) {
        for (unsigned a = 0; a < targets; a++) {
          checks.verify(
              r, [&](unsigned, unsigned) { return expected(a); }, commands, a);
          checks.mrt_cases++;
        }
      };
      r.setRsxConstants(world, colors);
      verify([&](unsigned a) { return opaque[a]; }, {&command, 1});
      rejected([&] { r.readPixels(targets); });
      for (unsigned i = 0; i < 4; i++)
        colors[i][3] = .5f;
      command.blend = true;
      r.setRsxConstants(world, colors);
      verify(
          [&](unsigned a) {
            auto p = opaque[a];
            for (unsigned c = 0; c < 3; c++)
              p[c] = (p[c] + (c == 2 ? 255 : 0) + 1) / 2;
            p[3] = 255;
            return p;
          },
          {&command, 1});
      colors[0][3] = 0;
      r.setRsxConstants(world, colors);
      verify([](unsigned) { return Pixel{0, 0, 255, 255}; }, {&command, 1});
      for (unsigned i = 0; i < 4; i++)
        colors[i][3] = 1;
      command.blend = false;
      command.depth = DepthMode::read_write;
      world[513][2] = .25f; // Far plane at z=.75.
      r.setRsxConstants(world, colors);
      auto near_colors = colors;
      for (unsigned i = 0; i < 4; i++)
        near_colors[i] = {1, 0, 1, 1};
      world[513][2] = -.25f; // Near plane at z=.25.
      auto near_state = r.createRsxConstants(world, near_colors);
      std::array<DrawCommand, 2> commands{command, command};
      commands[1].rsx_constants = near_state;
      verify([](unsigned) { return Pixel{255, 0, 255, 255}; }, commands);
      std::reverse(commands.begin(), commands.end());
      verify([](unsigned) { return Pixel{255, 0, 255, 255}; }, commands);
      // A later shader only writes attachment 0: other targets must preserve
      // the first draw's colors, not receive undefined outputs or clear again.
      commands = {command, command};
      for (auto &c : commands)
        c.depth = DepthMode::disabled;
      commands[1].rsx_program = single;
      verify(
          [&](unsigned a) {
            return a == 0 ? Pixel{255, 255, 255, 255} : opaque[a];
          },
          commands);
      checks.finish(r);
    }
    {
      auto too_many = compiler.compile(
          "float4 main():SV_TARGET4{return float4(1,0,0,1);}", Stage::fragment);
      Renderer r;
      r.start(nullptr, validation);
      rejected([&] { r.setRsxProgram(vs.words, too_many.words); });
      checks.finish(r);
    }

  } catch (const Unavailable &e) {
    error = e.what();
    code = 77;
  } catch (const std::exception &e) {
    error = e.what();
    code = 1;
  }
  try {
    std::filesystem::create_directories(output);
    std::ofstream report(output / "rsx-gpu-report.json");
    report << "{\"schema\":3,\"purpose\":\"synthetic-rsx-gpu-execution\","
              "\"status\":\""
           << (code == 0    ? "passed"
               : code == 77 ? "unavailable"
                            : "failed")
           << "\",\"game_backend_ready\":false,\"real_game_shaders_tested\":"
              "false,\"validation_enabled\":"
           << (validation ? "true" : "false")
           << ",\"validation_errors\":" << checks.validation_errors
           << ",\"cases_passed\":" << checks.cases
           << ",\"per_draw_cases_passed\":" << checks.multi_cases
           << ",\"mrt_attachment_cases_passed\":" << checks.mrt_cases
           << ",\"renderer_stage\":\"offscreen-multiple-color-targets\""
           << ",\"pixels_verified\":" << checks.pixels << "}\n";
    std::ofstream(output / "adapter.txt") << checks.adapter << '\n';
    if (!error.empty())
      std::ofstream(output / "rsx-gpu-error.txt") << error << '\n';
    else
      std::filesystem::remove(output / "rsx-gpu-error.txt");
    require(bool(report), "Cannot write RSX GPU report");
  } catch (const std::exception &e) {
    error = e.what();
    code = 1;
  }
  std::cout << (error.empty() ? "Synthetic RSX GPU pixel checks passed" : error)
            << '\n';
  return code;
}
