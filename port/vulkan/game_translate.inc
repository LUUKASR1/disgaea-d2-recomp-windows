// Disgaea D2 - Vulkan backend, milestone M2: guest draw translation.
// Included inside the anonymous namespace of rsx_vulkan_backend.cpp, game
// build only (D2_VULKAN_GAME_BACKEND). Mirrors what rsx_d3d12_backend.c does
// per draw (vertex staging, constant layout, FP post-processing, texture
// decode) but records into the renderer's draw-list path.
//
// Scope of this first slice: draws that target the display surface, QUADS /
// TRIANGLES / STRIP / FAN (array and indexed), 2D textures decoded to RGBA8
// or BC1-3. Everything else is counted in `tr::skipped` and reported.
namespace tr {
using namespace d2::vulkan;

struct Counters {
  uint64_t recorded = 0, skipped_target = 0, skipped_prim = 0,
           skipped_shader = 0, skipped_other = 0, skipped_cap = 0,
           tex_uploads = 0, tex_unsupported = 0, compiles = 0, cache_hits = 0,
           compile_failures = 0, pipelines = 0;
} cnt;

std::unique_ptr<shader::Compiler> compiler;
bool ready = false;
uint64_t frame_no = 0;
uint32_t guest_w = 1280, guest_h = 720;

void log_once(const char *key, const char *fmt, ...) {
  static std::set<std::string> seen;
  if (!seen.insert(key).second) return;
  va_list ap;
  va_start(ap, fmt);
  std::fprintf(stderr, "[vulkan][m2] ");
  std::vfprintf(stderr, fmt, ap);
  std::fprintf(stderr, "\n");
  va_end(ap);
}

std::string replace_all(std::string s, const std::string &from,
                        const std::string &to) {
  if (from.empty()) return s;
  size_t at = 0;
  while ((at = s.find(from, at)) != std::string::npos) {
    s.replace(at, from.size(), to);
    at += to.size();
  }
  return s;
}

// ---- shaders ----------------------------------------------------------------
struct Module {
  std::vector<uint32_t> words;
  uint32_t nconst = 0;
  bool ok = false;
};
std::map<uint64_t, Module> vs_cache;
std::map<uint64_t, Module> fp_cache;

// Same palette workaround as the D3D12 backend (d2_fix_palette_hlsl).
std::string fix_palette(std::string hlsl, uint32_t code_hash, uint32_t constants) {
  if (code_hash != 0x72ECBE66u || constants != 31u) return hlsl;
  const std::string sample = "rsx_tex[1].Sample(rsx_samp[1], ";
  const std::string anchor = "struct PSOutput {";
  size_t count = 0, at = 0;
  while ((at = hlsl.find(sample, at)) != std::string::npos) { count++; at += sample.size(); }
  if (count != 4 || hlsl.find(anchor) == std::string::npos) return hlsl;
  const std::string function =
      "float4 d2_palette_lookup(float2 uv) {\n"
      "    uint w, h; rsx_tex[1].GetDimensions(w, h);\n"
      "    int2 size = int2(w, h);\n"
      "    int2 p = clamp(int2(floor(uv * float2(w, h))), int2(0, 0), size - int2(1, 1));\n"
      "    return rsx_tex[1].Load(int3(p, 0));\n"
      "}\n";
  hlsl = replace_all(hlsl, sample, "d2_palette_lookup(");
  return replace_all(hlsl, anchor, function + anchor);
}

// Four independent 64-bit lanes: ~10x faster than a byte-wise FNV over the 4 KiB
// microcode buffer that every draw hashes. Only used as an in-process cache key.
uint64_t fast_hash(const uint8_t *p, size_t n) {
  uint64_t h0 = 0x9E3779B97F4A7C15ull, h1 = 0xC2B2AE3D27D4EB4Full, h2 = 0x165667B19E3779F9ull, h3 = 0x27D4EB2F165667C5ull;
  size_t i = 0;
  for (; i + 32 <= n; i += 32) {
    uint64_t a, b, c, d;
    std::memcpy(&a, p + i, 8); std::memcpy(&b, p + i + 8, 8); std::memcpy(&c, p + i + 16, 8); std::memcpy(&d, p + i + 24, 8);
    h0 = (h0 ^ a) * 0x100000001B3ull; h0 ^= h0 >> 31;
    h1 = (h1 ^ b) * 0x100000001B3ull; h1 ^= h1 >> 31;
    h2 = (h2 ^ c) * 0x100000001B3ull; h2 ^= h2 >> 31;
    h3 = (h3 ^ d) * 0x100000001B3ull; h3 ^= h3 >> 31;
  }
  for (; i < n; i++) h0 = (h0 ^ p[i]) * 0x100000001B3ull;
  uint64_t h = h0 ^ (h1 * 31) ^ (h2 * 131) ^ (h3 * 1031) ^ n;
  h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
  return h;
}
// Compiled SPIR-V is cached on disk (keyed by the final HLSL text, the stage and the pinned
// DXC build) so a program is compiled by DXC only the first time it ever appears: the
// 50-200 ms compile otherwise lands in the middle of gameplay as a visible hitch.
std::vector<uint32_t> compile_cached(const std::string &hlsl, shader::Stage stage) {
  namespace fs = std::filesystem;
  static const std::string dir = [] {
    const char *e = std::getenv("D2_VULKAN_CACHE_DIR");
    return std::string(e && *e ? e : "vulkan-shader-cache");
  }();
  const bool off = [] { const char *e = std::getenv("D2_VULKAN_SHADER_CACHE"); return e && e[0] == '0'; }();
  char name[64] = {};
  if (!off) {
#ifdef D2_SHADER_GLSLANG
    const std::string tag = std::string("glslang-1|") + (stage == shader::Stage::vertex ? "v|" : "f|");
#else
    const std::string tag = std::string("dxc-1.8.2505.1|") + (stage == shader::Stage::vertex ? "v|" : "f|");
#endif
    const uint64_t h1 = fast_hash(reinterpret_cast<const uint8_t *>(hlsl.data()), hlsl.size());
    const uint64_t h2 = fast_hash(reinterpret_cast<const uint8_t *>(tag.data()), tag.size()) ^ (h1 * 0x9E3779B97F4A7C15ull);
    std::snprintf(name, sizeof name, "%016llX%016llX.spv", (unsigned long long)h1, (unsigned long long)h2);
    std::error_code ec;
    const fs::path path = fs::path(dir) / name;
    if (std::FILE *f = std::fopen(path.string().c_str(), "rb")) {
      std::fseek(f, 0, SEEK_END);
      const long n = std::ftell(f);
      std::fseek(f, 0, SEEK_SET);
      std::vector<uint32_t> w;
      if (n >= 20 && n % 4 == 0 && n < (16 << 20)) {
        w.resize(size_t(n) / 4);
        if (std::fread(w.data(), 1, size_t(n), f) != size_t(n)) w.clear();
      }
      std::fclose(f);
      if (!w.empty() && w[0] == 0x07230203u) { cnt.cache_hits++; return w; }
    }
  }
  std::vector<uint32_t> words = compiler->compile(hlsl, stage).words;
  if (!off && !words.empty()) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path path = fs::path(dir) / name, tmp = fs::path(dir) / (std::string(name) + ".tmp");
    if (std::FILE *f = std::fopen(tmp.string().c_str(), "wb")) {
      const bool wrote = std::fwrite(words.data(), 4, words.size(), f) == words.size();
      std::fclose(f);
      if (wrote) fs::rename(tmp, path, ec);
      else fs::remove(tmp, ec);
    }
  }
  return words;
}
Module &get_vs(const rsx_state *st, uint64_t &key) {
  uint32_t start = 0;
  if (st->transform_program_start * 16u < st->vp_ucode_bytes)
    start = st->transform_program_start * 16u;
  const uint8_t *code = st->vp_ucode + start;
  const uint32_t len = st->vp_ucode_bytes - start;
  key = fast_hash(code, std::min<uint32_t>(len, 4096));
  auto it = vs_cache.find(key);
  if (it != vs_cache.end()) return it->second;
  Module &m = vs_cache[key];
  static char hlsl[262144];
  const int n = rsx_vp_decompile(code, len, hlsl, sizeof hlsl);
  if (n <= 0) {
    log_once(("vsdec" + std::to_string(key)).c_str(), "VP %08llX decompile failed", (unsigned long long)(key & 0xFFFFFFFF));
    cnt.compile_failures++;
    return m;
  }
  try {
    m.words = compile_cached(hlsl, shader::Stage::vertex);
    m.ok = true;
    cnt.compiles++;
  } catch (const std::exception &e) {
    cnt.compile_failures++;
    log_once(("vsc" + std::to_string(key)).c_str(), "VP %08llX compile failed: %.300s", (unsigned long long)(key & 0xFFFFFFFF), e.what());
  }
  return m;
}

Module &get_fp(const rsx_state *st, uint64_t &key, const uint8_t *&code_out) {
  static Module none;
  none = Module{};
  code_out = nullptr;
  const u32 fa = st->shader_program;
  const u32 off = cellGcmResolveLocated((fa & 0x3u) == 1, fa & ~0x3u);
  if (!vm_base || off == 0xFFFFFFFFu) return none;
  const uint8_t *code = vm_base + off;
  code_out = code;
  const uint32_t uhash = d2_fp_code_hash(code, 4096);
  key = (uint64_t(uhash) << 32) | rsx_fp_program_size(code, 4096);
  auto it = fp_cache.find(key);
  if (it != fp_cache.end()) return it->second;
  Module &m = fp_cache[key];
  static char hlsl[32768];
  u32 nconst = 0;
  const int n = rsx_fp_decompile_buffered_ex(code, 4096, RSX_FP_CTRL_AUTO, 0u, hlsl, sizeof hlsl, &nconst);
  if (n <= 0 || nconst > 64u) {
    cnt.compile_failures++;
    log_once(("fpdec" + std::to_string(key)).c_str(), "FP 0x%X decompile failed (n=%d consts=%u)", fa, n, nconst);
    return m;
  }
  std::string h = hlsl;
  char oldb[192], newb[320];
  std::snprintf(oldb, sizeof oldb,
                "cbuffer PSConstants : register(b1) {\n"
                "    float4 fp_constants[%u];\n"
                "    float4 fp_alpha;\n"
                "};\n", nconst ? nconst : 1u);
  std::snprintf(newb, sizeof newb,
                "cbuffer PSConstants : register(b1) {\n"
                "    float4 rsx_texscale[4] : packoffset(c0);\n"
                "    float4 fp_alpha : packoffset(c4);\n"
                "    float4 fp_constants[%u] : packoffset(c5);\n"
                "};\n", nconst ? nconst : 1u);
  if (h.find(oldb) == std::string::npos) {
    cnt.compile_failures++;
    log_once(("fpdecl" + std::to_string(key)).c_str(), "FP 0x%X: unexpected constant declaration", fa);
    return m;
  }
  h = replace_all(h, oldb, newb);
  const char *alpha =
      "if (fp_alpha.x > 0.5) {\n"
      " float d2_a = D2_ALPHA; int d2_f = (int)fp_alpha.z;\n"
      " bool d2_pass = d2_f == 7 || (d2_f == 1 && d2_a < fp_alpha.y)\n"
      " || (d2_f == 2 && d2_a == fp_alpha.y) || (d2_f == 3 && d2_a <= fp_alpha.y)\n"
      " || (d2_f == 4 && d2_a > fp_alpha.y) || (d2_f == 5 && d2_a != fp_alpha.y)\n"
      " || (d2_f == 6 && d2_a >= fp_alpha.y); if (!d2_pass) discard; }\n";
  if (h.find("return _out;") != std::string::npos)
    h = replace_all(h, "return _out;", replace_all(std::string(alpha) + "return _out;", "D2_ALPHA", "_out.t0.a"));
  else if (h.find("return (_o == _o)") != std::string::npos)
    h = replace_all(h, "return (_o == _o)", replace_all(std::string(alpha) + "return (_o == _o)", "D2_ALPHA", "_o.a"));
  h = fix_palette(h, uhash, nconst);
  try {
    m.words = compile_cached(h, shader::Stage::fragment);
    m.nconst = nconst;
    m.ok = true;
    cnt.compiles++;
  } catch (const std::exception &e) {
    cnt.compile_failures++;
    log_once(("fpc" + std::to_string(key)).c_str(), "FP 0x%X compile failed: %.300s", fa, e.what());
  }
  return m;
}

// ---- state -> RasterState ----------------------------------------------------
BlendFactor blend_factor(u32 f, bool alpha) {
  switch (f & 0xFFFF) {
  case 0x0000: return BlendFactor::zero;
  case 0x0001: return BlendFactor::one;
  case 0x0300: return alpha ? BlendFactor::src_alpha : BlendFactor::src_color;
  case 0x0301: return alpha ? BlendFactor::inv_src_alpha : BlendFactor::inv_src_color;
  case 0x0302: return BlendFactor::src_alpha;
  case 0x0303: return BlendFactor::inv_src_alpha;
  case 0x0304: return BlendFactor::dst_alpha;
  case 0x0305: return BlendFactor::inv_dst_alpha;
  case 0x0306: return alpha ? BlendFactor::dst_alpha : BlendFactor::dst_color;
  case 0x0307: return alpha ? BlendFactor::inv_dst_alpha : BlendFactor::inv_dst_color;
  case 0x0308: return BlendFactor::src_alpha_saturate;
  default: return BlendFactor::one;
  }
}
BlendOperation blend_op(u32 e) {
  switch (e & 0xFFFF) {
  case 0x8007: return BlendOperation::minimum;
  case 0x8008: return BlendOperation::maximum;
  case 0x800A: return BlendOperation::subtract;
  case 0x800B: return BlendOperation::reverse_subtract;
  default: return BlendOperation::add;
  }
}
RasterState make_raster(const rsx_state *st) {
  RasterState r;
  r.discard_unbound_outputs = true; // FPs may declare more outputs than we bind
  if (st->cull_face_enable) {
    r.cull = (st->cull_face == 0x0404u || st->cull_face == 0x0408u) ? 1 : 2;
    r.front_ccw = st->front_face == 0x0901u;
  }
  const u32 cm = st->color_mask;
  r.color_mask = ((cm & 0x00010000) ? 1u : 0u) | ((cm & 0x00000100) ? 2u : 0u) |
                 ((cm & 0x00000001) ? 4u : 0u) | ((cm & 0x01000000) ? 8u : 0u);
  const u32 df = st->depth_func;
  r.depth_compare = (df >= 0x200u && df <= 0x207u) ? df - 0x200u : 1u;
  if (st->stencil_test_enable) {
    auto cmp = [](u32 f) -> u32 { return (f >= 0x200u && f <= 0x207u) ? f - 0x200u : (f < 8u ? f : 3u); };
    auto op = [](u32 o) -> u32 {
      switch (o) {
      case 0x1E00u: return 0; case 0u: return 1; case 0x1E01u: return 2; case 0x1E02u: return 3;
      case 0x1E03u: return 4; case 0x150Au: return 5; case 0x8507u: return 6; case 0x8508u: return 7;
      default: return 0;
      }
    };
    r.stencil_enable = true;
    r.stencil_front = cmp(st->stencil_func) | (op(st->stencil_op_fail) << 3) | (op(st->stencil_op_zfail) << 6) | (op(st->stencil_op_zpass) << 9);
    r.stencil_back = st->d2_stencil_two_sided
        ? cmp(st->d2_back_stencil_func) | (op(st->d2_back_stencil_fail) << 3) | (op(st->d2_back_stencil_zfail) << 6) | (op(st->d2_back_stencil_zpass) << 9)
        : r.stencil_front;
    r.stencil_read_mask = st->stencil_mask & 255u;
    r.stencil_write_mask = st->d2_stencil_write_mask & 255u;
    r.stencil_ref = st->stencil_ref & 255u;
  }
  if (st->blend_enable && (st->blend_sfactor || st->blend_dfactor)) {
    r.src_color = blend_factor(st->blend_sfactor, false);
    r.dst_color = blend_factor(st->blend_dfactor, false);
    r.src_alpha = blend_factor(st->blend_sfactor >> 16, true);
    r.dst_alpha = blend_factor(st->blend_dfactor >> 16, true);
    r.color_op = blend_op(st->blend_equation);
    r.alpha_op = blend_op(st->blend_equation >> 16);
  }
  return r;
}

// ---- textures -----------------------------------------------------------------
struct TexEntry {
  uint32_t handle = 0, csum = 0;
  uint64_t used_frame = ~0ull;
  uint32_t face_bytes = 0;
};
std::map<std::array<uint32_t, 6>, TexEntry> tex_cache;

uint32_t tex_csum(const uint8_t *base, uint32_t nbytes) {
  uint32_t h = 2166136261u;
  const uint32_t step = nbytes > 4096u ? nbytes / 1024u : 4u;
  for (uint32_t i = 0; i + 3 < nbytes; i += step) {
    uint32_t w;
    std::memcpy(&w, base + i, 4);
    h ^= w;
    h *= 16777619u;
  }
  return h;
}

using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}
uint64_t const_epoch = 0; // bumps whenever the renderer's per-frame constant blocks may be reset
double f_record = 0, f_texup = 0; // per-frame accumulators (reset in submit)
uint32_t texture_for(Renderer &r, uint32_t unit, const rsx_texture_state &t) {
  const uint32_t w = (t.image_rect >> 16) & 0xFFFFu, h = t.image_rect & 0xFFFFu;
  const uint32_t format = (t.format >> 8) & 0xFFu;
  if (!t.offset || !w || !h || !vm_base) return 0;
  if (t.format & 4) { log_once("cube", "cube textures are not supported yet"); cnt.tex_unsupported++; return 0; }
  const uint32_t off = cellGcmResolveLocated((t.format & 3) == 1, t.offset);
  if (off == 0xFFFFFFFFu) return 0;
  const uint32_t pitch = t.control3 & 0xFFFFFu;
  rsx_tex_layout tl;
  rsx_texture_layout_pitched(format, w, h, pitch, &tl);
  const bool bc = tl.compressed;
  const bool rgba = tl.fmt == RSX_TEXFMT_R8G8B8A8 || tl.fmt == RSX_TEXFMT_R8 || tl.fmt == RSX_TEXFMT_R8G8;
  if (!bc && !rgba) {
    char key[64];
    std::snprintf(key, sizeof key, "texfmt%02X", format & 0x9F);
    log_once(key, "texture format 0x%02X is not supported yet", format);
    cnt.tex_unsupported++;
    return 0;
  }
  const std::array<uint32_t, 6> key{off, w, h, format, pitch, t.control1};
  auto &e = tex_cache[key];
  const uint8_t *src = vm_base + off;
  if (e.handle && e.used_frame == frame_no) return e.handle;
  const auto tup0 = Clock::now();
  const uint32_t cs = tex_csum(src, tl.face_bytes);
  if (e.handle && cs == e.csum) { e.used_frame = frame_no; f_texup += ms(tup0, Clock::now()); return e.handle; }
  // Decode to a tight RGBA8 (or BC block) image.
  std::vector<uint8_t> out, dumpsrc;
  uint32_t bcf = 0;
  if (bc) {
    bcf = tl.fmt == RSX_TEXFMT_BC1 ? 1 : tl.fmt == RSX_TEXFMT_BC2 ? 2 : 3;
    out.resize(size_t(tl.row_bytes) * tl.rows);
    rsx_texture_decode(out.data(), tl.row_bytes, src, w, h, &tl, rsx_texture_argb_is_rgba());
  } else {
    std::vector<uint8_t> tmp(size_t(tl.dst_row_bytes) * h);
    rsx_texture_decode(tmp.data(), tl.dst_row_bytes, src, w, h, &tl, rsx_texture_argb_is_rgba());
    if (d2_texture_dump_enabled()) dumpsrc = tmp;
    out.resize(size_t(w) * h * 4);
    const uint32_t dbpp = tl.dst_bytes_per_texel;
    for (uint32_t y = 0; y < h; y++)
      for (uint32_t x = 0; x < w; x++) {
        const uint8_t *s = tmp.data() + size_t(y) * tl.dst_row_bytes + size_t(x) * dbpp;
        uint8_t *d = out.data() + (size_t(y) * w + x) * 4;
        if (dbpp == 4) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
        else if (dbpp == 2) { d[0] = s[0]; d[1] = s[1]; d[2] = 0; d[3] = 255; }
        else { d[0] = s[0]; d[1] = 0; d[2] = 0; d[3] = 255; }
      }
  }
  d2_texture_dump(src, tl.face_bytes, w, h, format & 0xFFu, int(tl.fmt), bc ? static_cast<const void *>(out.data()) : static_cast<const void *>(dumpsrc.data()), bc ? tl.row_bytes : tl.dst_row_bytes);
  uint32_t cw = w, ch = h;
  if (d2_texture_pack_enabled()) {
    uint32_t rw = 0, rh = 0;
    if (uint8_t *rp = d2_texture_replacement(src, tl.face_bytes, w, h, format & 0xFFu, &rw, &rh)) {
      out.assign(rp, rp + size_t(rw) * rh * 4);
      d2_texture_free(rp);
      bcf = 0; cw = rw; ch = rh;
    }
  }
  uint8_t remap[4];
  rsx_texture_component_remap(t.control1, format & 0x9Fu, remap);
  const uint32_t mapping = (remap[1] | (remap[2] << 3) | (remap[3] << 6) | (remap[0] << 9)) & 0xFFFu;
  if (e.handle) { r.destroyTexture(e.handle); e.handle = 0; }
  e.handle = r.createTexture(cw, ch, bcf, mapping, out, unit != 0, unit != 0);
  e.csum = cs;
  e.used_frame = frame_no;
  e.face_bytes = tl.face_bytes;
  cnt.tex_uploads++;
  f_texup += ms(tup0, Clock::now());
  return e.handle;
}

// ---- recording ----------------------------------------------------------------
struct Rec {
  uint32_t first, count, program, vs_block, fp_block;
  std::array<uint32_t, 16> textures;
  Rect viewport, scissor;
  DepthMode depth;
  bool blend;
};
std::vector<Rec> recs;
std::vector<RsxVertex> verts;
std::vector<uint32_t> indices;   // every recorded draw is indexed: Rec::first/count address this list
std::map<std::array<uint32_t, 10>, uint32_t> pipelines;
constexpr size_t kMaxVerts = 1000000;
constexpr size_t kMaxIndices = 3000000;
// Draw composition (printed with the counters): how many draws per primitive type, and for indexed draws how many index
// entries there were against distinct vertices.
uint64_t prim_draws[16] = {}, prim_verts[16] = {}, idx_draws = 0, idx_entries = 0, idx_unique = 0, idx_deduped = 0;
void print_mix() {
  static const char *const names[16] = {"?0", "points", "lines", "line_loop", "line_strip", "triangles", "tri_strip", "tri_fan",
                                        "quads", "quad_strip", "polygon", "?11", "?12", "?13", "?14", "?15"};
  std::fprintf(stderr, "[vulkan][m2] draw mix since start:");
  for (int i = 0; i < 16; i++)
    if (prim_draws[i]) std::fprintf(stderr, " %s=%llu draws/%llu verts", names[i], (unsigned long long)prim_draws[i], (unsigned long long)prim_verts[i]);
  std::fprintf(stderr, " | indexed draws=%llu, deduplicated=%llu (%llu index entries -> %llu distinct vertices)\n",
               (unsigned long long)idx_draws, (unsigned long long)idx_deduped, (unsigned long long)idx_entries, (unsigned long long)idx_unique);
}
#ifdef __ANDROID__
// CPU time of the calling thread / the whole process, in ms: tells busy time from time spent waiting.
double cpu_ms(clockid_t id) {
  timespec ts{};
  clock_gettime(id, &ts);
  return double(ts.tv_sec) * 1e3 + double(ts.tv_nsec) / 1e6;
}
#endif

uint32_t read_index(const rsx_state *st, uint32_t k) {
  const int local = ((st->index_array_dma & 0xF) == 0);
  const u32 base = cellGcmResolveLocated(local, st->index_array_offset);
  const uint8_t *p = vm_base + base;
  if (((st->index_array_dma >> 4) & 0xF) == 1) {
    p += size_t(k) * 2;
    return (uint32_t(p[0]) << 8) | p[1];
  }
  p += size_t(k) * 4;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Per-draw template: the constant vertex attribute registers, sanitised once, plus a decode plan per enabled
// attribute. LOCAL (VRAM) arrays without a frequency divider are linear in host memory, so their base address and
// component decoder are resolved once per draw instead of once per vertex; anything else goes through the generic
// rsx_fetch_attrib().
struct AttrPlan {
  int idx = 0;
  bool fast = false;
  uint32_t type = 0, n = 4, stride = 0, offset = 0, local_base = 0;
};
struct VertexTemplate {
  RsxVertex base;
  std::array<AttrPlan, 16> plan{};
  int count = 0;
};
VertexTemplate make_template(const rsx_state *st) {
  VertexTemplate t;
  std::memcpy(t.base.attributes.data(), st->vertex_data4f, sizeof t.base.attributes);
  for (auto &a : t.base.attributes)
    for (float &c : a)
      if (!std::isfinite(c)) c = 0.f;
  static const bool no_fast = std::getenv("D2_VULKAN_SLOW_FETCH") != nullptr;
  for (int i = 0; i < 16; i++) {
    const rsx_vertex_attrib *a = &st->vertex_attribs[i];
    if (a->enabled && a->stride && vm_base) {
      AttrPlan &p = t.plan[t.count++];
      p.idx = i;
      p.type = a->type;
      p.n = a->size ? std::min<uint32_t>(a->size, 4u) : 4u;
      p.stride = a->stride;
      p.offset = a->offset & 0x7FFFFFFFu;
      p.local_base = cellGcmResolveLocated(1, 0);
      const bool known = a->type == 2 || a->type == 3 || a->type == 4 || a->type == 1 || a->type == 5 || a->type == 7;
      p.fast = !no_fast && known && !(a->offset & 0x80000000u) && a->frequency <= 1;
    }
  }
  return t;
}
static inline float be_f32(const uint8_t *p) {
  uint32_t w;
  std::memcpy(&w, p, 4);
  w = __builtin_bswap32(w);
  float f;
  std::memcpy(&f, &w, 4);
  return f;
}
// `out` already holds the template (filled in bulk by the caller): only the enabled attributes are fetched.
inline void fetch_vertex(const rsx_state *st, const VertexTemplate &t, uint32_t vi, RsxVertex &out) {
  for (int k = 0; k < t.count; k++) {
    const AttrPlan &a = t.plan[k];
    float *v = out.attributes[a.idx].data();
    if (!a.fast) {
      rsx_fetch_attrib(st, a.idx, vi, v);
      for (int c = 0; c < 4; c++)
        if (!std::isfinite(v[c])) v[c] = 0.f;
      continue;
    }
    const uint32_t off = a.offset + vi * a.stride;                 // same 32-bit wrap as rsx_fetch_attrib
    const uint8_t *p = vm_base + uint32_t(a.local_base + off);
    switch (a.type) {
      case 2:  // float32 BE
        for (uint32_t c = 0; c < a.n; c++) { const float f = be_f32(p + c * 4); v[c] = std::isfinite(f) ? f : 0.f; }
        break;
      case 3:  // half float BE
        for (uint32_t c = 0; c < a.n; c++) { const float f = rsx_rd_half_be(p + c * 2); v[c] = std::isfinite(f) ? f : 0.f; }
        break;
      case 4:  // u8 normalised
        for (uint32_t c = 0; c < a.n; c++) v[c] = p[c] / 255.0f;
        break;
      case 1:  // s16 normalised
        for (uint32_t c = 0; c < a.n; c++) v[c] = (float)(int16_t)((p[c * 2] << 8) | p[c * 2 + 1]) / 32767.0f;
        break;
      case 5:  // s16 integer
        for (uint32_t c = 0; c < a.n; c++) v[c] = (float)(int16_t)((p[c * 2] << 8) | p[c * 2 + 1]);
        break;
      default:  // 7: u8 unnormalised
        for (uint32_t c = 0; c < a.n; c++) v[c] = (float)p[c];
        break;
    }
  }
}

// RSX splits one primitive stream into several draw calls of at most 256
// vertices inside a single BEGIN/END, and 256 is not a multiple of 3: a
// triangle list must be assembled over the concatenated stream or every batch
// after the first is out of phase (spike triangles, missing triangles). Like the
// D3D12 backend, continuity is "same begin_epoch and first == previous end";
// the 0..2 vertices left over by a batch are carried into the next one.
struct Stream {
  bool valid = false;
  uint32_t epoch = 0, end = 0, prim = 0;
  std::vector<RsxVertex> carry;
} stream;
double t_record = 0, t_geom = 0, t_draw = 0, t_fetch = 0, t_consts = 0, t_tex = 0;
uint64_t verts_total = 0, frames_timed = 0;

void record(Renderer &r, const rsx_state *st, bool indexed, u32 prim, u32 first, u32 count) {
  const auto t0 = Clock::now();
  struct Timer { Clock::time_point t; ~Timer() { const double d = ms(t, Clock::now()); t_record += d; f_record += d; } } timer{t0};
  if (!ready || !st || st->vp_ucode_bytes < 16 || !st->vertex_attribs[0].enabled) {
    cnt.skipped_other++;
    stream.valid = false;
    return;
  }
  if (!cellGcmOffsetIsDisplay(st->surface_color_offset[st->color_target == 2 ? 1 : 0]) || st->color_target >= 0x13) {
    cnt.skipped_target++;
    stream.valid = false;
    return;
  }
  if (st->surface_clip_w && st->surface_clip_h) { guest_w = st->surface_clip_w; guest_h = st->surface_clip_h; }
  const bool tri_list = prim == RSX_PRIMITIVE_TRIANGLES;
  const bool cont = tri_list && stream.valid && stream.prim == prim &&
                    stream.epoch == st->begin_epoch && stream.end == first;
  if (!cont) stream.carry.clear();
  stream.valid = false;
  if (prim != RSX_PRIMITIVE_QUADS && !tri_list && prim != RSX_PRIMITIVE_TRIANGLE_STRIP &&
      prim != RSX_PRIMITIVE_TRIANGLE_FAN) {
    cnt.skipped_prim++;
    log_once(("prim" + std::to_string(prim)).c_str(), "primitive %u is not supported yet", prim);
    return;
  }
  if (!count) return;
  prim_draws[prim & 15]++;
  prim_verts[prim & 15] += count;
  uint64_t vkey = 0, fkey = 0;
  const auto th0 = Clock::now();
  const uint64_t compiles_before = cnt.compiles, pipes_before = cnt.pipelines;
  Module &vs = get_vs(st, vkey);
  const uint8_t *fcode = nullptr;
  Module &fp = get_fp(st, fkey, fcode);
  if (!vs.ok || !fp.ok) { cnt.skipped_shader++; return; }

  // Pipeline (cached by program identity + raster state).
  RasterState rs = make_raster(st);
  std::array<uint32_t, 10> pk{uint32_t(vkey), uint32_t(vkey >> 32), uint32_t(fkey), uint32_t(fkey >> 32),
      rs.cull | (rs.front_ccw ? 8u : 0u) | (rs.color_mask << 4) | (rs.depth_compare << 8),
      uint32_t(rs.src_color) | (uint32_t(rs.dst_color) << 4) | (uint32_t(rs.src_alpha) << 8) | (uint32_t(rs.dst_alpha) << 12),
      uint32_t(rs.color_op) | (uint32_t(rs.alpha_op) << 4),
      uint32_t(rs.stencil_enable) | (rs.stencil_front << 1) | (rs.stencil_back << 13),
      rs.stencil_read_mask | (rs.stencil_write_mask << 8) | (rs.stencil_ref << 16), 0};
  uint32_t program;
  auto pit = pipelines.find(pk);
  if (pit != pipelines.end()) program = pit->second;
  else {
    try {
      program = r.acquireRsxPipeline(vs.words, fp.words, rs);
    } catch (const std::exception &e) {
      cnt.compile_failures++;
      log_once(("pipe" + std::to_string(pk[0]) + std::to_string(pk[2])).c_str(), "pipeline creation failed: %.300s", e.what());
      cnt.skipped_shader++;
      return;
    }
    pipelines[pk] = program;
    cnt.pipelines++;
  }
  {
    const double setup = ms(th0, Clock::now());
    if (setup > 4.0) {
      static int nh = 0;
      if (nh < 40) {
        nh++;
        std::fprintf(stderr, "[vulkan][hitch] shader/pipeline setup took %.1f ms (new shader compiles=%llu, new pipelines=%llu)\n", setup,
                     (unsigned long long)(cnt.compiles - compiles_before), (unsigned long long)(cnt.pipelines - pipes_before));
      }
    }
  }

  // Vertices of this batch, stored once in the frame's vertex array; the draw addresses them through `indices`, so a
  // quad is 4 vertices instead of 6 and nothing is copied twice. A batch that continues a triangle stream (carried
  // vertices) or ends mid-primitive goes through a temporary array. Everything is rolled back on an early return.
  struct Rollback {
    size_t vbase = verts.size(), ibase = indices.size();
    bool commit = false;
    ~Rollback() { if (!commit) { verts.resize(vbase); indices.resize(ibase); } }
  } rollback;
  const size_t vbase = rollback.vbase;
  const bool whole = stream.carry.empty() && (prim == RSX_PRIMITIVE_QUADS ? count % 4 == 0 : tri_list ? count % 3 == 0 : true);
  const auto tf0 = Clock::now();
  const VertexTemplate vt = make_template(st);
  size_t n_src = 0;      // vertices of this batch now in verts[vbase..]
  size_t used = 0;       // tri_list: vertices that form whole triangles
  // An indexed draw usually reuses vertices: fetch each distinct index once and keep the index list. Needs the index range
  // to fit the lookup table (16-bit index arrays always do).
  static std::vector<uint32_t> d_idx, d_local, d_uniq, d_slot(65536), d_gen(65536);
  static uint32_t d_cur = 0;
  bool dedup = false;
  if (whole) {
    if (indexed && count >= 3) {
      d_idx.resize(count);
      uint32_t mn = UINT32_MAX, mx = 0;
      for (u32 k = 0; k < count; k++) {
        const uint32_t v = read_index(st, first + k);
        d_idx[k] = v;
        mn = std::min(mn, v);
        mx = std::max(mx, v);
      }
      if (mx - mn < 65536u) {
        dedup = true;
        d_local.resize(count);
        d_uniq.clear();
        if (++d_cur == 0) { std::fill(d_gen.begin(), d_gen.end(), 0u); d_cur = 1; }
        for (u32 k = 0; k < count; k++) {
          const uint32_t off = d_idx[k] - mn;
          if (d_gen[off] != d_cur) {
            d_gen[off] = d_cur;
            d_slot[off] = uint32_t(d_uniq.size());
            d_uniq.push_back(d_idx[k]);
          }
          d_local[k] = d_slot[off];
        }
      }
    }
    const size_t nv = dedup ? d_uniq.size() : size_t(count);
    if (verts.size() + nv > kMaxVerts) { cnt.skipped_cap++; return; }
    verts.insert(verts.end(), nv, vt.base);   // one 256-byte write per vertex instead of zero-fill + copy
    if (dedup) {
      for (size_t u = 0; u < nv; u++) fetch_vertex(st, vt, d_uniq[u], verts[vbase + u]);
      idx_deduped++;
      idx_entries += count;
      idx_unique += nv;
    } else {
      for (u32 k = 0; k < count; k++)
        fetch_vertex(st, vt, indexed ? read_index(st, first + k) : first + k, verts[vbase + k]);
    }
    if (indexed) idx_draws++;
    n_src = used = count;
    if (tri_list) stream.carry.clear();
  } else {
    std::vector<RsxVertex> src;
    src.reserve(stream.carry.size() + count);
    src.insert(src.end(), stream.carry.begin(), stream.carry.end());
    const size_t carried = src.size();
    src.insert(src.end(), count, vt.base);
    for (u32 k = 0; k < count; k++)
      fetch_vertex(st, vt, indexed ? read_index(st, first + k) : first + k, src[carried + k]);
    n_src = prim == RSX_PRIMITIVE_QUADS ? src.size() / 4 * 4 : tri_list ? src.size() / 3 * 3 : src.size();
    used = n_src;
    if (tri_list) stream.carry.assign(src.begin() + used, src.end());
    if (verts.size() + n_src > kMaxVerts) { cnt.skipped_cap++; if (tri_list) { stream.valid = true; stream.prim = prim; stream.epoch = st->begin_epoch; stream.end = first + count; } return; }
    verts.insert(verts.end(), src.begin(), src.begin() + n_src);
  }
  t_fetch += ms(tf0, Clock::now());
  if (tri_list) {
    stream.valid = true;
    stream.prim = prim;
    stream.epoch = st->begin_epoch;
    stream.end = first + count;
  }
  indices.reserve(indices.size() + n_src * 3 / 2 + 6);
  const uint32_t vb = uint32_t(vbase);
  auto at = [&](size_t k) -> uint32_t { return vb + (dedup ? d_local[k] : uint32_t(k)); };
  if (prim == RSX_PRIMITIVE_QUADS) {
    for (size_t q = 0; q + 3 < n_src; q += 4) {
      static const int idx[6] = {0, 1, 2, 0, 2, 3};
      for (int k : idx) indices.push_back(at(q + k));
    }
  } else if (tri_list) {
    for (size_t k = 0; k < used; k++) indices.push_back(at(k));
  } else {
    const bool fan = prim == RSX_PRIMITIVE_TRIANGLE_FAN;
    for (size_t t = 0; t + 2 < n_src; t++) {
      uint32_t i0 = fan ? 0 : uint32_t(t), i1 = uint32_t(t + 1), i2 = uint32_t(t + 2);
      if (!fan && (t & 1)) std::swap(i1, i2);
      indices.push_back(at(i0)); indices.push_back(at(i1)); indices.push_back(at(i2));
    }
  }
  const size_t added = indices.size() - rollback.ibase;
  if (!added) return;
  if (indices.size() > kMaxIndices) { cnt.skipped_cap++; return; }

  // Constants (same layout as the D3D12 backend).
  const auto tc0 = Clock::now();
  // Fast path: identical vertex constants (and viewport z) as the previous draw of this frame
  // reuse its block without rebuilding, validating or hashing 8 KiB.
  static std::vector<uint8_t> last_vraw;
  static float last_vz[2] = {0.f, 0.f};
  static uint32_t last_vblock = UINT32_MAX;
  static uint64_t last_vframe = ~0ull;
  const size_t vraw_bytes = size_t(RSX_MAX_VERTEX_CONSTANTS) * 16;
  const bool vs_same = last_vframe == const_epoch && last_vblock != UINT32_MAX && last_vraw.size() == vraw_bytes &&
                       last_vz[0] == st->viewport_scale[2] && last_vz[1] == st->viewport_offset[2] &&
                       !std::memcmp(last_vraw.data(), st->vertex_constants, vraw_bytes);
  std::array<std::array<float, 4>, 514> vc{};
  if (!vs_same) std::memcpy(vc.data(), st->vertex_constants, RSX_MAX_VERTEX_CONSTANTS * 16);
  vc[512] = {1.f, 1.f, 1.f, 1.f};
  vc[513] = {0.f, 0.f, 0.f, 0.f};
  if (st->viewport_scale[2] != 0.f) { vc[512][2] = st->viewport_scale[2]; vc[513][2] = st->viewport_offset[2]; }
  std::array<std::array<float, 4>, 5 + 64> fc{};
  std::array<uint32_t, 16> tex{};
  const auto tt0 = Clock::now();
  for (uint32_t u = 0; u < 4; u++) {
    const rsx_texture_state &t = st->textures[u];
    tex[u] = texture_for(r, u, t);
    const uint32_t w = (t.image_rect >> 16) & 0xFFFFu, h = t.image_rect & 0xFFFFu;
    float sx = 1.f, sy = 1.f;
    if (t.offset && w && h && (((t.format >> 8) & 0xFFu) & 0x40u)) { sx = 1.f / w; sy = 1.f / h; }
    fc[u] = {sx, sy, 0.f, 0.f};
  }
  fc[4] = {st->alpha_test_enable ? 1.f : 0.f, float(st->alpha_ref & 0xFFu) / 255.f,
           float(st->alpha_func & 0x07u), 0.f};
  t_tex += ms(tt0, Clock::now());
  if (fcode) d2_fp_extract_consts(fcode, 4096, &fc[5][0], 64);
  Rec rec{};
  try {
    if (vs_same) rec.vs_block = last_vblock;
    else {
      rec.vs_block = r.frameVsConstants(vc);
      last_vraw.assign(reinterpret_cast<const uint8_t *>(st->vertex_constants), reinterpret_cast<const uint8_t *>(st->vertex_constants) + vraw_bytes);
      last_vz[0] = st->viewport_scale[2]; last_vz[1] = st->viewport_offset[2];
      last_vblock = rec.vs_block; last_vframe = const_epoch;
    }
    rec.fp_block = r.frameFpConstants(fc);
  } catch (const std::exception &e) {
    log_once("blocks", "constant block failed: %.200s", e.what());
    cnt.skipped_other++;
    return;
  }
  t_consts += ms(tc0, Clock::now());
  rec.textures = tex;
  rec.program = program;
  rec.first = uint32_t(rollback.ibase);
  rec.count = uint32_t(added);
  rec.blend = st->blend_enable != 0;
  rec.depth = !st->depth_test_enable ? DepthMode::disabled : st->depth_mask ? DepthMode::read_write : DepthMode::read_only;
  // Guest viewport / scissor, scaled to the output later (kept in guest units).
  rec.viewport = {st->viewport_x, st->viewport_y, st->viewport_w, st->viewport_h};
  rec.scissor = {st->scissor_x, st->scissor_y, st->scissor_w, st->scissor_h};
  auto same_rect = [](const Rect &a, const Rect &b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
  };
  // Contiguous draws with identical state are one draw call (a batched stream
  // is thousands of 256-vertex records otherwise, over the renderer's limit).
  if (!recs.empty()) {
    Rec &b = recs.back();
    if (b.first + b.count == rec.first && b.program == rec.program && b.vs_block == rec.vs_block &&
        b.fp_block == rec.fp_block && b.textures == rec.textures && b.depth == rec.depth &&
        b.blend == rec.blend && same_rect(b.viewport, rec.viewport) && same_rect(b.scissor, rec.scissor)) {
      b.count += rec.count;
      cnt.recorded++;
      rollback.commit = true;
      return;
    }
  }
  if (recs.size() >= 4000) { cnt.skipped_cap++; return; }
  recs.push_back(rec);
  cnt.recorded++;
  rollback.commit = true;
}

// Present: submit everything recorded this frame.
// Very long sessions: the GPU has a hard allocation limit, so under pressure (many cached textures)
// free the ones that have not been used for a while. Normal play never gets near it, and idle textures
// are deliberately NOT evicted by age alone: re-created textures showed wrong in the hall after a battle.
uint64_t tex_evicted = 0;
void evict_textures(Renderer &r) {
  static const size_t pressure = [] { const char *e = std::getenv("D2_VULKAN_TEX_PRESSURE"); return size_t(e && *e ? std::atoi(e) : 1500); }();
  if (frame_no % 60 != 0 || tex_cache.size() <= pressure) return;
  size_t freed = 0;
  for (auto it = tex_cache.begin(); it != tex_cache.end();) {
    const TexEntry &e = it->second;
    if (e.used_frame == ~0ull || frame_no - e.used_frame > 600) {
      if (e.handle) {
        try { r.destroyTexture(e.handle); } catch (const std::exception &) {}
        tex_evicted++;
        freed++;
      }
      it = tex_cache.erase(it);
    } else ++it;
  }
  std::fprintf(stderr, "[vulkan][m2] texture cache pressure: freed %zu of the idle textures, %zu remain\n", freed, tex_cache.size());
}
#include "ui_overlay.inc"
bool submit(Renderer &r, uint32_t w, uint32_t h, const std::array<float, 4> &clear,
            uint32_t guest_w, uint32_t guest_h) {
  DrawParameters params;
  params.clear_color = clear;
  params.viewport = {0, 0, w, h};
  params.scissor = {0, 0, w, h};
  r.setParameters(params);
  std::vector<DrawCommand> cmds;
  cmds.reserve(recs.size());
  // Keep the game's aspect ratio: scale uniformly and centre it (black bars on ultrawide / odd window sizes).
  const float fit = (guest_w && guest_h) ? std::min(float(w) / guest_w, float(h) / guest_h) : 1.f;
  const float sx = fit, sy = fit;
  const float ox = (guest_w ? (float(w) - guest_w * fit) * 0.5f : 0.f);
  const float oy = (guest_h ? (float(h) - guest_h * fit) * 0.5f : 0.f);
  const Rect whole{uint32_t(std::max(0.f, ox) + .5f), uint32_t(std::max(0.f, oy) + .5f),
                   std::max(1u, std::min(w, uint32_t(guest_w * fit + .5f))), std::max(1u, std::min(h, uint32_t(guest_h * fit + .5f)))};
  auto scale = [&](Rect g) {
    if (!guest_w || !guest_h) return Rect{0, 0, w, h};
    if (g.width < 1 || g.height < 1 || g.x + g.width > guest_w + 1 || g.y + g.height > guest_h + 1)
      return whole;
    Rect o{uint32_t(g.x * sx + ox), uint32_t(g.y * sy + oy), std::max(1u, uint32_t(g.width * sx + .5f)),
           std::max(1u, uint32_t(g.height * sy + .5f))};
    if (o.x >= w) o.x = w - 1;
    if (o.y >= h) o.y = h - 1;
    o.width = std::min(o.width, w - o.x);
    o.height = std::min(o.height, h - o.y);
    return o;
  };
  for (const Rec &c : recs) {
    DrawCommand d;
    d.first = c.first;
    d.count = c.count;
    d.indexed = true;
    d.depth = c.depth;
    d.blend = c.blend;
    d.viewport = scale(c.viewport);
    d.scissor = d.viewport;
    d.rsx_program = c.program;
    d.textures = c.textures;
    d.vs_block = c.vs_block;
    d.fp_block = c.fp_block;
    cmds.push_back(d);
  }
  ui_flush(r, w, h, cmds); // options menu / FPS counter on top of the game
  const auto g0 = Clock::now();
  if (!verts.empty()) r.setRsxGeometry(verts, indices);
  const auto g1 = Clock::now();
  const bool ok = r.draw(cmds);
  const_epoch++;
  const auto g2 = Clock::now();
  t_geom += ms(g0, g1);
  t_draw += ms(g1, g2);
  {
    static Clock::time_point last{};
    static bool have_last = false;
    static uint64_t last_uploads = 0, hist[5] = {}, slow_logged = 0, nframes = 0;
    static double period_max = 0, guest_sum = 0, thr_sum = 0, proc_sum = 0;
#ifdef __ANDROID__
    static double last_thr = 0, last_proc = 0;
    const double thr_now = cpu_ms(CLOCK_THREAD_CPUTIME_ID), proc_now = cpu_ms(CLOCK_PROCESS_CPUTIME_ID);
    const double thr_frame = last_thr ? thr_now - last_thr : 0.0, proc_frame = last_proc ? proc_now - last_proc : 0.0;
    last_thr = thr_now;
    last_proc = proc_now;
#else
    const double thr_frame = 0.0, proc_frame = 0.0;
#endif
    const auto now = Clock::now();
    if (have_last) {
      const double period = ms(last, now);
      const double work = f_record + ms(g0, g2);
      nframes++;
      guest_sum += std::max(0.0, period - work);
      thr_sum += thr_frame;
      proc_sum += proc_frame;
      period_max = std::max(period_max, period);
      hist[period <= 17.5 ? 0 : period <= 25 ? 1 : period <= 34 ? 2 : period <= 50 ? 3 : 4]++;
      if (period > 34.0 && slow_logged < 80 && nframes > 300) {
        slow_logged++;
        std::fprintf(stderr,
            "[vulkan][slow] frame#%llu period=%.1f ms: record=%.1f (texture check/upload=%.1f, uploads=%llu) geometry=%.1f draw+present=%.1f guest/other=%.1f | draws=%zu verts=%zu idx=%zu | cpu: render-thread=%.1f process=%.1f ms\n",
            (unsigned long long)frame_no, period, f_record, f_texup, (unsigned long long)(cnt.tex_uploads - last_uploads),
            ms(g0, g1), ms(g1, g2), std::max(0.0, period - work), recs.size(), verts.size(), indices.size(), thr_frame, proc_frame);
      }
      if (nframes % 600 == 0)
        std::fprintf(stderr,
            "[vulkan][pace] last 600 frames: <=17ms=%llu 17-25=%llu 25-34=%llu 34-50=%llu >50=%llu | max=%.1f ms | avg guest/other=%.1f ms | avg cpu per frame: render-thread=%.1f, process=%.1f ms\n",
            (unsigned long long)hist[0], (unsigned long long)hist[1], (unsigned long long)hist[2], (unsigned long long)hist[3],
            (unsigned long long)hist[4], period_max, guest_sum / 600.0, thr_sum / 600.0, proc_sum / 600.0);
      if (nframes % 600 == 0) { print_mix(); for (auto &hh : hist) hh = 0; period_max = 0; guest_sum = 0; thr_sum = 0; proc_sum = 0; }
    }
    last = Clock::now();
    have_last = true;
    last_uploads = cnt.tex_uploads;
    f_record = 0; f_texup = 0;
  }
  if (ok) { verts_total += verts.size(); frames_timed++; recs.clear(); verts.clear(); indices.clear(); frame_no++; stream.valid = false; stream.carry.clear(); evict_textures(r); }
  return ok;
}

// Install the default program (required before the render targets exist) and
// start the shader compiler. Throws when DXC is unavailable.
void init(Renderer &r) {
  compiler = std::make_unique<shader::Compiler>();
  const auto vs = compile_cached(shader::fixtures::vertex(), shader::Stage::vertex);
  const auto fs = compile_cached(shader::fixtures::fragment(false), shader::Stage::fragment);
  r.setRsxProgram(vs, fs);
  init_post(r);
  ready = true;
}

void drop_frame() { recs.clear(); verts.clear(); indices.clear(); const_epoch++; }

void print_counters() {
  if (frames_timed)
    std::fprintf(stderr,
                 "[vulkan][m2] per frame (avg of %llu): record %.2f ms (vertex fetch %.2f, constants+textures %.2f of which textures %.2f), geometry upload %.2f ms, draw+present %.2f ms, vertices %.0f\n",
                 (unsigned long long)frames_timed, t_record / frames_timed, t_fetch / frames_timed,
                 t_consts / frames_timed, t_tex / frames_timed, t_geom / frames_timed,
                 t_draw / frames_timed, double(verts_total) / frames_timed);
  t_record = t_geom = t_draw = t_fetch = t_consts = t_tex = 0; verts_total = 0; frames_timed = 0;
  print_mix();
  std::fprintf(stderr,
               "[vulkan][m2] recorded=%llu skipped: target=%llu prim=%llu shader=%llu other=%llu cap=%llu | "
               "compiles=%llu (disk cache hits %llu) failures=%llu pipelines=%llu | textures: uploads=%llu unsupported=%llu live=%zu evicted=%llu\n",
               (unsigned long long)cnt.recorded, (unsigned long long)cnt.skipped_target,
               (unsigned long long)cnt.skipped_prim, (unsigned long long)cnt.skipped_shader,
               (unsigned long long)cnt.skipped_other, (unsigned long long)cnt.skipped_cap,
               (unsigned long long)cnt.compiles, (unsigned long long)cnt.cache_hits, (unsigned long long)cnt.compile_failures,
               (unsigned long long)cnt.pipelines, (unsigned long long)cnt.tex_uploads,
               (unsigned long long)cnt.tex_unsupported, tex_cache.size(), (unsigned long long)tex_evicted);
}
} // namespace tr
