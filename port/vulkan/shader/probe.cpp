#include "compiler.h"
#include "rsx_fp_decompiler.h"
#include "rsx_vp_decompiler.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace d2::vulkan::shader;
void require(bool value, const char *reason) {
  if (!value)
    throw std::runtime_error(reason);
}
void word(std::vector<uint8_t> &bytes, size_t offset, uint32_t value,
          bool fragment) {
  if (fragment) {
    value = (value << 16) | (value >> 16);
    for (unsigned n = 0; n < 4; n++)
      bytes[offset + n] = uint8_t(value >> (24 - 8 * n));
  } else
    for (unsigned n = 0; n < 4; n++)
      bytes[offset + n] = uint8_t(value >> (8 * n));
}
std::string vertex(bool texture = false) {
  std::vector<uint8_t> code(32);
  // MOV o0, ATTR0; MOV o1, ATTR3; second instruction ends the program.
  const uint32_t source = 2 | (3 << 8) | (2 << 10) | (1 << 12);
  for (unsigned n = 0; n < 2; n++) {
    word(code, n * 16, (1u << 30) | (0x3fu << 15), false);
    word(code, n * 16 + 4,
         ((n && texture ? 25u : 1u) << 22) | ((n ? 3u : 0u) << 8) |
             (source >> 9),
         false);
    word(code, n * 16 + 8, (source & 511u) << 23, false);
    word(code, n * 16 + 12, (15u << 13) | (n << 2) | (n ? 1u : 0u), false);
  }
  std::vector<char> hlsl(256 * 1024);
  require(rsx_vp_decompile_ex(code.data(), uint32_t(code.size()),
                              texture ? 1u : 0u, hlsl.data(),
                              uint32_t(hlsl.size())) == 2,
          "Synthetic VP decode failed");
  return hlsl.data();
}
std::string fragment(bool texture) {
  std::vector<uint8_t> code(16);
  uint32_t instruction = ((texture ? 0x17u : 1u) << 24) | (15u << 9) |
                         ((texture ? 4u : 1u) << 13) | 1u;
  if (texture)
    instruction |= 3u << 17;
  word(code, 0, instruction, true);
  const uint32_t swizzle = (1u << 11) | (2u << 13) | (3u << 15);
  const uint32_t always = (7u << 18) | (1u << 23) | (2u << 25) | (3u << 27);
  word(code, 4, 1 | swizzle | always, true);
  std::vector<char> hlsl(256 * 1024);
  require(rsx_fp_decompile(code.data(), uint32_t(code.size()), 0x40,
                           hlsl.data(), uint32_t(hlsl.size())) == 1,
          "Synthetic FP decode failed");
  return hlsl.data();
}
std::string bufferedFragment() {
  std::vector<uint8_t> code(32);
  const uint32_t swizzle = (1u << 11) | (2u << 13) | (3u << 15);
  const uint32_t always = (7u << 18) | (1u << 23) | (2u << 25) | (3u << 27);
  word(code, 0, (1u << 24) | (15u << 9) | 1u, true);
  word(code, 4, 2u | swizzle | always, true); // MOV r0, inline CONST
  for (unsigned i = 0; i < 4; i++)
    word(code, 16 + i * 4, 0x3f000000u, true);
  std::vector<char> hlsl(256 * 1024);
  uint32_t constants = 0;
  require(rsx_fp_decompile_buffered_ex(code.data(), uint32_t(code.size()), 0x40,
                                       0, hlsl.data(), uint32_t(hlsl.size()),
                                       &constants) == 1 &&
              constants == 1,
          "Buffered FP decode failed");
  require(rsx_fp_apply_alpha_test_buffered(hlsl.data(), uint32_t(hlsl.size()),
                                           0x204) == 1,
          "Buffered alpha injection failed");
  return hlsl.data();
}
bool binding(const Module &m, unsigned set, unsigned number) {
  return std::any_of(
      m.metadata.resources.begin(), m.metadata.resources.end(),
      [&](auto r) { return r.set == set && r.binding == number; });
}
bool has(const std::vector<uint32_t> &list, unsigned n) {
  return std::find(list.begin(), list.end(), n) != list.end();
}
int main(int argc, char **argv) {
  std::filesystem::path output = "shader-check";
  unsigned count = 0;
  std::string error;
  try {
    if (argc == 3 && std::string(argv[1]) == "--output")
      output = argv[2];
    else if (argc != 1)
      throw std::runtime_error(
          "Usage: DisgaeaD2ShaderProbe [--output directory]");
    std::filesystem::create_directories(output);
    std::filesystem::remove(output / "shader-error.txt");
    Compiler compiler;
    auto write = [&](const char *name, const std::string &hlsl, Stage stage) {
      auto m = compiler.compile(hlsl, stage);
      std::ofstream file(output / (std::string(name) + ".spv"),
                         std::ios::binary);
      file.write(reinterpret_cast<const char *>(m.words.data()),
                 std::streamsize(m.words.size() * 4));
      require(bool(file), "SPIR-V report write failed");
      count++;
      return m;
    };
    auto vp = write("synthetic-vp", vertex(), Stage::vertex);
    require(has(vp.metadata.inputs, 0) && has(vp.metadata.inputs, 3),
            "VP attribute locations are not preserved");
    require(has(vp.metadata.outputs, 0) && binding(vp, 0, 0),
            "VP COLOR0/uniform mapping failed");
    auto vertexTexture =
        write("synthetic-texture-vp", vertex(true), Stage::vertex);
    require(binding(vertexTexture, 0, 48) && binding(vertexTexture, 0, 64),
            "VP texture namespace failed");
    auto buffered =
        write("synthetic-buffered-fp", bufferedFragment(), Stage::fragment);
    require(binding(buffered, 1, 1),
            "Buffered fragment constants mapping failed");
    auto fp = write("synthetic-fp", fragment(false), Stage::fragment);
    require(has(fp.metadata.inputs, 0), "FP COLOR0 location mismatch");
    auto tex = write("synthetic-texture-fp", fragment(true), Stage::fragment);
    require(binding(tex, 1, 32) && binding(tex, 1, 64),
            "FP texture/sampler binding mapping failed");
    require(has(tex.metadata.inputs, 3), "FP TEXCOORD0 location mismatch");
    const std::string collision =
        R"(cbuffer Values:register(b0){float4 factor;} Texture2D image:register(t0); SamplerState sampler0:register(s0);
        struct PSInput{float4 uv:TEXCOORD0;};
        float4 main(PSInput input):SV_TARGET{return factor*image.Sample(sampler0,input.uv.xy);})";
    auto resources = write("resource-namespaces", collision, Stage::fragment);
    require(binding(resources, 1, 0) && binding(resources, 1, 32) &&
                binding(resources, 1, 64),
            "b0/t0/s0 aliasing");
    bool rejected = false;
    try {
      compiler.compile("invalid HLSL", Stage::fragment);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected, "Invalid source accepted");
    write("recovery-fp", fragment(false), Stage::fragment);
    std::cout << "RSX synthetic VP/FP, fixed locations, distinct resource sets "
                 "and namespaces passed\n";
  } catch (const std::exception &e) {
    error = e.what();
    std::cerr << error << '\n';
  }
  std::filesystem::create_directories(output);
  std::ofstream report(output / "shader-report.json");
  // Error text is written separately to preserve valid JSON without embedding
  // diagnostics.
  report << "{\"schema\":1,\"purpose\":\"synthetic-rsx-shader-compilation\","
            "\"game_backend_ready\":false,\"real_game_shaders_tested\":false,"
            "\"status\":\""
         << (error.empty() ? "passed" : "failed")
         << "\",\"compiled_modules\":" << count << "}\n";
  if (!error.empty())
    std::ofstream(output / "shader-error.txt") << error;
  return error.empty() && report ? 0 : 1;
}
