#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>
namespace d2::vulkan {
// Bounded layout checks for trusted DXC output, NOT an untrusted SPIR-V loader
// or a replacement for spirv-val. Reject features outside this experimental
// ABI.
struct RsxContract {
  std::vector<uint32_t> inputs, outputs;
  uint32_t uniform_bytes = 0;
};
inline RsxContract validateRsxModule(std::span<const uint32_t> words,
                                     bool fragment) {
  auto fail = [] {
    throw std::runtime_error("Unsupported RSX raster shader contract");
  };
  if (words.size() < 5 || words.size() > 1048576 || words[0] != 0x07230203 ||
      words[3] > 1048576)
    fail();
  struct Type {
    uint32_t op;
    std::vector<uint32_t> args;
  };
  struct Decoration {
    int set = -1, binding = -1, location = -1, stride = -1;
    bool block = false, builtin = false;
  };
  struct Variable {
    uint32_t type, storage;
  };
  std::map<uint32_t, Type> types;
  std::map<uint32_t, Decoration> dec;
  std::map<uint32_t, Variable> variables;
  std::map<uint32_t, uint32_t> constants;
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> offsets;
  unsigned entries = 0;
  for (size_t p = 5; p < words.size();) {
    auto n = words[p] >> 16, op = words[p] & 65535;
    if (!n || n > words.size() - p)
      fail();
    auto a = words.subspan(p + 1, n - 1);
    if (op == 15) { // OpEntryPoint, compiler always emits main
      if (a.size() < 4 || a[0] != (fragment ? 4u : 0u) || a[2] != 0x6e69616d ||
          a[3] != 0)
        fail();
      entries++;
    }
    if (op >= 19 && op <= 33 && a.size() >= 1)
      types[a[0]] = {op, {a.begin() + 1, a.end()}};
    if (op == 43 && a.size() == 3)
      constants[a[1]] = a[2];
    if (op == 59 && a.size() >= 3)
      variables[a[1]] = {a[0], a[2]};
    if (op == 71 && a.size() >= 2) {
      auto &d = dec[a[0]];
      if (a[1] == 2)
        d.block = true;
      if (a[1] == 11)
        d.builtin = true;
      if (a.size() >= 3) {
        if (a[1] == 6)
          d.stride = int(a[2]);
        if (a[1] == 30)
          d.location = int(a[2]);
        if (a[1] == 33)
          d.binding = int(a[2]);
        if (a[1] == 34)
          d.set = int(a[2]);
      }
    }
    if (op == 72 && a.size() == 4 && a[2] == 35)
      offsets[{a[0], a[1]}] = a[3];
    p += n;
  }
  if (entries != 1)
    fail();
  std::function<uint64_t(uint32_t, unsigned)> bytes =
      [&](uint32_t id, unsigned depth) -> uint64_t {
    if (depth > 8 || !types.count(id)) {
      fail();
      return 0;
    }
    auto &t = types.at(id);
    auto &a = t.args;
    if (t.op == 22 && a.size() == 1 && a[0] == 32)
      return 4;
    if (t.op == 23 && a.size() == 2 && a[1] == 4)
      return 4 * bytes(a[0], depth + 1);
    if (t.op == 28 && a.size() == 2 && constants.count(a[1]) &&
        dec[id].stride == 16) {
      auto count = constants.at(a[1]);
      if (!count || count > 1024 || bytes(a[0], depth + 1) != 16)
        fail();
      return uint64_t(count) * 16;
    }
    if (t.op == 30 && !a.empty() && a.size() <= 16) {
      uint64_t size = 0;
      for (uint32_t i = 0; i < a.size(); i++) {
        auto it = offsets.find({id, i});
        if (it == offsets.end() || it->second != size)
          fail();
        size += bytes(a[i], depth + 1);
        if (size > 16384)
          fail();
      }
      return size;
    }
    fail();
    return 0;
  };
  RsxContract result;
  std::set<int> bindings, input_locations, output_locations;
  for (auto [id, v] : variables) {
    auto d = dec[id];
    if (v.storage == 9)
      fail(); // no push constants
    if (d.location >= 0) {
      if (!types.count(v.type))
        fail();
      auto ptr = types.at(v.type);
      if (ptr.op != 32 || ptr.args.size() != 2 || ptr.args[0] != v.storage ||
          bytes(ptr.args[1], 0) != 16)
        fail();
      auto &locations = v.storage == 1 ? input_locations : output_locations;
      if (!locations.insert(d.location).second)
        fail();
      if (v.storage == 1)
        result.inputs.push_back(uint32_t(d.location));
      if (v.storage == 3)
        result.outputs.push_back(uint32_t(d.location));
      if (v.storage == 1 && d.location > (fragment ? 10 : 15))
        fail();
      if (v.storage == 3 && d.location > (fragment ? 3 : 10))
        fail();
    }
    if (d.binding < 0) {
      if (v.storage == 0 || v.storage == 2 || v.storage == 12)
        fail();
      continue;
    }
    if (d.set != (fragment ? 1 : 0) || !bindings.insert(d.binding).second)
      fail();
    if (!types.count(v.type))
      fail();
    auto ptr = types.at(v.type);
    if (ptr.op != 32 || ptr.args.size() != 2 || ptr.args[0] != v.storage ||
        !types.count(ptr.args[1]))
      fail();
    auto type_id = ptr.args[1];
    if (d.binding == (fragment ? 1 : 0)) {
      if (v.storage != 2 || !dec[type_id].block)
        fail();
      auto size = bytes(type_id, 0);
      result.uniform_bytes = uint32_t(size);
      if ((!fragment && size != 8224) || !size || size > 16384)
        fail();
    } else {
      if (!fragment || v.storage != 0 || (d.binding != 32 && d.binding != 64))
        fail();
      auto array = types.at(type_id);
      if (array.op != 28 || array.args.size() != 2 ||
          !constants.count(array.args[1]) ||
          constants.at(array.args[1]) != 16 || !types.count(array.args[0]))
        fail();
      auto element = types.at(array.args[0]);
      if (d.binding == 64) {
        if (element.op != 26)
          fail();
      } else { // Float Texture2D, single sample, non-array, sampled image.
        if (element.op != 25 || element.args.size() != 7 ||
            element.args[1] != 1 ||
            (element.args[2] != 0 && element.args[2] != 2) ||
            element.args[3] != 0 || element.args[4] != 0 ||
            element.args[5] != 1 || element.args[6] != 0)
          fail();
        auto scalar = types.find(element.args[0]);
        if (scalar == types.end() || scalar->second.op != 22 ||
            scalar->second.args != std::vector<uint32_t>{32})
          fail();
      }
    }
  }
  return result;
}
} // namespace d2::vulkan
