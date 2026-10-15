#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace d2::vulkan::shader {
enum class Stage { vertex, fragment };
struct Resource {
  uint32_t set, binding;
};
struct Interface {
  std::vector<Resource> resources;
  std::vector<uint32_t> inputs, outputs;
};
struct Module {
  std::vector<uint32_t> words;
  std::string diagnostics;
  Interface metadata;
};
// Input is bounded HLSL emitted by the pinned RSX decompilers, not arbitrary
// source with includes or external register spaces. Compilation is in-process.
std::string prepare_rsx_hlsl(const std::string &, Stage);
Interface inspect(const std::vector<uint32_t> &);
class Compiler {
  class Impl;
  std::unique_ptr<Impl> impl_;

public:
  Compiler();
  ~Compiler();
  Compiler(const Compiler &) = delete;
  Compiler &operator=(const Compiler &) = delete;
  Module compile(const std::string &, Stage);
};
} // namespace d2::vulkan::shader
