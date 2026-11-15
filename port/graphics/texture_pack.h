// Texture pack support shared by the DirectX 12 and Vulkan renderers (header only).
//
// Step 1 (this file): TEXTURE DUMP. With "Salvar texturas" on (F1 > SISTEMA, or D2_TEXTURE_DUMP=1) every distinct
// texture the game uploads is written once as a PNG to
//     %LOCALAPPDATA%\DisgaeaD2Recomp\texture-pack\dump\<hash>_<width>x<height>_f<format>.png
// (D2_TEXTURE_PACK_DIR overrides the folder). The hash is a 64-bit FNV-1a of the texture's raw bytes in guest memory
// plus its size and format, so the same texture has the same name in both renderers and in every run. The PNGs are
// what a player (or an upscaling tool) starts from to make replacements.
//
// Many Disgaea D2 sprites are palette-indexed: an 8-bit index image sampled together with a 256x1 colour palette. Those
// index images are dumped as they are (grey levels), so they look odd until the palette is applied.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#include <unordered_map>

namespace d2tex {

enum Kind { R8 = 0, RG8 = 1, RGBA8 = 2, BC1 = 3, BC2 = 4, BC3 = 5 }; // same numbering as RSX_TEXFMT_*

inline std::atomic<int>& dump_flag() { static std::atomic<int> f{-1}; return f; } // -1: not read yet

inline std::string settings_dir() {
  const char *e = std::getenv("D2_SETTINGS_DIR");
  if (e && *e) return e;
  const char *l = std::getenv("LOCALAPPDATA");
  return (l && *l) ? std::string(l) + "\\DisgaeaD2Recomp" : std::string();
}

inline std::string pack_dir() {
  const char *e = std::getenv("D2_TEXTURE_PACK_DIR");
  if (e && *e) return e;
  const std::string base = settings_dir();
  return base.empty() ? std::string() : base + "\\texture-pack";
}

inline bool dump_enabled() {
  int v = dump_flag().load();
  if (v < 0) {
    v = 0;
    if (const char *e = std::getenv("D2_TEXTURE_DUMP"); e && *e) {
      v = e[0] != '0';
    } else {
      const std::string dir = settings_dir();
      if (!dir.empty())
        if (std::FILE *f = std::fopen((dir + "\\settings.ini").c_str(), "rb")) {
          char line[256];
          while (std::fgets(line, sizeof line, f))
            if (std::strncmp(line, "texdump=", 8) == 0) v = std::atoi(line + 8) != 0;
          std::fclose(f);
        }
    }
    dump_flag().store(v);
  }
  return v > 0;
}

inline void set_dump(bool on) { dump_flag().store(on ? 1 : 0); }

inline uint64_t hash_bytes(const uint8_t *data, size_t n, uint32_t w, uint32_t h, uint32_t format) {
  uint64_t x = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) {
    x ^= data[i];
    x *= 1099511628211ull;
  }
  const uint32_t extra[3] = {w, h, format};
  const uint8_t *e = reinterpret_cast<const uint8_t *>(extra);
  for (size_t i = 0; i < sizeof extra; i++) {
    x ^= e[i];
    x *= 1099511628211ull;
  }
  return x;
}

inline void make_dirs(const std::string &path) {
  for (size_t i = 3; i < path.size(); i++)
    if (path[i] == '\\' || path[i] == '/') CreateDirectoryA(path.substr(0, i).c_str(), nullptr);
  CreateDirectoryA(path.c_str(), nullptr);
}

inline void rgb565(uint16_t c, uint8_t *o) {
  o[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
  o[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
  o[2] = uint8_t((c & 31) * 255 / 31);
}

// Decodes one 4x4 BC1/BC2/BC3 block into 16 RGBA pixels.
inline void decode_block(const uint8_t *b, int kind, uint8_t out[16][4]) {
  const uint8_t *c = kind == BC1 ? b : b + 8;
  const uint16_t c0 = uint16_t(c[0] | (c[1] << 8)), c1 = uint16_t(c[2] | (c[3] << 8));
  uint8_t col[4][4];
  rgb565(c0, col[0]);
  rgb565(c1, col[1]);
  col[0][3] = col[1][3] = col[2][3] = col[3][3] = 255;
  if (c0 > c1 || kind != BC1) {
    for (int i = 0; i < 3; i++) {
      col[2][i] = uint8_t((2 * col[0][i] + col[1][i]) / 3);
      col[3][i] = uint8_t((col[0][i] + 2 * col[1][i]) / 3);
    }
  } else {
    for (int i = 0; i < 3; i++) {
      col[2][i] = uint8_t((col[0][i] + col[1][i]) / 2);
      col[3][i] = 0;
    }
    col[3][3] = 0;
  }
  const uint32_t bits = uint32_t(c[4]) | (uint32_t(c[5]) << 8) | (uint32_t(c[6]) << 16) | (uint32_t(c[7]) << 24);
  for (int i = 0; i < 16; i++) std::memcpy(out[i], col[(bits >> (2 * i)) & 3], 4);
  if (kind == BC2) {
    for (int i = 0; i < 16; i++) {
      const uint8_t n = (b[i / 2] >> ((i & 1) * 4)) & 15;
      out[i][3] = uint8_t(n * 17);
    }
  } else if (kind == BC3) {
    const uint8_t a0 = b[0], a1 = b[1];
    uint8_t al[8] = {a0, a1};
    if (a0 > a1)
      for (int i = 1; i < 7; i++) al[i + 1] = uint8_t(((7 - i) * a0 + i * a1) / 7);
    else {
      for (int i = 1; i < 5; i++) al[i + 1] = uint8_t(((5 - i) * a0 + i * a1) / 5);
      al[6] = 0;
      al[7] = 255;
    }
    uint64_t ab = 0;
    for (int i = 0; i < 6; i++) ab |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; i++) out[i][3] = al[(ab >> (3 * i)) & 7];
  }
}

// Converts a decoded texture (rows of `pitch` bytes) to RGBA8. Returns false for formats that cannot be shown.
inline bool to_rgba(int kind, const uint8_t *data, uint32_t w, uint32_t h, uint32_t pitch, std::vector<uint8_t> &rgba) {
  rgba.assign(size_t(w) * h * 4, 255);
  if (kind == RGBA8) {
    for (uint32_t y = 0; y < h; y++) std::memcpy(&rgba[size_t(y) * w * 4], data + size_t(y) * pitch, size_t(w) * 4);
  } else if (kind == R8) {
    for (uint32_t y = 0; y < h; y++)
      for (uint32_t x = 0; x < w; x++) {
        const uint8_t v = data[size_t(y) * pitch + x];
        uint8_t *o = &rgba[(size_t(y) * w + x) * 4];
        o[0] = o[1] = o[2] = v;
      }
  } else if (kind == RG8) {
    for (uint32_t y = 0; y < h; y++)
      for (uint32_t x = 0; x < w; x++) {
        const uint8_t *s = data + size_t(y) * pitch + size_t(x) * 2;
        uint8_t *o = &rgba[(size_t(y) * w + x) * 4];
        o[0] = s[0];
        o[1] = s[1];
        o[2] = 0;
      }
  } else if (kind >= BC1 && kind <= BC3) {
    const uint32_t block = kind == BC1 ? 8 : 16;
    for (uint32_t by = 0; by < (h + 3) / 4; by++)
      for (uint32_t bx = 0; bx < (w + 3) / 4; bx++) {
        uint8_t px[16][4];
        decode_block(data + size_t(by) * pitch + size_t(bx) * block, kind, px);
        for (uint32_t i = 0; i < 16; i++) {
          const uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
          if (x < w && y < h) std::memcpy(&rgba[(size_t(y) * w + x) * 4], px[i], 4);
        }
      }
  } else {
    return false;
  }
  return true;
}

// Writes the texture as a PNG once. `source` is the guest memory the texture was read from (hash input), `decoded` the
// converted image the renderer uploads (rows of `pitch` bytes; for BC formats rows of blocks).
inline void dump(const uint8_t *source, uint32_t source_bytes, uint32_t w, uint32_t h, uint32_t format, int kind,
                 const void *decoded, uint32_t pitch) {
  if (!dump_enabled() || !source || !decoded || !w || !h || w > 4096 || h > 4096 || w * h < 16 || !source_bytes) return;
  static std::mutex lock;
  static std::unordered_set<uint64_t> seen;
  static unsigned written = 0;
  const uint64_t hash = hash_bytes(source, source_bytes, w, h, format);
  {
    std::lock_guard<std::mutex> g(lock);
    if (!seen.insert(hash).second || written >= 30000) return;
  }
  const std::string base = pack_dir();
  if (base.empty()) return;
  const std::string dir = base + "\\dump";
  make_dirs(dir);
  char name[96];
  std::snprintf(name, sizeof name, "\\%016llx_%ux%u_f%02X.png", static_cast<unsigned long long>(hash), w, h, format & 0xFFu);
  const std::string path = dir + name;
  if (std::FILE *probe = std::fopen(path.c_str(), "rb")) {
    std::fclose(probe);
    return;
  }
  std::vector<uint8_t> rgba;
  if (!to_rgba(kind, static_cast<const uint8_t *>(decoded), w, h, pitch, rgba)) return;
  if (stbi_write_png(path.c_str(), int(w), int(h), 4, rgba.data(), int(w) * 4)) {
    std::lock_guard<std::mutex> g(lock);
    if (++written == 1) std::fprintf(stderr, "[textures] saving textures to %s\n", dir.c_str());
  }
}

// ---- replacement ------------------------------------------------------------------------------------------------
// Any PNG in texture-pack\ (not in dump\) whose name starts with the 16 hex digit hash replaces that texture, so
// files copied from dump\ keep working after being edited or upscaled. The replacement may have any size.
inline std::unordered_map<uint64_t, std::string>& pack_index() {
  static std::unordered_map<uint64_t, std::string> idx;
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    const std::string dir = pack_dir();
    if (!dir.empty()) {
      WIN32_FIND_DATAA fd;
      HANDLE h = FindFirstFileA((dir + "\\*.png").c_str(), &fd);
      if (h != INVALID_HANDLE_VALUE) {
        do {
          if (std::strlen(fd.cFileName) >= 16) {
            char hex[17] = {};
            std::memcpy(hex, fd.cFileName, 16);
            char *end = nullptr;
            const uint64_t v = std::strtoull(hex, &end, 16);
            if (end == hex + 16) idx[v] = dir + "\\" + fd.cFileName;
          }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
      }
    }
    std::fprintf(stderr, "[textures] texture pack: %zu replacement(s) in %s\n", idx.size(), dir.c_str());
  }
  return idx;
}

inline bool pack_enabled() {
  static const bool on = [] {
    const char *e = std::getenv("D2_TEXTURE_PACK");
    if (e && *e) return e[0] != '0';
    return !pack_index().empty();
  }();
  return on && !pack_index().empty();
}

// Returns a malloc'd RGBA8 image (free with std::free) or nullptr.
inline uint8_t *replacement(const uint8_t *source, uint32_t source_bytes, uint32_t w, uint32_t h, uint32_t format,
                            uint32_t *ow, uint32_t *oh) {
  if (!pack_enabled() || !source || !source_bytes) return nullptr;
  auto &idx = pack_index();
  const auto it = idx.find(hash_bytes(source, source_bytes, w, h, format));
  if (it == idx.end()) return nullptr;
  int x = 0, y = 0, n = 0;
  uint8_t *px = stbi_load(it->second.c_str(), &x, &y, &n, 4);
  if (!px || x <= 0 || y <= 0) return nullptr;
  *ow = uint32_t(x);
  *oh = uint32_t(y);
  return px; // stbi allocates with malloc
}

} // namespace d2tex
