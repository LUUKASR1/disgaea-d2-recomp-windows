#include "compiler.h"
#include "fixtures.h"
#include "rsx_contract.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
namespace fs = std::filesystem;
using namespace d2::vulkan::shader;
namespace {
std::string json(const std::string &s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += char(c);
    } else if (c < 32) {
      char escape[7];
      std::snprintf(escape, sizeof(escape), "\\u%04x", unsigned(c));
      out += escape;
    } else
      out += char(c);
  }
  return out + '"';
}
struct Entry {
  std::string file, stage, compile = "failed", abi = "not-evaluated", reason;
  std::vector<uint32_t> fragment_outputs;
  uint32_t minimum_color_targets = 0;
};
struct Result {
  unsigned compiled = 0, failed = 0, gaps = 0;
  std::vector<Entry> entries;
};
void write(const fs::path &path, const std::string &data) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(data.data(), std::streamsize(data.size()));
  if (!stream)
    throw std::runtime_error("Cannot write corpus report");
}
Result check(const fs::path &input, const fs::path &output, bool fixture) {
  if (!fs::is_directory(input) || fs::is_symlink(fs::symlink_status(input)))
    throw std::runtime_error("Input must be a regular directory");
  fs::create_directories(output);
  std::vector<fs::path> files;
  for (auto &e : fs::directory_iterator(input)) {
    if (e.path().extension() != ".hlsl")
      continue;
    if (fs::is_symlink(e.symlink_status()) || !e.is_regular_file())
      throw std::runtime_error("Non-regular HLSL input rejected");
    files.push_back(e.path());
    if (files.size() > 256)
      throw std::runtime_error("Corpus exceeds 256 HLSL files");
  }
  if (files.empty())
    throw std::runtime_error(
        "No HLSL files found; select the captured shaders folder");
  std::sort(files.begin(), files.end());
  Compiler compiler;
  Result result;
  uint64_t total = 0;
  for (auto &path : files) {
    Entry entry;
    entry.file = path.filename().string();
    try {
      auto name = path.stem().string();
      if (name.size() < 4 ||
          name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRS"
                                 "TUVWXYZ0123456789_-") != std::string::npos)
        throw std::runtime_error("Unsafe/unknown shader name");
      Stage stage;
      if (name.starts_with("vp_")) {
        stage = Stage::vertex;
        entry.stage = "vertex";
      } else if (name.starts_with("fp_")) {
        stage = Stage::fragment;
        entry.stage = "fragment";
      } else
        throw std::runtime_error("Name must start with vp_ or fp_");
      fs::remove(
          output /
          (name +
           ".spv")); // Never leave a stale successful module after a failure.
      auto bytes = fs::file_size(path);
      if (!bytes || bytes > 262144 || total + bytes > 33554432)
        throw std::runtime_error("Corpus size limit exceeded");
      total += bytes;
      std::ifstream file(path, std::ios::binary);
      std::string source(size_t(bytes), '\0');
      file.read(source.data(), std::streamsize(bytes));
      if (!file || file.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Shader changed during read");
      auto module = compiler.compile(source, stage);
      auto binary = output / (name + ".spv");
      std::ofstream spirv(binary, std::ios::binary);
      spirv.write(reinterpret_cast<const char *>(module.words.data()),
                  std::streamsize(module.words.size() * 4));
      if (!spirv)
        throw std::runtime_error("Cannot save compiled shader");
      entry.compile = "passed";
      result.compiled++;
      try {
        auto contract = d2::vulkan::validateRsxModule(module.words,
                                                      stage == Stage::fragment);
        if (stage == Stage::fragment) {
          entry.fragment_outputs = contract.outputs;
          std::sort(entry.fragment_outputs.begin(),
                    entry.fragment_outputs.end());
          for (auto location : entry.fragment_outputs)
            entry.minimum_color_targets =
                std::max(entry.minimum_color_targets, location + 1);
        }
        entry.abi = "supported";
      } catch (const std::exception &error) {
        entry.abi = "unsupported";
        entry.reason = error.what();
        result.gaps++;
      }
    } catch (const std::exception &error) {
      entry.reason = error.what();
      result.failed++;
    }
    result.entries.push_back(std::move(entry));
  }
  std::ostringstream report;
  report << "{\"schema\":1,\"purpose\":\"runtime-hlsl-vulkan-compilation\","
            "\"status\":"
         << json(result.failed ? "compile-failures"
                 : result.gaps ? "compiled-with-renderer-gaps"
                               : "compiled")
         << ",\"input_is_synthetic_fixture\":" << (fixture ? "true" : "false")
         << ",\"compiled\":" << result.compiled
         << ",\"compile_failed\":" << result.failed
         << ",\"renderer_abi_gaps\":" << result.gaps
         << ",\"game_backend_ready\":false,\"pixel_equivalence_tested\":false,"
            "\"shader_pairs_linked\":false,\"rpcs3_comparison_performed\":"
            "false,\"mrt_scope\":\"offscreen-rgba8-only\","
            "\"presentation_color_targets\":1,\"shaders\":[";
  for (size_t i = 0; i < result.entries.size(); i++) {
    auto &e = result.entries[i];
    if (i)
      report << ',';
    report << "{\"file\":" << json(e.file) << ",\"stage\":" << json(e.stage)
           << ",\"compile_status\":" << json(e.compile)
           << ",\"renderer_abi\":" << json(e.abi)
           << ",\"reason\":" << json(e.reason)
           << ",\"minimum_color_targets\":" << e.minimum_color_targets
           << ",\"fragment_output_locations\":[";
    for (size_t j = 0; j < e.fragment_outputs.size(); j++) {
      if (j)
        report << ',';
      report << e.fragment_outputs[j];
    }
    report << "]}";
  }
  report << "]}\n";
  write(output / "corpus-vulkan-report.json", report.str());
  return result;
}
} // namespace
int main(int argc, char **argv) {
  fs::path input, output = "corpus-check";
  bool fixture = false;
  try {
    for (int i = 1; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--input" && i + 1 < argc)
        input = argv[++i];
      else if (arg == "--output" && i + 1 < argc)
        output = argv[++i];
      else if (arg == "--self-test")
        fixture = true;
      else
        throw std::runtime_error("Usage: DisgaeaD2ShaderCorpus --input shaders "
                                 "[--output corpus-check]");
    }
    if (fixture) {
      if (!input.empty())
        throw std::runtime_error("Do not combine self-test and user input");
      input = output / "synthetic-input";
      fs::create_directories(input);
      write(input / "fp_000_broken.hlsl", "invalid HLSL");
      write(input / "fp_001_good.hlsl", fixtures::fragment(false));
      write(input / "vp_002_good.hlsl", fixtures::vertex());
      write(input / "fp_003_gap.hlsl",
            "cbuffer Values:register(b0){float4 color;} float4 "
            "main():SV_TARGET{return color;}");
      write(input / "fp_004_mrt.hlsl",
            "struct Out{float4 a:SV_TARGET0;float4 b:SV_TARGET2;};"
            "Out main(){Out o;o.a=float4(1,0,0,1);o.b=float4(0,1,0,1);return "
            "o;}");
    }
    if (input.empty())
      throw std::runtime_error(
          "Select the captured shaders folder with --input");
    auto result = check(input, output, fixture);
    if (fixture) {
      auto mrt = std::find_if(
          result.entries.begin(), result.entries.end(),
          [](const Entry &e) { return e.file == "fp_004_mrt.hlsl"; });
      if (result.compiled != 4 || result.failed != 1 || result.gaps != 1 ||
          mrt == result.entries.end() ||
          mrt->fragment_outputs != std::vector<uint32_t>{0, 2} ||
          mrt->minimum_color_targets != 3)
        throw std::runtime_error(
            "Corpus continuation/ABI classification self-test failed");
      write(output / "self-test.json", "{\"schema\":1,\"status\":\"passed\","
                                       "\"real_game_shaders_tested\":false}\n");
      std::cout << "Synthetic corpus: compile failure isolated, later shaders "
                   "compiled, renderer gap classified\n";
      return 0;
    }
    std::cout << result.compiled << " compiled; " << result.failed
              << " failed; " << result.gaps << " renderer ABI gaps\n";
    return result.failed ? 1 : result.gaps ? 2 : 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
