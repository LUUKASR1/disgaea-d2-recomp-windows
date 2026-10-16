#include "compiler.h"
#include "draw_packet.h"
#include "fixtures.h"
#include "rsx_contract.h"
#include "vulkan_renderer.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
using namespace d2::vulkan;
using namespace d2::vulkan::shader;
namespace fs = std::filesystem;
namespace {
std::string jsonString(const std::string &value) {
  std::string result = "\"";
  const char hex[] = "0123456789abcdef";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') {
      result += '\\';
      result += char(c);
    } else if (c < 32) {
      result += "\\u00";
      result += hex[c >> 4];
      result += hex[c & 15];
    } else
      result += char(c);
  }
  return result + "\"";
}
void require(bool b, const char *message) {
  if (!b)
    throw std::runtime_error(message);
}
std::vector<uint8_t> read(const fs::path &file, size_t limit,
                          size_t exact = 0) {
  require(!fs::is_symlink(fs::symlink_status(file)) &&
              fs::is_regular_file(file),
          "Missing/nonregular draw file");
  auto n = fs::file_size(file);
  require(n > 0 && n <= limit && (!exact || n == exact),
          "Invalid draw file size");
  std::vector<uint8_t> data(size_t(n), 0);
  std::ifstream stream(file, std::ios::binary);
  stream.read((char *)data.data(), std::streamsize(n));
  require(bool(stream) && stream.peek() == std::char_traits<char>::eof(),
          "Draw file changed during read");
  return data;
}
void write(const fs::path &p, const void *data, size_t n) {
  std::ofstream s(p, std::ios::binary);
  s.write((const char *)data, std::streamsize(n));
  require(bool(s), "Cannot write draw output");
}
std::string text(const fs::path &p) {
  auto b = read(p, 262144);
  return {(char *)b.data(), b.size()};
}
void bmp(const fs::path &path, const std::vector<uint8_t> &rgba, unsigned w,
         unsigned h) {
  std::vector<uint8_t> b(54 + size_t(w) * h * 4);
  auto word = [&](unsigned off, uint32_t n) {
    for (unsigned i = 0; i < 4; i++)
      b[off + i] = uint8_t(n >> (8 * i));
  };
  b[0] = 'B';
  b[1] = 'M';
  word(2, uint32_t(b.size()));
  word(10, 54);
  word(14, 40);
  word(18, w);
  word(22, uint32_t(-int32_t(h)));
  b[26] = 1;
  b[28] = 32;
  for (size_t i = 0; i < size_t(w) * h; i++) {
    b[54 + i * 4] = rgba[i * 4 + 2];
    b[55 + i * 4] = rgba[i * 4 + 1];
    b[56 + i * 4] = rgba[i * 4];
    b[57 + i * 4] = rgba[i * 4 + 3];
  }
  write(path, b.data(), b.size());
}
BlendFactor factor(uint32_t n) {
  switch (n) {
  case 1:
    return BlendFactor::zero;
  case 2:
    return BlendFactor::one;
  case 3:
    return BlendFactor::src_color;
  case 4:
    return BlendFactor::inv_src_color;
  case 5:
    return BlendFactor::src_alpha;
  case 6:
    return BlendFactor::inv_src_alpha;
  case 7:
    return BlendFactor::dst_alpha;
  case 8:
    return BlendFactor::inv_dst_alpha;
  case 9:
    return BlendFactor::dst_color;
  case 10:
    return BlendFactor::inv_dst_color;
  case 11:
    return BlendFactor::src_alpha_saturate;
  default:
    throw std::runtime_error("Unsupported D3D blend factor");
  }
}
BlendOperation operation(uint32_t n) {
  require(n >= 1 && n <= 5, "Unsupported D3D blend operation");
  return BlendOperation(n - 1);
}
void fixture(const fs::path &input) {
  fs::create_directories(input);
  d2::DrawPacket p;
  p.width = 64;
  p.height = 48;
  p.vertices = 6;
  p.fragment_bytes = 16;
  p.depth_clip = 1;
  auto vs = fixtures::vertex();
  std::string fp =
      "Texture2D rsx_tex[16]:register(t0);SamplerState "
      "rsx_samp[16]:register(s0);cbuffer Color:register(b1){float4 "
      "color;}struct In{float4 uv:TEXCOORD0;};float4 main(In "
      "i):SV_TARGET{return rsx_tex[0].Sample(rsx_samp[0],i.uv.xy)*color;}";
  write(input / "vp.hlsl", vs.data(), vs.size());
  write(input / "fp.hlsl", fp.data(), fp.size());
  std::string marker = "{\"status\":\"captured\"}";
  write(input / "draw.json", marker.data(), marker.size());
  std::array<RsxVertex, 6> vertices{};
  float pos[6][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};
  for (unsigned i = 0; i < 6; i++) {
    vertices[i].attributes[0] = {pos[i][0], pos[i][1], .5f, 1};
    vertices[i].attributes[3] = {1, 1, 1, 1};
    vertices[i].attributes[8] = {(pos[i][0] + 1) / 2, (1 - pos[i][1]) / 2, 0,
                                 1};
  }
  std::array<std::array<float, 4>, 514> vc{};
  vc[512] = {1, 1, 1, 1};
  std::array<float, 4> fc{1, 1, 1, 1};
  write(input / "vertices.bin", vertices.data(), sizeof(vertices));
  write(input / "vertex-constants.bin", vc.data(), sizeof(vc));
  write(input / "fragment-constants.bin", fc.data(), sizeof(fc));
  uint8_t red[4] = {255, 0, 0, 255}, black[4]{};
  for (unsigned u = 0; u < 4; u++) {
    p.texture_width[u] = p.texture_height[u] = 1;
    write(input / ("tex" + std::to_string(u) + ".rgba"), u ? black : red, 4);
  }
  std::vector<uint8_t> image(size_t(p.width) * p.height * 4);
  for (size_t i = 0; i < image.size(); i += 4) {
    image[i] = image[i + 3] = 255;
  }
  write(input / "reference.rgba", image.data(), image.size());
  write(input / "packet.bin", &p, sizeof(p));
}
} // namespace
int main(int argc, char **argv) {
  fs::path input, output = "draw-vulkan-check";
  bool synthetic = false, validation = false, sequence_mode = false;
  uint32_t sequence_count = 0, sequence_version = 0, pipeline_switches = 0;
  int code = 1;
  std::string status = "failed", error, adapter;
  uint64_t different = 0, above = 0, nonzero = 0;
  uint32_t unused_outputs = 0;
  unsigned maximum = 0, validation_errors = 0;
  uint32_t w = 0, h = 0;
  try {
    for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      if (a == "--input" && i + 1 < argc)
        input = argv[++i];
      else if (a == "--output" && i + 1 < argc)
        output = argv[++i];
      else if (a == "--sequence")
        sequence_mode = true;
      else if (a == "--self-test")
        synthetic = true;
      else if (a == "--validation")
        validation = true;
      else
        throw std::runtime_error(
            "Usage: DisgaeaD2DrawReplay --input draw_N "
            "[--sequence] [--output folder] [--validation]");
    }
    require(std::endian::native == std::endian::little,
            "Packet requires little endian host");
    fs::create_directories(output);
    // Remove stale proof before any input parsing or GPU work.
    for (auto name : {"output.bmp", "reference.bmp", "difference.bmp"})
      fs::remove(output / name);
    if (synthetic) {
      require(input.empty(), "Do not mix self-test and input");
      input = output / "synthetic-input";
      fixture(input);
    }
    require(!input.empty() && !fs::is_symlink(fs::symlink_status(input)),
            "Select a regular draw directory");
    require(!(synthetic && sequence_mode), "Sequence needs captured inputs");
    fs::path root = input;
    d2::DrawSequence sequence;
    if (sequence_mode) {
      require(text(root / "sequence.json").find("\"status\":\"captured\"") !=
                  std::string::npos,
              "Sequence was not completed");
      auto data = read(root / "sequence.bin", sizeof(sequence));
      require(data.size() == 48 || data.size() == 64, "Invalid sequence size");
      std::memcpy(&sequence, data.data(), data.size());
      require(data.size() == (sequence.version == 1 ? 48u : 64u),
              "Sequence version/size mismatch");
      require(sequence.magic == 0x31535144 &&
                  (sequence.version == 1 || sequence.version == 2) &&
                  sequence.reserved == 0 && sequence.count >= 2 &&
                  sequence.count <= (sequence.version == 1 ? 4u : 8u) &&
                  sequence.width && sequence.width <= 1920 && sequence.height &&
                  sequence.height <= 1080,
              "Invalid sequence header");
      for (unsigned i = 0; i < 8; i++)
        require(i < sequence.count
                    ? (sequence.draws[i] < 16384 &&
                       (!i || sequence.draws[i] > sequence.draws[i - 1]))
                    : sequence.draws[i] == 0,
                "Invalid sequence order/reserved slot");
      sequence_count = sequence.count;
      sequence_version = sequence.version;
    }
    Renderer r;
    Compiler compiler;
    std::vector<uint8_t> reference;
    std::vector<uint32_t> first_vs, first_fp;
    d2::DrawPacket first_packet;
    for (unsigned index = 0; index < (sequence_mode ? sequence.count : 1u);
         index++) {
      if (sequence_mode) {
        char name[32];
        std::snprintf(name, sizeof(name), "draw_%03u", sequence.draws[index]);
        input = root / name;
        require(!fs::is_symlink(fs::symlink_status(input)),
                "Draw directory symlink");
        auto marker = text(input / "draw.json");
        auto number = [&](const char *key) {
          std::smatch m;
          require(std::regex_search(marker, m,
                                    std::regex(std::string("\"") + key +
                                               "\"\\s*:\\s*([0-9]+)")),
                  "Draw identity missing");
          return std::stoull(m[1].str());
        };
        require(number("frame") == sequence.frame &&
                    number("draw") == sequence.draws[index],
                "Sequence draw identity/frame mismatch");
      }
      require(text(input / "draw.json").find("\"status\":\"captured\"") !=
                  std::string::npos,
              "Draw was rejected or not completed");
      auto bytes = read(input / "packet.bin", 160);
      require(bytes.size() == 128 || bytes.size() == 160,
              "Invalid packet size");
      d2::DrawPacket p;
      std::memcpy(&p, bytes.data(), bytes.size());
      require(bytes.size() == (p.version == 3 ? 160u : 128u),
              "Packet version/size mismatch");
      require(p.magic == 0x31524444 &&
                  (p.version == 1 || p.version == 2 || p.version == 3) &&
                  p.width && p.height && p.width <= 1920 && p.height <= 1080 &&
                  p.vertices && p.vertices <= 4096 && p.vertices % 3 == 0 &&
                  p.fragment_bytes && p.fragment_bytes <= 16384 &&
                  p.fragment_bytes % 16 == 0,
              "Invalid draw packet header");
      require(p.depth_enabled <= 1 && p.depth_write <= 1 &&
                  p.depth_compare >= 1 && p.depth_compare <= 8 &&
                  p.blend <= 1 && p.cull >= 1 && p.cull <= 3 &&
                  p.front_ccw <= 1 && p.depth_clip <= 1 && p.color_mask <= 15 &&
                  p.palette_clamp <= 1,
              "Unsupported packet raster fields");
      if (p.version == 1) {
        require(p.fragment_output_mask == 0, "Unknown legacy packet field");
        p.fragment_output_mask = 1;
      }
      require((p.fragment_output_mask & 1u) && p.fragment_output_mask <= 15,
              "Invalid reflected fragment output mask");
      require(std::all_of(std::begin(p.reserved), std::end(p.reserved),
                          [](auto v) { return v == 0; }),
              "Unknown packet extension");
      w = p.width;
      h = p.height;
      auto vertex = read(input / "vertices.bin", 4096 * 256,
                         size_t(p.vertices) * 256),
           vc = read(input / "vertex-constants.bin", 8224, 8224),
           fc = read(input / "fragment-constants.bin", 16384, p.fragment_bytes);
      std::vector<RsxVertex> vertices(p.vertices);
      std::memcpy(vertices.data(), vertex.data(), vertex.size());
      std::array<std::array<float, 4>, 514> constants{};
      std::memcpy(constants.data(), vc.data(), vc.size());
      std::vector<std::array<float, 4>> fragment(p.fragment_bytes / 16);
      std::memcpy(fragment.data(), fc.data(), fc.size());
      if (!sequence_mode)
        reference =
            read(input / "reference.rgba", 1920 * 1080 * 4, size_t(w) * h * 4);
      auto vs = compiler.compile(text(input / "vp.hlsl"), Stage::vertex),
           fp = compiler.compile(text(input / "fp.hlsl"), Stage::fragment);
      auto contract = d2::vulkan::validateRsxModule(fp.words, true);
      uint32_t actual_mask = 0;
      for (auto location : contract.outputs)
        actual_mask |= 1u << location;
      require(actual_mask == p.fragment_output_mask,
              "Reflected shader outputs differ from capture");
      unused_outputs = actual_mask & ~1u;
      RasterState state;
      state.discard_unbound_outputs = (actual_mask & ~1u) != 0;
      state.cull = p.cull - 1;
      state.front_ccw = p.front_ccw != 0;
      state.depth_clamp = !p.depth_clip;
      state.color_mask = p.color_mask;
      state.depth_compare = p.depth_compare - 1;
      // Disabled blending ignores unused/zero D3D blend enums.
      if (p.blend) {
        state.src_color = factor(p.src_color);
        state.dst_color = factor(p.dst_color);
        state.src_alpha = factor(p.src_alpha);
        state.dst_alpha = factor(p.dst_alpha);
        state.color_op = operation(p.color_op);
        state.alpha_op = operation(p.alpha_op);
      }
      uint32_t pipeline = 0;
      if (index == 0) {
        first_packet = p;
        first_vs = vs.words;
        first_fp = fp.words;
        r.start(nullptr, validation);
        adapter = r.info().adapter;
        r.setRsxProgram(vs.words, fp.words);
        r.setRasterState(state);
        r.offscreen(w, h);
      } else {
        require(w == first_packet.width && h == first_packet.height,
                "Sequence target mismatch");
        bool changed =
            std::memcmp(&p.depth_enabled, &first_packet.depth_enabled,
                        15 * sizeof(uint32_t)) != 0 ||
            p.fragment_output_mask != first_packet.fragment_output_mask ||
            vs.words != first_vs || fp.words != first_fp;
        if (changed) {
          require(sequence.version >= 2,
                  "Legacy sequence requires identical pipeline/shaders");
          // Preserve prior pipeline handles for later frames/draws.
          // This replay still synchronizes uploads; it is not the game backend.
          pipeline_switches++;
          first_packet = p;
          first_vs = vs.words;
          first_fp = fp.words;
        }
      }
      pipeline = r.acquireRsxPipeline(vs.words, fp.words, state);
      if (sequence_mode)
        require(w == sequence.width && h == sequence.height,
                "Sequence target mismatch");
      for (unsigned u = 0; u < 16; u++) {
        uint8_t black[4]{};
        r.setTextureUnit(u, 1, 1, black);
      }
      for (unsigned u = 0; u < 4; u++) {
        require(p.texture_width[u] && p.texture_height[u] &&
                    p.texture_width[u] <= 4096 && p.texture_height[u] <= 4096 &&
                    uint64_t(p.texture_width[u]) * p.texture_height[u] <=
                        4194304,
                "Invalid texture dimensions");
        require(p.texture_format[u] <= 3, "Unsupported texture format");
        if (p.texture_format[u]) {
          size_t size = size_t((p.texture_width[u] + 3) / 4) *
                        ((p.texture_height[u] + 3) / 4) *
                        (p.texture_format[u] == 1 ? 8 : 16);
          auto blocks =
              read(input / ("tex" + std::to_string(u) + ".bc"), 4194304, size);
          r.setTextureUnitBlocks(u, p.texture_width[u], p.texture_height[u],
                                 p.texture_format[u], p.texture_mapping[u],
                                 blocks);
        } else {
          require(p.texture_mapping[u] == 0, "RGBA texture already remapped");
          auto pixels =
              read(input / ("tex" + std::to_string(u) + ".rgba"), 16777216,
                   size_t(p.texture_width[u]) * p.texture_height[u] * 4);
          r.setTextureUnit(u, p.texture_width[u], p.texture_height[u], pixels);
        }
        r.setSamplerUnit(u, u != 0, u != 0 && !(u == 1 && p.palette_clamp));
      }
      DrawParameters parameters;
      parameters.clear_color = {0, 0, 0, 0};
      r.setParameters(parameters);
      r.setRsxGeometry(vertices);
      r.setRsxConstants(constants, fragment);
      DrawCommand draw;
      draw.rsx_program = pipeline;
      draw.count = p.vertices;
      draw.blend = p.blend != 0;
      draw.depth = !p.depth_enabled ? DepthMode::disabled
                   : p.depth_write  ? DepthMode::read_write
                                    : DepthMode::read_only;
      auto commands = std::span<const DrawCommand>(&draw, 1);
      require(index ? r.drawAppend(commands) : r.draw(commands),
              "Draw was not submitted");
    }
    if (sequence_mode)
      reference = read(root / "sequence-reference.rgba", 1920 * 1080 * 4,
                       size_t(w) * h * 4);
    auto rendered = r.readPixels();
    r.close();
    validation_errors = r.info().validation_errors;
    require(!validation_errors, "Vulkan validation errors");
    std::vector<uint8_t> difference(rendered.size());
    for (size_t i = 0; i < rendered.size(); i += 4) {
      unsigned delta = 0;
      bool active = false;
      for (unsigned c = 0; c < 4; c++) {
        unsigned d =
            unsigned(std::abs(int(reference[i + c]) - int(rendered[i + c])));
        delta = std::max(delta, d);
        maximum = std::max(maximum, d);
        difference[i + c] = uint8_t(d);
        active |= reference[i + c] != 0;
      }
      different += delta != 0;
      above += delta > 1;
      nonzero += active;
      difference[i + 3] = 255;
    }
    bmp(output / "output.bmp", rendered, w, h);
    bmp(output / "reference.bmp", reference, w, h);
    bmp(output / "difference.bmp", difference, w, h);
    status = !nonzero    ? "empty-reference"
             : above     ? "pixel-mismatch"
             : different ? "within-tolerance"
                         : "exact-match";
    code = nonzero && !above ? 0 : 2;
  } catch (const Unavailable &e) {
    status = "unavailable";
    error = e.what();
    code = 77;
  } catch (const std::exception &e) {
    error = e.what();
  }
  try {
    fs::create_directories(output);
    std::string json =
        "{\"schema\":1,\"purpose\":\"isolated-draw-d3d12-vulkan-comparison\","
        "\"status\":\"" +
        status +
        "\",\"input_is_synthetic_fixture\":" + (synthetic ? "true" : "false") +
        ",\"game_backend_ready\":false,\"full_frame_equivalence_tested\":false,"
        "\"validation_enabled\":" +
        (validation ? "true" : "false") +
        ",\"validation_errors\":" + std::to_string(validation_errors) +
        ",\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) +
        ",\"bound_color_targets\":1,\"discarded_shader_output_mask\":" +
        std::to_string(unused_outputs) +
        ",\"different_pixels\":" + std::to_string(different) +
        ",\"pixels_above_tolerance\":" + std::to_string(above) +
        ",\"maximum_channel_error\":" + std::to_string(maximum) +
        ",\"nonzero_reference_pixels\":" + std::to_string(nonzero) +
        ",\"tolerance\":1}\n";
    json.insert(json.size() - 2, ",\"adapter\":" + jsonString(adapter));
    if (sequence_mode) {
      auto from = std::string("isolated-draw-d3d12-vulkan-comparison");
      json.replace(json.find(from), from.size(),
                   "selected-sequence-d3d12-vulkan-comparison");
      json.insert(
          json.size() - 2,
          ",\"selected_draw_count\":" + std::to_string(sequence_count) +
              ",\"coverage_scope\":\"selected-same-pipeline-subset-only\"");
    }
    if (sequence_mode) {
      if (sequence_version >= 2) {
        auto from = std::string("selected-same-pipeline-subset-only");
        json.replace(json.find(from), from.size(),
                     "selected-mixed-pipeline-subset-only");
      }
      json.insert(
          json.size() - 2,
          ",\"sequence_version\":" + std::to_string(sequence_version) +
              ",\"pipeline_switches\":" + std::to_string(pipeline_switches));
    }
    write(output / (sequence_mode ? "sequence-vulkan-report.json"
                                  : "draw-vulkan-report.json"),
          json.data(), json.size());
    if (!error.empty())
      write(output / "error.txt", error.data(), error.size());
    else
      fs::remove(output / "error.txt");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  std::cout << status << ": " << error << '\n';
  return code;
}
