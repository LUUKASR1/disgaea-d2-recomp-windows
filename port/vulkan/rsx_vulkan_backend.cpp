// Disgaea D2 - experimental Vulkan RSX backend, milestone M1.
//
// Scope (see docs/VULKAN-BACKEND-PLAN.md): Win32 window, Vulkan presentation,
// guest clear colour and frame pacing through the same entry points the
// D3D12 backend exports. Guest draws, shaders and textures are NOT translated
// yet: every draw callback is counted and ignored, so the screen only shows
// the guest clear colour. This backend is opt-in (D2_GRAPHICS_API=vulkan and
// D2_VULKAN_EXPERIMENTAL=1); D3D12 stays the default and is never replaced
// silently.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <vector>
#include <span>
#include <stdexcept>
#include <string_view>

#include "rsx_commands.h"
#ifdef D2_VULKAN_GAME_BACKEND
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <string>
#include "compiler.h"
#include "fixtures.h"
#include "rsx_fp_decompiler.h"
#include "rsx_texture_layout.h"
#include "rsx_vertex_fetch.h"
#include "rsx_vp_decompiler.h"
extern "C" uint8_t *vm_base;
extern "C" u32 cellGcmResolveLocated(int local, u32 offset);
extern "C" int cellGcmOffsetIsDisplay(u32 offset);
// rsx_fp_decompiler.h declares these two after closing its extern "C" block,
// so a C++ include gets C++ linkage for functions the SDK defines in C.
extern "C" int d2_fp_extract_consts(const u8 *, u32, float *, int) __asm__("rsx_fp_extract_consts");
extern "C" u32 d2_fp_code_hash(const u8 *, u32) __asm__("rsx_fp_code_hash");

#endif
#include "vulkan_renderer.h"

namespace {

struct State {
  HWND hwnd = nullptr;
  std::unique_ptr<d2::vulkan::Renderer> renderer;
  rsx_backend backend{};
  std::array<float, 4> clear{0.f, 0.f, 0.f, 1.f};
  bool closed = false;
  bool resized = false;
  bool vsync = true;
  bool fullscreen = false;
  bool initialized = false;
  uint64_t frames = 0, ignored_draws = 0, ignored_textures = 0;
  bool logged_draw = false;
} g;

// ---- player settings (saved between runs, like a PC game) ----------------
// Stored in %LOCALAPPDATA%\\DisgaeaD2Recomp\\settings.ini (or D2_SETTINGS_DIR). Environment variables set by the
// launcher (D2_VSYNC, D2_OUTPUT_WIDTH/HEIGHT) still win over the file.
struct Settings {
  bool fullscreen = false; // borderless fullscreen on the current monitor
  bool vsync = true;
  uint32_t width = 1280, height = 720; // windowed client size
};
Settings g_settings;

std::string settings_file() {
  std::string dir;
  if (const char *e = std::getenv("D2_SETTINGS_DIR"); e && *e)
    dir = e;
  else if (const char *l = std::getenv("LOCALAPPDATA"); l && *l)
    dir = std::string(l) + "\\DisgaeaD2Recomp";
  else
    return {};
  CreateDirectoryA(dir.c_str(), nullptr);
  return dir + "\\settings.ini";
}

void load_settings() {
  const std::string path = settings_file();
  if (!path.empty()) {
    if (std::FILE *f = std::fopen(path.c_str(), "rb")) {
      char line[256];
      while (std::fgets(line, sizeof line, f)) {
        char key[64] = {};
        unsigned value = 0;
        if (std::sscanf(line, "%63[^=]=%u", key, &value) != 2)
          continue;
        const std::string k = key;
        if (k == "fullscreen") g_settings.fullscreen = value != 0;
        else if (k == "vsync") g_settings.vsync = value != 0;
        else if (k == "width" && value >= 640 && value <= 16384) g_settings.width = value;
        else if (k == "height" && value >= 360 && value <= 16384) g_settings.height = value;
      }
      std::fclose(f);
    }
  }
  const auto env_u = [](const char *name) -> unsigned { const char *e = std::getenv(name); return e && *e ? unsigned(std::atoi(e)) : 0u; };
  if (const char *vs = std::getenv("D2_VSYNC"); vs && *vs) g_settings.vsync = vs[0] != '0';
  if (env_u("D2_OUTPUT_WIDTH") >= 640 && env_u("D2_OUTPUT_HEIGHT") >= 360) {
    g_settings.width = env_u("D2_OUTPUT_WIDTH");
    g_settings.height = env_u("D2_OUTPUT_HEIGHT");
  }
}

void save_settings() {
  const std::string path = settings_file();
  if (path.empty()) return;
  if (std::FILE *f = std::fopen(path.c_str(), "wb")) {
    std::fprintf(f, "fullscreen=%u\nvsync=%u\nwidth=%u\nheight=%u\n", g_settings.fullscreen ? 1u : 0u,
                 g_settings.vsync ? 1u : 0u, g_settings.width, g_settings.height);
    std::fclose(f);
  }
}

// Borderless fullscreen: no mode switch, fast alt-tab, and the window keeps its normal size when restored.
void set_fullscreen(bool on) {
  static WINDOWPLACEMENT previous{sizeof(WINDOWPLACEMENT)};
  static LONG_PTR previous_style = 0;
  if (!g.hwnd || on == g.fullscreen)
    return;
  if (on) {
    MONITORINFO mi{sizeof(mi)};
    if (!GetWindowPlacement(g.hwnd, &previous) ||
        !GetMonitorInfoA(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST), &mi))
      return;
    previous_style = GetWindowLongPtrA(g.hwnd, GWL_STYLE);
    SetWindowLongPtrA(g.hwnd, GWL_STYLE, previous_style & ~LONG_PTR(WS_OVERLAPPEDWINDOW));
    SetWindowPos(g.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  } else {
    SetWindowLongPtrA(g.hwnd, GWL_STYLE, previous_style ? previous_style : LONG_PTR(WS_OVERLAPPEDWINDOW | WS_VISIBLE));
    SetWindowPlacement(g.hwnd, &previous);
    SetWindowPos(g.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  }
  g.fullscreen = on;
  g_settings.fullscreen = on;
  g.resized = true;
  save_settings();
}

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_CLOSE:
    g.closed = true;
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  case WM_SIZE:
    g.resized = true;
    return 0;
  case WM_SETCURSOR:
    if (g.fullscreen && LOWORD(lp) == HTCLIENT) {
      SetCursor(nullptr); // no mouse pointer over a fullscreen game
      return TRUE;
    }
    break;
  case WM_KEYDOWN:
    // Esc no longer quits: it belongs to the game. Close with Alt+F4 or the window button.
    if (wp == VK_F11)
      set_fullscreen(!g.fullscreen);
    return 0;
  case WM_SYSKEYDOWN:
    if (wp == VK_RETURN && (lp & (1 << 29))) { // Alt+Enter
      set_fullscreen(!g.fullscreen);
      return 0;
    }
    break;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}

HWND create_window(uint32_t width, uint32_t height, const char *title) {
  WNDCLASSEXA wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wndproc;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
  wc.lpszClassName = "ps3recomp_vulkan";
  RegisterClassExA(&wc);
  RECT wr{0, 0, (LONG)width, (LONG)height};
  AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
  SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED |
                          ES_SYSTEM_REQUIRED);
  return CreateWindowExA(0, wc.lpszClassName,
                         title ? title : "ps3recomp (Vulkan, experimental)",
                         WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                         CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
                         nullptr, nullptr, wc.hInstance, nullptr);
}

// ---- draw statistics (M2 groundwork) --------------------------------------
// Counts what the game asks for so the translation can be scoped from real data.
// Nothing here draws. Disable with D2_VULKAN_STATS=0.
struct Stats {
  uint64_t draws = 0, indexed = 0, with_vp = 0, blend = 0, depth = 0,
           stencil = 0, cull = 0, alpha = 0, no_state = 0;
  std::array<uint64_t, 16> prim{};
  std::map<uint32_t, uint64_t> texfmt;  // (format byte) | cube<<8
  std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint64_t> targets; // w,h,colour fmt
  std::map<std::pair<uint32_t, uint64_t>, uint64_t> shaders; // fp, vp hash
  std::set<uint64_t> vps;
  std::set<uint32_t> fps;
  uint64_t frame_draws = 0, max_frame_draws = 0, sampled_frames = 0;
  uint32_t texunits_used[5]{};  // histogram of bound texture units per draw
  bool enabled = true;
} st;
const rsx_state *g_state = nullptr;

uint64_t fnv1a(const uint8_t *p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}


#ifdef D2_VULKAN_GAME_BACKEND
// ---- shader/state dump (M2 groundwork, game build only) -------------------
// With D2_VULKAN_DUMP_DIR set, the first time a (fragment program, vertex
// program) pair is drawn, write the SDK-decompiled HLSL, the raw microcode and
// the full draw state next to each other so the translation can be designed
// and tested against the real shaders. Nothing is drawn or modified.
std::string dump_dir;
unsigned dump_count = 0;

void write_file(const std::string &name, const void *data, size_t n) {
  std::FILE *f = std::fopen((dump_dir + "/" + name).c_str(), "wb");
  if (!f) return;
  std::fwrite(data, 1, n, f);
  std::fclose(f);
}

void dump_pair(const rsx_state *r, uint64_t vh) {
  if (dump_dir.empty() || dump_count >= 24) return;
  const unsigned id = dump_count++;
  char name[96];
  static char hlsl[64 * 1024];
  // Vertex program.
  std::snprintf(name, sizeof name, "vp_%08llX", (unsigned long long)(vh & 0xFFFFFFFFull));
  const std::string vpn = name;
  if (r->vp_ucode_bytes >= 16) {
    write_file(vpn + ".bin", r->vp_ucode, r->vp_ucode_bytes);
    const int ni = rsx_vp_decompile(r->vp_ucode, r->vp_ucode_bytes, hlsl, sizeof hlsl);
    std::fprintf(stderr, "[vulkan][dump] %s: VP decompile -> %d instructions\n", vpn.c_str(), ni);
    if (ni > 0) write_file(vpn + ".hlsl", hlsl, std::strlen(hlsl));
  }
  // Fragment program (guest memory).
  std::snprintf(name, sizeof name, "fp_%06X", r->shader_program & 0xFFFFFFu);
  const std::string fpn = name;
  const u32 fa = r->shader_program;
  u32 off = cellGcmResolveLocated((fa & 0x3u) == 1, fa & ~0x3u);
  std::string fp_note = "unresolved";
  if (vm_base && off != 0xFFFFFFFFu) {
    u32 nconst = 0;
    const u8 *code = vm_base + off;
    write_file(fpn + ".bin", code, 512);
    const int n = rsx_fp_decompile_buffered_ex(code, 4096, RSX_FP_CTRL_AUTO, 0u, hlsl, sizeof hlsl, &nconst);
    fp_note = "decompile=" + std::to_string(n) + " constants=" + std::to_string(nconst);
    if (n > 0) write_file(fpn + ".hlsl", hlsl, std::strlen(hlsl));
  }
  std::fprintf(stderr, "[vulkan][dump] %s: FP %s\n", fpn.c_str(), fp_note.c_str());
  // State description.
  std::string t;
  char b[512];
  auto add = [&](const char *fmt, auto... a) { std::snprintf(b, sizeof b, fmt, a...); t += b; };
  add("draw #%u  vp=%s fp=%s (shader_program=0x%X off=0x%X)\n", id, vpn.c_str(), fpn.c_str(), fa, off);
  add("primitive=%u shader_control=0x%X vertex_attrib_output_mask=0x%X vp_bytes=%u\n", r->primitive_type, r->shader_control, r->vertex_attrib_output_mask, r->vp_ucode_bytes);
  add("surface: format=0x%X clip=%u,%u %ux%u color_target=%u color_off[0]=0x%X pitch[0]=%u zeta_off=0x%X\n", r->surface_format, r->surface_clip_x, r->surface_clip_y, r->surface_clip_w, r->surface_clip_h, r->color_target, r->surface_color_offset[0], r->surface_color_pitch[0], r->surface_zeta_offset);
  add("viewport: %u,%u %ux%u  offset=(%g %g %g %g) scale=(%g %g %g %g) clip=[%g,%g]\n", r->viewport_x, r->viewport_y, r->viewport_w, r->viewport_h, r->viewport_offset[0], r->viewport_offset[1], r->viewport_offset[2], r->viewport_offset[3], r->viewport_scale[0], r->viewport_scale[1], r->viewport_scale[2], r->viewport_scale[3], r->clip_min, r->clip_max);
  add("scissor: %u,%u %ux%u\n", r->scissor_x, r->scissor_y, r->scissor_w, r->scissor_h);
  add("blend: enable=%d sfactor=0x%X dfactor=0x%X equation=0x%X color=0x%X\n", r->blend_enable, r->blend_sfactor, r->blend_dfactor, r->blend_equation, r->blend_color);
  add("depth: test=%d func=0x%X mask=%d  stencil=%d  cull=%d face=0x%X front=0x%X colormask=0x%X\n", r->depth_test_enable, r->depth_func, r->depth_mask, r->stencil_test_enable, r->cull_face_enable, r->cull_face, r->front_face, r->color_mask);
  add("alpha test: enable=%d func=0x%X ref=0x%X\n", r->alpha_test_enable, r->alpha_func, r->alpha_ref);
  for (int u = 0; u < 4; u++) {
    const rsx_texture_state &x = r->textures[u];
    add("tex%d: offset=0x%X format=0x%X address=0x%X control0=0x%X control1=0x%X filter=0x%X rect=0x%X (%ux%u) border=0x%X control3=0x%X (pitch %u)\n", u, x.offset, x.format, x.address, x.control0, x.control1, x.filter, x.image_rect, x.image_rect >> 16, x.image_rect & 0xFFFFu, x.border_color, x.control3, x.control3 & 0xFFFFFu);
  }
  for (int a = 0; a < RSX_MAX_VERTEX_ATTRIBS; a++) {
    const rsx_vertex_attrib &v = r->vertex_attribs[a];
    if (!v.enabled && !v.size) continue;
    add("attr%d: enabled=%d format=0x%X offset=0x%X stride=%u size=%u type=%u freq=%u const=(%g %g %g %g)\n", a, v.enabled, v.format, v.offset, v.stride, v.size, v.type, v.frequency, r->vertex_data4f[a][0], r->vertex_data4f[a][1], r->vertex_data4f[a][2], r->vertex_data4f[a][3]);
  }
  for (int c = 0; c < 12; c++)
    add("vc[%d]=(%g %g %g %g)\n", c, r->vertex_constants[c][0], r->vertex_constants[c][1], r->vertex_constants[c][2], r->vertex_constants[c][3]);
  add("vertex_constants_lo=%u hi=%u\n", r->vertex_constants_lo, r->vertex_constants_hi);
  std::snprintf(name, sizeof name, "draw_%02u.txt", id);
  write_file(name, t.data(), t.size());
}
#include "game_translate.inc"
#endif

void note_draw(bool indexed, u32 primitive) {
  g.ignored_draws++;
  if (!g.logged_draw) {
    g.logged_draw = true;
    std::fprintf(stderr,
                 "[vulkan] M1: guest draws are not translated yet; ignoring "
                 "draws (only the clear colour is shown)\n");
  }
  if (!st.enabled) return;
  st.draws++;
  st.frame_draws++;
  if (indexed) st.indexed++;
  st.prim[primitive & 15]++;
  const rsx_state *r = g_state;
  if (!r) { st.no_state++; return; }
  const bool vp = r->vp_ucode_bytes >= 16;
  if (vp) st.with_vp++;
  if (r->blend_enable) st.blend++;
  if (r->depth_test_enable) st.depth++;
  if (r->stencil_test_enable) st.stencil++;
  if (r->cull_face_enable) st.cull++;
  if (r->alpha_test_enable) st.alpha++;
  const uint64_t vh = vp ? fnv1a(r->vp_ucode, std::min<u32>(r->vp_ucode_bytes, 4096)) : 0;
  st.vps.insert(vh);
  st.fps.insert(r->shader_program);
#ifdef D2_VULKAN_GAME_BACKEND
  if (!st.shaders.count({r->shader_program, vh})) dump_pair(r, vh);
#endif
  st.shaders[{r->shader_program, vh}]++;
  st.targets[{r->surface_clip_w, r->surface_clip_h, r->surface_format & 0x1Fu}]++;
  unsigned used = 0;
  for (int u = 0; u < 4; u++) {
    const rsx_texture_state &t = r->textures[u];
    if (!t.offset) continue;
    used++;
    st.texfmt[((t.format >> 8) & 0xFFu) | (((t.format >> 2) & 1u) << 8)]++;
  }
  st.texunits_used[used]++;
}

void print_stats(const char *when) {
  if (!st.enabled) return;
  std::fprintf(stderr, "[vulkan][stats] %s: frames=%llu draws=%llu (indexed %llu, with VP %llu, no state %llu) max/frame=%llu\n",
               when, (unsigned long long)g.frames, (unsigned long long)st.draws,
               (unsigned long long)st.indexed, (unsigned long long)st.with_vp,
               (unsigned long long)st.no_state, (unsigned long long)st.max_frame_draws);
  std::fprintf(stderr, "[vulkan][stats]   blend=%llu depth=%llu stencil=%llu cull=%llu alphatest=%llu\n",
               (unsigned long long)st.blend, (unsigned long long)st.depth,
               (unsigned long long)st.stencil, (unsigned long long)st.cull,
               (unsigned long long)st.alpha);
  std::fprintf(stderr, "[vulkan][stats]   primitives:");
  for (unsigned i = 0; i < 16; i++)
    if (st.prim[i]) std::fprintf(stderr, " %u=%llu", i, (unsigned long long)st.prim[i]);
  std::fprintf(stderr, "\n[vulkan][stats]   distinct vertex programs=%zu fragment programs=%zu (vp,fp) pairs=%zu\n",
               st.vps.size(), st.fps.size(), st.shaders.size());
  std::fprintf(stderr, "[vulkan][stats]   texture units bound per draw (0..4):");
  for (unsigned i = 0; i < 5; i++) std::fprintf(stderr, " %llu", (unsigned long long)st.texunits_used[i]);
  std::fprintf(stderr, "\n[vulkan][stats]   texture formats (fmt byte, +256 = cube):");
  for (auto &kv : st.texfmt) std::fprintf(stderr, " 0x%X=%llu", kv.first, (unsigned long long)kv.second);
  std::fprintf(stderr, "\n[vulkan][stats]   render targets (w x h, colour fmt):");
  for (auto &kv : st.targets)
    std::fprintf(stderr, " %ux%u/f%u=%llu", std::get<0>(kv.first), std::get<1>(kv.first),
                 std::get<2>(kv.first), (unsigned long long)kv.second);
  std::vector<std::pair<uint64_t, std::pair<uint32_t, uint64_t>>> top;
  for (auto &kv : st.shaders) top.push_back({kv.second, kv.first});
  std::sort(top.rbegin(), top.rend());
  std::fprintf(stderr, "\n[vulkan][stats]   top (fp addr, vp hash) pairs by draws:");
  for (size_t i = 0; i < top.size() && i < 12; i++)
    std::fprintf(stderr, " [fp=0x%X vp=%08llX n=%llu]", top[i].second.first,
                 (unsigned long long)(top[i].second.second & 0xFFFFFFFFull),
                 (unsigned long long)top[i].first);
  std::fprintf(stderr, "\n");
}

// ---- rsx_backend callbacks -------------------------------------------------

int cb_init(void *, u32, u32) { return 0; }
void cb_shutdown(void *) {}
void cb_begin_frame(void *) {}
void cb_end_frame(void *) {}
void cb_present(void *, u32) {}
void cb_set_state(void *, const rsx_state *state) { g_state = state; }

void cb_clear(void *, u32 flags, u32 color, float, u8) {
  // RSX CLEAR_SURFACE: bits 4..7 select colour channels. Same ARGB decoding
  // as the D3D12 backend.
  if (!(flags & 0xF0u))
    return;
  g.clear = {((color >> 16) & 0xFF) / 255.f, ((color >> 8) & 0xFF) / 255.f,
             (color & 0xFF) / 255.f, ((color >> 24) & 0xFF) / 255.f};
}

void translate(bool indexed, u32 prim, u32 first, u32 count) {
#ifdef D2_VULKAN_GAME_BACKEND
  if (!tr::ready || !g.renderer) return;
  try {
    tr::record(*g.renderer, g_state, indexed, prim, first, count);
  } catch (const std::exception &e) {
    tr::cnt.skipped_other++;
    tr::log_once("record", "draw skipped: %.300s", e.what());
  }
#else
  (void)indexed; (void)prim; (void)first; (void)count;
#endif
}
void cb_draw_arrays(void *, u32 prim, u32 first, u32 count) {
  note_draw(false, prim);
  translate(false, prim, first, count);
}
void cb_draw_indexed(void *, u32 prim, u32 first, u32 count) {
  note_draw(true, prim);
  translate(true, prim, first, count);
}
void cb_bind_texture(void *, u32, const rsx_texture_state *) {
  g.ignored_textures++;
}

bool client_size(uint32_t &w, uint32_t &h) {
  RECT r{};
  if (!GetClientRect(g.hwnd, &r) || r.right <= 0 || r.bottom <= 0)
    return false;
  w = (uint32_t)r.right;
  h = (uint32_t)r.bottom;
  return true;
}

} // namespace

extern "C" {

int rsx_vulkan_backend_init(uint32_t width, uint32_t height,
                            const char *title) {
  if (g.initialized)
    return 0;
  // Test hook for the explicit DirectX 12 fallback.
  if (const char *force = std::getenv("D2_VULKAN_FORCE_INIT_FAIL"); force && force[0] == '1') {
    std::fprintf(stderr, "[vulkan] init failure forced by D2_VULKAN_FORCE_INIT_FAIL\n");
    return -1;
  }
  const char *gate = std::getenv("D2_VULKAN_EXPERIMENTAL");
  if (!gate || std::string_view(gate) != "1") {
    std::fprintf(stderr, "[vulkan] experimental backend disabled; set "
                         "D2_VULKAN_EXPERIMENTAL=1 to opt in\n");
    return -1;
  }
#ifdef D2_VULKAN_GAME_BACKEND
  if (const char *dd = std::getenv("D2_VULKAN_DUMP_DIR")) {
    std::error_code ec;
    std::filesystem::create_directories(dd, ec);
    if (!ec) dump_dir = dd;
    std::fprintf(stderr, "[vulkan] dump dir: %s (%s)\n", dd, ec ? "FAILED" : "ok");
  }
#endif
  const char *sv = std::getenv("D2_VULKAN_STATS");
  // Per-draw statistics hash the vertex program and update several maps: off unless asked for.
#ifdef D2_VULKAN_GAME_BACKEND
  st.enabled = (sv && sv[0] == '1') || !dump_dir.empty();
#else
  st.enabled = !(sv && sv[0] == '0');
#endif
  load_settings();
  g.vsync = g_settings.vsync;
  try {
    g.hwnd = create_window(g_settings.width, g_settings.height, title);
    if (!g.hwnd) {
      std::fprintf(stderr, "[vulkan] window creation failed\n");
      return -1;
    }
    g.renderer = std::make_unique<d2::vulkan::Renderer>();
    g.renderer->start(g.hwnd, std::getenv("D2_VULKAN_VALIDATION") != nullptr);
#ifdef D2_VULKAN_GAME_BACKEND
    // M2: real guest draws. Needs the shader compiler (embedded DXC); a
    // failure here makes init fail so the caller can report it clearly.
    tr::init(*g.renderer);
#else
    // Degenerate triangle: keeps the presentation path valid with no visible
    // geometry until real draws are translated.
    const std::array<float, 4> zero{0.f, 0.f, 0.f, 1.f};
    const std::array<d2::vulkan::Vertex, 3> tri{
        d2::vulkan::Vertex{zero, zero, {}}, d2::vulkan::Vertex{zero, zero, {}},
        d2::vulkan::Vertex{zero, zero, {}}};
    g.renderer->setGeometry(tri);
#endif
    if (g_settings.fullscreen)
      set_fullscreen(true);
    uint32_t w = width, h = height;
    client_size(w, h);
    if (!g.renderer->resize(w, h, g.vsync))
      throw std::runtime_error("initial swapchain creation failed");
    const auto info = g.renderer->info();
    std::fprintf(stderr, "[vulkan] adapter: %s (api 0x%x)\n",
                 info.adapter.c_str(), info.api_version);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "[vulkan] init failed: %s\n", e.what());
    if (g.renderer)
      g.renderer->close();
    g.renderer.reset();
    if (g.hwnd) {
      DestroyWindow(g.hwnd);
      g.hwnd = nullptr;
    }
    return -1;
  }

  g.backend = {};
  g.backend.init = cb_init;
  g.backend.shutdown = cb_shutdown;
  g.backend.begin_frame = cb_begin_frame;
  g.backend.end_frame = cb_end_frame;
  g.backend.present = cb_present;
  g.backend.clear = cb_clear;
  g.backend.set_render_target = cb_set_state;
  g.backend.set_viewport = cb_set_state;
  g.backend.set_blend = cb_set_state;
  g.backend.set_depth_stencil = cb_set_state;
  g.backend.set_color_mask = cb_set_state;
  g.backend.set_alpha_test = cb_set_state;
  g.backend.set_shader = cb_set_state;
  g.backend.set_vertex_attribs = cb_set_state;
  g.backend.draw_arrays = cb_draw_arrays;
  g.backend.draw_indexed = cb_draw_indexed;
  g.backend.bind_texture = cb_bind_texture;
  rsx_set_backend(&g.backend);
  g.initialized = true;
  return 0;
}

void rsx_vulkan_backend_shutdown(void) {
  if (!g.initialized)
    return;
  print_stats("final");
#ifdef D2_VULKAN_GAME_BACKEND
  tr::print_counters();
#endif
  rsx_set_backend(nullptr);
  if (g.renderer)
    g.renderer->close();
  g.renderer.reset();
  if (g.hwnd && IsWindow(g.hwnd))
    DestroyWindow(g.hwnd);
  g.hwnd = nullptr;
  g.initialized = false;
  std::fprintf(stderr,
               "[vulkan] shutdown: %llu frames, %llu draws and %llu texture "
               "binds ignored (M1)\n",
               (unsigned long long)g.frames,
               (unsigned long long)g.ignored_draws,
               (unsigned long long)g.ignored_textures);
}

int rsx_vulkan_backend_pump_messages(void) {
  MSG msg;
  while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT)
      g.closed = true;
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
  return g.closed ? -1 : 0;
}

void rsx_vulkan_backend_present(void) {
  if (!g.initialized || g.closed || !g.renderer || !g.hwnd)
    return;
  try {
    uint32_t w = 0, h = 0;
    if (!client_size(w, h))
      return; // minimized: skip presentation, keep the guest running
    if (g.resized || g.renderer->needsResize()) {
      if (!g.renderer->resize(w, h, g.vsync))
        return;
      g.resized = false;
    }
    bool presented = false;
#ifdef D2_VULKAN_GAME_BACKEND
    presented = tr::submit(*g.renderer, w, h, g.clear, tr::guest_w, tr::guest_h);
#else
    d2::vulkan::DrawParameters params;
    params.clear_color = g.clear;
    params.viewport = {0, 0, w, h};
    params.scissor = {0, 0, w, h};
    g.renderer->setParameters(params);
    d2::vulkan::DrawCommand cmd;
    cmd.first = 0;
    cmd.count = 3;
    cmd.viewport = params.viewport;
    cmd.scissor = params.scissor;
    cmd.blend = false;
    presented = g.renderer->draw(std::span<const d2::vulkan::DrawCommand>(&cmd, 1));
#endif
    if (presented) {
      g.frames++;
      st.max_frame_draws = std::max(st.max_frame_draws, st.frame_draws);
      st.frame_draws = 0;
      if (g.frames == 300 || (g.frames % 1200) == 0) {
        print_stats("periodic");
#ifdef D2_VULKAN_GAME_BACKEND
        tr::print_counters();
#endif
      }
    }
  } catch (const std::exception &e) {
    std::fprintf(stderr, "[vulkan] present failed: %s\n", e.what());
    g.closed = true;
  }
}

} // extern "C"
#endif // _WIN32
