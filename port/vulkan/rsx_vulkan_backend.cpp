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
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <tuple>
#include <vector>
#include <span>
#include <stdexcept>
#include <string_view>

#include "rsx_commands.h"
#ifdef D2_VULKAN_GAME_BACKEND
#include "sys_overlay.h" // SDK system dialogs (save/load lists, confirmations)
#include <atomic>
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
#include "../graphics/graphics.h"
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
  bool show_fps = false; // FPS counter in the corner of the screen
  bool texdump = false; // save every texture the game loads as PNG (texture pack)
  uint32_t scale_pct = 100; // internal resolution: percent of the output size (50..200)
  // Image filters, each 0 (off) .. 3 (strong).
  uint32_t sharpen = 0, scanlines = 0, crt_mask = 0, vignette = 0, vivid = 0;
  uint32_t aa = 0;       // 0 off, 1 FXAA
  uint32_t lang = 0;     // menu language: 0 Portugues, 1 English
  int api = -1;          // graphics API for the next start: 0 DirectX 12, 1 Vulkan, -1 not chosen in the game
  // Keyboard bindings (Windows virtual-key codes) for the pad buttons, in the order of kKeyActions below.
  int keys[15] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'Z', 'X', 'A', 'S', 'Q', 'W', '1', '2', VK_RETURN, VK_TAB, VK_F12}; // [14] = screenshot
  uint32_t upscaler = 0; // 0 bilinear, 1 AMD FSR 1.0 (EASU + RCAS)
  uint32_t width = 1280, height = 720; // windowed client size
};
Settings g_settings;
std::atomic<int> g_listen{-1}; // pad action waiting for a key press in the controls tab, or -1
// UTF-8 text in the menu language: L(portuguese, english).
inline const char *L(const char *pt, const char *en) { return g_settings.lang ? en : pt; }

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
  // First run: follow the Windows display language (Portuguese keeps Portuguese, everything else gets English).
  g_settings.lang = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE) ? 0u : 1u;
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
        else if (k == "showfps") g_settings.show_fps = value != 0;
        else if (k == "texdump") g_settings.texdump = value != 0;
        else if (k == "scale" && value >= 25 && value <= 400) g_settings.scale_pct = value;
        else if (k == "sharpen" && value <= 3) g_settings.sharpen = value;
        else if (k == "scanlines" && value <= 3) g_settings.scanlines = value;
        else if (k == "crtmask" && value <= 3) g_settings.crt_mask = value;
        else if (k == "vignette" && value <= 3) g_settings.vignette = value;
        else if (k == "vivid" && value <= 3) g_settings.vivid = value;
        else if (k == "aa" && value <= 1) g_settings.aa = value;
        else if (k == "upscaler" && value <= 1) g_settings.upscaler = value;
        else if (k == "lang" && value <= 1) g_settings.lang = value;
        else if (k == "api" && value <= 1) g_settings.api = int(value);
        else if (k.rfind("key", 0) == 0 && k.size() > 3 && value >= 1 && value <= 254) {
          const int idx = std::atoi(k.c_str() + 3);
          if (idx >= 0 && idx < 15) g_settings.keys[idx] = int(value);
        }
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
    std::fprintf(f, "fullscreen=%u\nvsync=%u\nshowfps=%u\nscale=%u\nsharpen=%u\nscanlines=%u\ncrtmask=%u\nvignette=%u\nvivid=%u\naa=%u\nupscaler=%u\nlang=%u\nwidth=%u\nheight=%u\n",
                 g_settings.fullscreen ? 1u : 0u, g_settings.vsync ? 1u : 0u, g_settings.show_fps ? 1u : 0u, g_settings.scale_pct,
                 g_settings.sharpen, g_settings.scanlines, g_settings.crt_mask, g_settings.vignette, g_settings.vivid, g_settings.aa, g_settings.upscaler, g_settings.lang,
                 g_settings.width, g_settings.height);
    for (int i = 0; i < 15; i++) std::fprintf(f, "key%d=%d\n", i, g_settings.keys[i]);
    if (g_settings.api >= 0) std::fprintf(f, "api=%d\n", g_settings.api);
    std::fprintf(f, "texdump=%u\n", g_settings.texdump ? 1u : 0u);
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

// ---- options menu (F1) -------------------------------------------------------
// Mouse driven on purpose: the game reads the keyboard itself (arrows, Enter, ...), so keyboard navigation
// would also move the game. The layout is produced each frame by build_overlay() (further down); the window
// thread only hit-tests against the rectangles stored here. Two tabs: video and image filters.
enum Act { A_MODE, A_WINSIZE, A_SCALE, A_VSYNC, A_FPS, A_SHARPEN, A_SCAN, A_MASK, A_VIGNETTE, A_VIVID, A_AA, A_UPSCALER, A_LANG, A_RESET, A_CLOSE, A_TAB_VIDEO, A_TAB_FILTERS, A_TAB_CONTROLS, A_KEYS_RESET, A_API, A_TAB_SYSTEM, A_TEXDUMP };
constexpr int A_KEY0 = 200; // A_KEY0 + i: rebind pad action i
constexpr int kMaxHits = 32;
constexpr uint32_t kResW[] = {1280, 1600, 1920, 2560, 3840}, kResH[] = {720, 900, 1080, 1440, 2160};
constexpr int kResCount = 5;
constexpr uint32_t kScalePct[] = {50, 75, 100, 150, 200};
constexpr int kScaleCount = 5;
struct Menu {
  std::mutex m;
  bool open = false;
  int page = 0;     // 0 video, 1 filters
  int hover = -1;   // Act under the mouse
  int nhits = 0;
  int act[kMaxHits]{};
  float rx[kMaxHits]{}, ry[kMaxHits]{}, rw[kMaxHits]{}, rh[kMaxHits]{};
  float px = 0, py = 0, pw = 0, ph = 0; // panel rectangle (0 until the first frame draws it)
  float sx = 1.f, sy = 1.f; // overlay pixels per window pixel (differs when the game is render-scaled)
} menu;

bool menu_is_open() {
  std::lock_guard<std::mutex> lock(menu.m);
  return menu.open;
}
void menu_toggle(bool open) {
  g_listen = -1;
  std::lock_guard<std::mutex> lock(menu.m);
  menu.open = open;
  menu.hover = -1;
}
int menu_hit(float x, float y) { // returns the Act under the point, or -1
  std::lock_guard<std::mutex> lock(menu.m);
  x *= menu.sx;
  y *= menu.sy;
  for (int i = 0; i < menu.nhits; i++)
    if (x >= menu.rx[i] && x < menu.rx[i] + menu.rw[i] && y >= menu.ry[i] && y < menu.ry[i] + menu.rh[i])
      return menu.act[i];
  return -1;
}
void apply_window_size(uint32_t w, uint32_t h) {
  g_settings.width = w;
  g_settings.height = h;
  if (!g.hwnd || g.fullscreen)
    return;
  if (IsZoomed(g.hwnd))
    ShowWindow(g.hwnd, SW_RESTORE);
  RECT wr{0, 0, (LONG)w, (LONG)h};
  AdjustWindowRect(&wr, DWORD(GetWindowLongPtrA(g.hwnd, GWL_STYLE)), FALSE);
  SetWindowPos(g.hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---- Graphics API switch ------------------------------------------------------------------------------------
// The API is chosen before the window exists, so the switch saves the choice (api= in settings.ini) and restarts
// the game; main.cpp reads it at the next start. The first click only arms the row, a second click within a few
// seconds restarts (unsaved game progress is lost, exactly like closing the window).
#ifdef D2_VULKAN_GAME_BACKEND
extern "C" void d2_restart_with_api(int api);
#endif
std::atomic<ULONGLONG> g_api_armed_until{0};
bool api_is_armed() { return GetTickCount64() < g_api_armed_until.load(); }
void request_api_switch() {
  if (!api_is_armed()) {
    g_api_armed_until = GetTickCount64() + 6000;
    return;
  }
  g_api_armed_until = 0;
  g_settings.api = 0; // this backend is Vulkan: the only other choice is DirectX 12
  save_settings();
  std::fprintf(stderr, "[vulkan] graphics API switch requested: restarting with DirectX 12\n");
#ifdef D2_VULKAN_GAME_BACKEND
  d2_restart_with_api(0);
#endif
}

// ---- Keyboard bindings ------------------------------------------------------------------------------------------
// The SDK pad backend reads these keys when no XInput controller is connected (ps3_pad_set_keyboard_map).
constexpr int kKeyActionCount = 15; // 14 pad actions + screenshot
constexpr int kPadKeys = 14;
constexpr int kShotKey = 14;
constexpr int kDefaultKeys[kKeyActionCount] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'Z', 'X', 'A', 'S', 'Q', 'W', '1', '2', VK_RETURN, VK_TAB, VK_F12};
std::atomic<bool> g_shot_wanted{false};
#ifdef D2_VULKAN_GAME_BACKEND
extern "C" void ps3_pad_set_keyboard_map(const int *vk, int count);
void apply_keymap() { ps3_pad_set_keyboard_map(g_settings.keys, kPadKeys); }
#else
void apply_keymap() {}
#endif
std::string vk_name(int vk) {
  switch (vk) {
  case VK_UP: return L("Seta cima", "Up");
  case VK_DOWN: return L("Seta baixo", "Down");
  case VK_LEFT: return L("Seta esq.", "Left");
  case VK_RIGHT: return L("Seta dir.", "Right");
  case VK_RETURN: return "Enter";
  case VK_TAB: return "Tab";
  case VK_SPACE: return L("Espa\303\247o", "Space");
  case VK_BACK: return "Backspace";
  case VK_SHIFT: return "Shift";
  case VK_CONTROL: return "Ctrl";
  case VK_MENU: return "Alt";
  case VK_CAPITAL: return "Caps";
  case VK_DELETE: return "Del";
  case VK_INSERT: return "Ins";
  case VK_HOME: return "Home";
  case VK_END: return "End";
  case VK_PRIOR: return "PgUp";
  case VK_NEXT: return "PgDn";
  case VK_SNAPSHOT: return "Print Screen";
  default: break;
  }
  if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::string(1, char(vk));
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Num" + std::string(1, char('0' + (vk - VK_NUMPAD0)));
  if (vk >= VK_F1 && vk <= VK_F12) return "F" + std::to_string(vk - VK_F1 + 1);
  char name[64] = {};
  if (GetKeyNameTextA(LONG(MapVirtualKeyA(UINT(vk), MAPVK_VK_TO_VSC)) << 16, name, int(sizeof name)) > 0) return name;
  return "VK" + std::to_string(vk);
}
// Binds `vk` to action `index`; a key already used by another action is swapped over to it.
void assign_key(int index, int vk) {
  if (index < 0 || index >= kKeyActionCount) return;
  for (int i = 0; i < kKeyActionCount; i++)
    if (i != index && g_settings.keys[i] == vk) g_settings.keys[i] = g_settings.keys[index];
  g_settings.keys[index] = vk;
  std::fprintf(stderr, "[vulkan][keys] action %d -> VK %d\n", index, vk);
  apply_keymap();
}
bool key_is_bindable(int vk) {
  return vk > 0 && vk < 255 && vk != VK_F1 && vk != VK_F11 && vk != VK_ESCAPE && vk != VK_LWIN && vk != VK_RWIN && vk != VK_APPS;
}
uint32_t cycle_level(uint32_t value, int dir) { return uint32_t((int(value) + dir + 4) % 4); }
void menu_activate(int action, int dir) {
  if (action >= A_KEY0 && action < A_KEY0 + kKeyActionCount) { // click on a key cell: wait for the new key
    g_listen = action - A_KEY0;
    return;
  }
  g_listen = -1;
  if (action != A_API) g_api_armed_until = 0;
  switch (action) {
  case A_MODE: set_fullscreen(!g.fullscreen); break;
  case A_WINSIZE:
    if (!g.fullscreen) {
      int cur = 0;
      for (int i = 0; i < kResCount; i++)
        if (kResH[i] <= g_settings.height) cur = i;
      cur = (cur + dir + kResCount) % kResCount;
      apply_window_size(kResW[cur], kResH[cur]);
    }
    break;
  case A_SCALE: {
    int cur = 2;
    for (int i = 0; i < kScaleCount; i++)
      if (kScalePct[i] == g_settings.scale_pct) cur = i;
    g_settings.scale_pct = kScalePct[(cur + dir + kScaleCount) % kScaleCount];
    break; // the renderer picks it up on the next frame
  }
  case A_VSYNC:
    g.vsync = !g.vsync;
    g_settings.vsync = g.vsync;
    g.resized = true; // swapchain is recreated with the new present mode
    break;
  case A_FPS: g_settings.show_fps = !g_settings.show_fps; break;
  case A_SHARPEN: g_settings.sharpen = cycle_level(g_settings.sharpen, dir); break;
  case A_SCAN: g_settings.scanlines = cycle_level(g_settings.scanlines, dir); break;
  case A_MASK: g_settings.crt_mask = cycle_level(g_settings.crt_mask, dir); break;
  case A_VIGNETTE: g_settings.vignette = cycle_level(g_settings.vignette, dir); break;
  case A_VIVID: g_settings.vivid = cycle_level(g_settings.vivid, dir); break;
  case A_AA: if (g_settings.upscaler) return; g_settings.aa = g_settings.aa ? 0u : 1u; break;
  case A_UPSCALER: g_settings.upscaler = g_settings.upscaler ? 0u : 1u; break;
  case A_LANG: g_settings.lang = g_settings.lang ? 0u : 1u; break;
  case A_TEXDUMP: g_settings.texdump = !d2_texture_dump_enabled(); d2_texture_set_dump(g_settings.texdump ? 1 : 0); break;
  case A_API:
    if (dir != 1) { g_api_armed_until = 0; return; }
    request_api_switch();
    return;
  case A_RESET:
    g.vsync = g_settings.vsync = true;
    g_settings.show_fps = false;
    g_settings.scale_pct = 100;
    g_settings.sharpen = g_settings.scanlines = g_settings.crt_mask = g_settings.vignette = g_settings.vivid = 0;
    g_settings.aa = g_settings.upscaler = 0;
    g.resized = true;
    if (g.fullscreen) set_fullscreen(false);
    apply_window_size(1280, 720);
    break;
  case A_CLOSE: menu_toggle(false); break;
  case A_TAB_VIDEO:
  case A_TAB_FILTERS:
  case A_TAB_CONTROLS:
  case A_TAB_SYSTEM: {
    std::lock_guard<std::mutex> lock(menu.m);
    menu.page = action == A_TAB_SYSTEM ? 3 : action == A_TAB_CONTROLS ? 2 : action == A_TAB_FILTERS ? 1 : 0;
    return; // nothing to save
  }
  case A_KEYS_RESET:
    for (int i = 0; i < kKeyActionCount; i++) g_settings.keys[i] = kDefaultKeys[i];
    apply_keymap();
    break;
  default: return;
  }
  save_settings();
}

// ---- System dialogs (save / load slot lists, confirmations) --------------------------------------------------
// The SDK keeps the dialog state (ps3_overlay_*); the game thread blocks inside cellSaveData until it ends. We
// draw it (build_overlay) and translate mouse clicks into the same results the keyboard/pad produce.
constexpr int D_ROW = 1000, D_OK = 1100, D_BACK = 1101, D_YES = 1102, D_NO = 1103;
constexpr int kDlgRows = 8;
#ifdef D2_VULKAN_GAME_BACKEND
std::atomic<bool> g_dialog{false};
std::atomic<int> g_dlg_top{0};
bool dialog_active() { return g_dialog.load(); }
void dialog_activate(int act, int dir) {
  if (act < 0) return;
  auto snap = std::make_unique<SysOverlaySnapshot>();
  ps3_overlay_snapshot(snap.get());
  if (!snap->active) return;
  const uint64_t token = snap->token;
  auto pulse = [](uint16_t b) { ps3_overlay_input(0); ps3_overlay_input(b); };
  if (act >= D_ROW && act < D_ROW + kDlgRows) {
    const int target = g_dlg_top.load() + (act - D_ROW);
    if (target < 0 || target >= snap->count) return;
    int cur = snap->selected;
    while (cur < target) { pulse(0x40); cur++; }
    while (cur > target) { pulse(0x10); cur--; }
    ps3_overlay_input(0);
    if (dir > 0) ps3_overlay_finish(token, SYS_OVERLAY_OK);
    else if (!snap->cancel_disabled) ps3_overlay_finish(token, SYS_OVERLAY_BACK);
  } else if (act == D_OK || act == D_YES) {
    ps3_overlay_finish(token, SYS_OVERLAY_OK);
  } else if (act == D_NO) {
    ps3_overlay_finish(token, SYS_OVERLAY_NO);
  } else if (act == D_BACK) {
    if (!snap->cancel_disabled) ps3_overlay_finish(token, SYS_OVERLAY_BACK);
  }
}
void dialog_wheel(int dir) { // list dialogs: move the selection one step
  auto snap = std::make_unique<SysOverlaySnapshot>();
  ps3_overlay_snapshot(snap.get());
  if (snap->active && snap->kind == SYS_OVERLAY_LIST && snap->count) {
    ps3_overlay_input(0);
    ps3_overlay_input(dir > 0 ? 0x40 : 0x10);
    ps3_overlay_input(0);
  }
}
#else
bool dialog_active() { return false; }
void dialog_activate(int, int) {}
void dialog_wheel(int) {}
#endif

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_CLOSE:
    save_settings();
    g.closed = true;
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  case WM_SIZE:
    g.resized = true;
    // Remember the windowed size (not while borderless fullscreen, maximized or minimized).
    if (wp == SIZE_RESTORED && (GetWindowLongPtrA(hwnd, GWL_STYLE) & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW &&
        LOWORD(lp) >= 640 && HIWORD(lp) >= 360) {
      g_settings.width = LOWORD(lp);
      g_settings.height = HIWORD(lp);
    }
    return 0;
  case WM_EXITSIZEMOVE:
    save_settings();
    return 0;
  case WM_MOUSEMOVE:
    if (dialog_active() || menu_is_open()) {
      const int hit = menu_hit(float(short(LOWORD(lp))), float(short(HIWORD(lp))));
      std::lock_guard<std::mutex> lock(menu.m);
      menu.hover = hit;
    }
    return 0;
  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
    if (dialog_active()) {
      int hit = menu_hit(float(short(LOWORD(lp))), float(short(HIWORD(lp))));
      if (hit < 0 && msg == WM_RBUTTONDOWN) hit = D_BACK;
      if (hit >= 0) dialog_activate(hit, msg == WM_LBUTTONDOWN ? 1 : -1);
      return 0;
    }
    if (menu_is_open()) {
      const int hit = menu_hit(float(short(LOWORD(lp))), float(short(HIWORD(lp))));
      if (hit >= A_KEY0) {
        if (msg == WM_LBUTTONDOWN) menu_activate(hit, 1);
        else g_listen = -1; // right click on a key cell cancels the rebinding
      } else if (hit >= 0)
        menu_activate(hit, msg == WM_LBUTTONDOWN ? 1 : -1);
      return 0;
    }
    break;
  case WM_MOUSEWHEEL:
    if (dialog_active()) {
      dialog_wheel(short(HIWORD(wp)) > 0 ? -1 : 1);
      return 0;
    }
    if (menu_is_open()) {
      int hit;
      {
        std::lock_guard<std::mutex> lock(menu.m);
        hit = menu.hover;
      }
      if (hit >= 0 && hit < A_KEY0 && hit != A_API)
        menu_activate(hit, short(HIWORD(wp)) > 0 ? -1 : 1);
      return 0;
    }
    break;
  case WM_SETCURSOR:
    if (g.fullscreen && LOWORD(lp) == HTCLIENT && !menu_is_open()) {
      SetCursor(nullptr); // no mouse pointer over a fullscreen game
      return TRUE;
    }
    break;
  case WM_KEYDOWN:
    if (g_listen.load() >= 0 && menu_is_open()) { // rebinding: the next key becomes the binding
      if (wp == VK_ESCAPE) g_listen = -1;
      else if (key_is_bindable(int(wp))) {
        assign_key(g_listen.load(), int(wp));
        g_listen = -1;
        save_settings();
      }
      return 0;
    }
    // Esc no longer quits: it belongs to the game. Close with Alt+F4 or the window button.
    if (int(wp) == g_settings.keys[kShotKey] && wp != VK_SNAPSHOT) {
      if (!(lp & (1 << 30))) g_shot_wanted = true;
      return 0;
    }
    if (wp == VK_F11)
      set_fullscreen(!g.fullscreen);
    else if (wp == VK_F1)
      menu_toggle(!menu_is_open());
    else if (wp == VK_ESCAPE && menu_is_open())
      menu_toggle(false);
    return 0;
  case WM_KEYUP:
    if (wp == VK_SNAPSHOT && g_settings.keys[kShotKey] == VK_SNAPSHOT) { // Print Screen only reports key-up
      g_shot_wanted = true;
      return 0;
    }
    break;
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
#ifdef D2_VULKAN_GAME_BACKEND
// Image filter strengths for the post-processing pass (see tr::post_fragment_hlsl): [0] sharpen, scanlines,
// CRT mask, vignette; [1] saturation, contrast.
void apply_filters() {
  static const float sharpen[4] = {0.f, 0.3f, 0.6f, 1.0f}, scan[4] = {0.f, 0.2f, 0.4f, 0.6f}, mask[4] = {0.f, 0.25f, 0.5f, 0.75f},
                     vig[4] = {0.f, 0.3f, 0.55f, 0.8f}, sat[4] = {1.f, 1.15f, 1.3f, 1.5f}, con[4] = {1.f, 1.05f, 1.1f, 1.15f},
                     rcas[4] = {0.5f, 0.7f, 0.87f, 1.0f}; // FSR RCAS amount (2^-stops), driven by "Nitidez"
  const std::array<float, 8> params{sharpen[g_settings.sharpen], scan[g_settings.scanlines], mask[g_settings.crt_mask],
                                    vig[g_settings.vignette], sat[g_settings.vivid], con[g_settings.vivid], g_settings.aa ? 1.f : 0.f,
                                    g_settings.upscaler ? rcas[g_settings.sharpen] : 0.f};
  const bool active = g_settings.sharpen || g_settings.scanlines || g_settings.crt_mask || g_settings.vignette || g_settings.vivid || g_settings.aa ||
                      g_settings.upscaler;
  g.renderer->setPostParams(params, active);
}

// Text helpers for the system dialogs: the SDK strings are English; show them in the menu language.
std::string dlg_tr(const char *text) {
  struct Tr { const char *raw, *pt, *en; };
  static const Tr exact[] = {
      {"Save saved data", "SALVAR JOGO", "SAVE GAME"}, {"Load saved data", "CARREGAR JOGO", "LOAD GAME"},
      {"Delete saved data", "APAGAR SAVE", "DELETE SAVE"}, {"Overwrite saved data", "SOBRESCREVER SAVE", "OVERWRITE SAVE"},
      {"Choose a save slot.", "Escolha um slot de save.", "Choose a save slot."},
      {"There is no saved data.", "N\303\243o h\303\241 saves.", "There is no saved data."},
      {"The existing saved data will be replaced.", "O save existente ser\303\241 substitu\303\255do.", "The existing saved data will be replaced."},
      {"Your current progress will be saved to this slot.", "Seu progresso ser\303\241 salvo neste slot.", "Your current progress will be saved to this slot."},
      {"This saved data will be deleted.", "Este save ser\303\241 apagado.", "This saved data will be deleted."}};
  for (const Tr &p : exact)
    if (!std::strcmp(text, p.raw)) return g_settings.lang ? p.en : p.pt;
  static const Tr prefixes[] = {{"New saved data: ", "Novo save: ", "New save: "}, {"Overwrite ", "Sobrescrever ", "Overwrite "},
                                {"Load ", "Carregar ", "Load "}, {"Save ", "Salvar ", "Save "}, {"Delete ", "Apagar ", "Delete "}};
  for (const Tr &p : prefixes) {
    const size_t n = std::strlen(p.raw);
    if (!std::strncmp(text, p.raw, n)) return std::string(g_settings.lang ? p.en : p.pt) + (text + n);
  }
  return text;
}
std::string dlg_fit(const std::string &text, float size, float max_w) { // shorten with an ellipsis (UTF-8 safe)
  if (tr::ui_text_width(text.c_str(), size) <= max_w) return text;
  std::string t = text;
  while (!t.empty()) {
    do t.pop_back(); while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80);
    if (tr::ui_text_width((t + "\342\200\246").c_str(), size) <= max_w) break;
  }
  return t + "\342\200\246";
}
// Word wrap: breaks `text` at spaces so every line fits `max_w` (a single very long word is kept whole).
std::vector<std::string> wrap_text(const std::string &text, float size, float max_w) {
  std::vector<std::string> lines;
  std::string cur, word;
  auto flush_word = [&]() {
    if (word.empty()) return;
    const std::string candidate = cur.empty() ? word : cur + " " + word;
    if (!cur.empty() && tr::ui_text_width(candidate.c_str(), size) > max_w) {
      lines.push_back(cur);
      cur = word;
    } else {
      cur = candidate;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == '\n') { flush_word(); lines.push_back(cur); cur.clear(); }
    else if (c == ' ') flush_word();
    else word += c;
  }
  flush_word();
  if (!cur.empty() || lines.empty()) lines.push_back(cur);
  return lines;
}

// Draws the SDK system dialog in the same Netherworld style as the F1 menu. Returns true when one is active.
bool draw_system_dialog(uint32_t w, uint32_t h, uint32_t cw, uint32_t ch, float s) {
  using Color = std::array<float, 4>;
  static std::unique_ptr<SysOverlaySnapshot> snap = std::make_unique<SysOverlaySnapshot>();
  ps3_overlay_snapshot(snap.get());
  const bool active = snap->active;
  g_dialog = active;
  if (!active) return false;
  const Color white{1.f, 1.f, 1.f, 1.f}, gold{1.f, 0.86f, 0.35f, 1.f}, pink{1.f, 0.28f, 0.58f, 1.f},
      ink{0.03f, 0.01f, 0.09f, 1.f}, soft{0.90f, 0.86f, 1.f, 1.f};
  const bool list = (snap->kind == SYS_OVERLAY_LIST || snap->kind == SYS_OVERLAY_EDITOR) && snap->count > 0;
  const bool yesno = snap->kind == SYS_OVERLAY_YESNO;
  const bool progress = snap->kind == SYS_OVERLAY_PROGRESS;
  const int rows = list ? std::min(snap->count, kDlgRows) : 0;
  int top = g_dlg_top.load();
  if (list) {
    top = std::clamp(snap->selected - kDlgRows / 2, 0, std::max(0, snap->count - kDlgRows));
    g_dlg_top = top;
  }
  const float rowh = 44.f * s, head_h = 68.f * s, line_h = 32.f * s, fs = 25.f * s, hint_fs = 19.f * s;
  const float pw = std::min(float(w) - 20.f, 720.f * s);
  const float text_w = pw - 70.f * s;
  // Message and footer hint, word-wrapped to the panel.
  std::vector<std::string> lines;
  {
    std::string msg = snap->message, cur;
    for (char c : msg) {
      if (c == '\n') { lines.push_back(cur); cur.clear(); }
      else cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
  }
  std::vector<std::string> shown;
  for (const std::string &line : lines)
    for (const std::string &part : wrap_text(dlg_tr(line.c_str()), fs, text_w)) shown.push_back(part);
  const std::string hint = progress ? L("Aguarde\342\200\246", "Please wait\342\200\246")
      : snap->cancel_disabled ? L("Clique ou Z / Enter: confirmar \302\267 Setas: mover", "Click or Z / Enter: confirm \302\267 Arrows: move")
                              : L("Clique ou Z: confirmar \302\267 X ou bot\303\243o direito: voltar \302\267 Setas: mover",
                                  "Click or Z: confirm \302\267 X or right click: back \302\267 Arrows: move");
  const std::vector<std::string> hint_lines = wrap_text(hint, hint_fs, text_w);
  const float hint_line_h = 25.f * s;
  const float foot_h = 22.f * s + hint_line_h * float(hint_lines.size());
  const float body_h = (list ? rowh * rows + 22.f * s : 0.f) + line_h * float(shown.size()) +
                       (progress ? 40.f * s * float(std::max(1, snap->progress_count)) : 0.f) +
                       ((yesno || (!list && !progress && snap->buttons)) ? 66.f * s : 0.f) + 24.f * s;
  const float ph = head_h + body_h + foot_h;
  const float px = std::max(0.f, (float(w) - pw) * 0.5f), py = std::max(0.f, (float(h) - ph) * 0.5f);
  std::lock_guard<std::mutex> lock(menu.m);
  menu.sx = cw ? float(w) / float(cw) : 1.f;
  menu.sy = ch ? float(h) / float(ch) : 1.f;
  menu.nhits = 0;
  auto add_hit = [&](int act, float x, float y, float rw, float rh) {
    if (menu.nhits >= kMaxHits) return;
    const int k = menu.nhits++;
    menu.act[k] = act; menu.rx[k] = x; menu.ry[k] = y; menu.rw[k] = rw; menu.rh[k] = rh;
  };
  tr::ui_rect(0.f, 0.f, float(w), float(h), {0.02f, 0.0f, 0.08f, 0.68f});
  tr::ui_gradient(px - 7.f * s, py - 7.f * s, pw + 14.f * s, ph + 14.f * s, pink, {0.45f, 0.10f, 0.45f, 1.f});
  tr::ui_rect(px - 3.f * s, py - 3.f * s, pw + 6.f * s, ph + 6.f * s, gold);
  tr::ui_gradient(px, py, pw, ph, {0.22f, 0.09f, 0.36f, 1.f}, {0.06f, 0.04f, 0.16f, 1.f});
  tr::ui_gradient(px, py, pw, head_h, {0.78f, 0.16f, 0.45f, 1.f}, {0.36f, 0.08f, 0.40f, 1.f});
  tr::ui_rect(px, py + head_h, pw, 3.f * s, gold);
  tr::ui_diamond(px + 36.f * s, py + head_h * 0.5f, 9.f * s, gold);
  tr::ui_diamond(px + pw - 36.f * s, py + head_h * 0.5f, 9.f * s, gold);
  const std::string title = dlg_fit(dlg_tr(snap->title), 36.f * s, pw - 120.f * s);
  const float tw = tr::ui_text_width(title.c_str(), 36.f * s);
  tr::ui_text_outline(px + (pw - tw) * 0.5f, py + (head_h - tr::ui_line_height(36.f * s)) * 0.5f, title.c_str(), 36.f * s, white, ink, 2.6f * s);

  float y = py + head_h + 14.f * s;
  for (const std::string &line : shown) {
    tr::ui_text_outline(px + 35.f * s, y, line.c_str(), fs, white, ink, 2.2f * s);
    y += line_h;
  }
  if (list) {
    y += 6.f * s;
    for (int i = 0; i < rows; i++) {
      const int idx = top + i;
      const float ry = y + rowh * i;
      add_hit(D_ROW + i, px, ry, pw, rowh);
      const bool sel = idx == snap->selected, hot = menu.hover == D_ROW + i;
      if (sel || hot) {
        tr::ui_gradient(px + 14.f * s, ry + 3.f * s, pw - 28.f * s, rowh - 6.f * s,
                        sel ? Color{1.f, 0.35f, 0.65f, 0.95f} : Color{0.45f, 0.20f, 0.60f, 0.85f},
                        sel ? Color{0.62f, 0.12f, 0.50f, 0.95f} : Color{0.30f, 0.12f, 0.45f, 0.85f}, 10.f * s);
        if (sel) tr::ui_rect(px + 14.f * s, ry + 3.f * s, 5.f * s, rowh - 6.f * s, gold);
      }
      const std::string label = dlg_fit(dlg_tr(snap->items[idx].label), fs, pw - 90.f * s);
      tr::ui_text_outline(px + 38.f * s, ry + (rowh - tr::ui_line_height(fs)) * 0.5f, label.c_str(), fs, sel ? gold : white, ink, 2.2f * s);
    }
    if (snap->count > kDlgRows) {
      char more[48];
      std::snprintf(more, sizeof more, "%d / %d", snap->selected + 1, snap->count);
      const float mw = tr::ui_text_width(more, 19.f * s);
      tr::ui_text_outline(px + pw - 30.f * s - mw, y + rowh * rows + 2.f * s, more, 19.f * s, soft, ink, 1.6f * s);
    }
    y += rowh * rows + 16.f * s;
  }
  if (progress) {
    for (int i = 0; i < std::max(1, snap->progress_count); i++) {
      const float bw = pw - 80.f * s;
      tr::ui_rect(px + 40.f * s, y + 8.f * s, bw, 14.f * s, {0.10f, 0.05f, 0.20f, 1.f});
      tr::ui_rect(px + 40.f * s, y + 8.f * s, bw * float(std::min(100u, snap->progress[i])) / 100.f, 14.f * s, {1.f, 0.35f, 0.65f, 1.f});
      y += 40.f * s;
    }
  }
  if (yesno) {
    const char *names[2] = {L("Sim", "Yes"), L("N\303\243o", "No")};
    const float bw = 170.f * s, bh = 46.f * s, gap = 24.f * s, bx = px + (pw - bw * 2 - gap) * 0.5f;
    for (int i = 0; i < 2; i++) {
      const float x = bx + (bw + gap) * i;
      const bool sel = snap->selected == i, hot = menu.hover == (i ? D_NO : D_YES);
      add_hit(i ? D_NO : D_YES, x, y + 6.f * s, bw, bh);
      tr::ui_gradient(x, y + 6.f * s, bw, bh, sel || hot ? Color{1.f, 0.35f, 0.65f, 1.f} : Color{0.20f, 0.10f, 0.32f, 1.f},
                      sel || hot ? Color{0.62f, 0.12f, 0.50f, 1.f} : Color{0.10f, 0.05f, 0.20f, 1.f}, 8.f * s);
      if (sel) tr::ui_rect(x, y + 6.f * s + bh - 4.f * s, bw, 3.f * s, gold);
      const float nw = tr::ui_text_width(names[i], fs);
      tr::ui_text_outline(x + (bw - nw) * 0.5f, y + 6.f * s + (bh - tr::ui_line_height(fs)) * 0.5f, names[i], fs, sel ? gold : white, ink, 2.2f * s);
    }
  } else if (!list && !progress && snap->buttons) {
    const float bw = 190.f * s, bh = 46.f * s, x = px + (pw - bw) * 0.5f;
    add_hit(D_OK, x, y + 6.f * s, bw, bh);
    const bool hot = menu.hover == D_OK;
    tr::ui_gradient(x, y + 6.f * s, bw, bh, {1.f, 0.35f, 0.65f, hot ? 1.f : 0.9f}, {0.62f, 0.12f, 0.50f, hot ? 1.f : 0.9f}, 8.f * s);
    tr::ui_rect(x, y + 6.f * s + bh - 4.f * s, bw, 3.f * s, gold);
    const float nw = tr::ui_text_width("OK", fs);
    tr::ui_text_outline(x + (bw - nw) * 0.5f, y + 6.f * s + (bh - tr::ui_line_height(fs)) * 0.5f, "OK", fs, gold, ink, 2.2f * s);
  }
  float fy = py + ph - foot_h + 8.f * s;
  tr::ui_rect(px + 30.f * s, fy - 6.f * s, pw - 60.f * s, 2.f * s, {1.f, 0.28f, 0.58f, 0.8f});
  fy += 6.f * s;
  for (const std::string &line : hint_lines) {
    tr::ui_text_outline(px + 35.f * s, fy, line.c_str(), hint_fs, soft, ink, 1.8f * s);
    fy += hint_line_h;
  }
  return true;
}

// Draws the FPS counter and the F1 options menu with the tr::ui_* primitives, once per presented frame.
// (w, h) is the size of the image being drawn; (cw, ch) the window's client size (the mouse lives there).
void build_overlay(uint32_t w, uint32_t h, uint32_t cw, uint32_t ch, bool hidden = false) {
  using Color = std::array<float, 4>;
  tr::ui_begin(w, h);
  if (hidden) return; // screenshot frame: nothing of the interface is drawn
  const float s = std::max(0.6f, float(h) / 720.f);
  // Palette: night-sky violet, netherworld magenta and gold trim.
  const Color white{1.f, 1.f, 1.f, 1.f}, gold{1.f, 0.82f, 0.30f, 1.f}, pink{1.f, 0.28f, 0.58f, 1.f},
      ink{0.03f, 0.01f, 0.09f, 1.f};

  // Presented frames per second (what the player sees), refreshed twice a second.
  static auto last = std::chrono::steady_clock::now();
  static unsigned count = 0, shown = 0;
  static uint64_t frame_counter = 0;
  frame_counter++;
  count++;
  const auto now = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double, std::milli>(now - last).count();
  if (elapsed >= 500.0) {
    shown = unsigned(count * 1000.0 / elapsed + 0.5);
    count = 0;
    last = now;
  }
  const bool open = menu_is_open();
  const bool dialog = draw_system_dialog(w, h, cw, ch, s);
  if (g_settings.show_fps) {
    char text[32];
    std::snprintf(text, sizeof text, "FPS %u", shown);
    tr::ui_text_outline(14.f * s, 10.f * s, text, 22.f * s, shown >= 55 ? Color{0.55f, 1.f, 0.65f, 1.f} : gold, ink, 1.5f * s);
  }
  {
    // Notices (screenshot saved, controller plugged/unplugged): a card that slides up from the bottom-right corner,
    // Steam style, in the menu's colours. Same behaviour as the DirectX 12 notices.
    struct Notice { std::string title, text; int kind; }; // kind: 0 screenshot, 1 controller connected, 2 disconnected
    static std::vector<Notice> queue;
    static bool active = false;
    static Notice cur;
    static auto t0 = std::chrono::steady_clock::now();
    static unsigned seen = 0;
    char path[520] = {};
    const unsigned n = d2_screenshot_last(path, sizeof path);
    if (n != seen) {
      seen = n;
      const char *base = std::strrchr(path, '\\');
      if (queue.size() < 6) queue.push_back({L("Captura salva", "Screenshot saved"), base ? base + 1 : path, 0});
    }
    { // controller hot-plug through XInput, polled twice a second
      typedef DWORD(WINAPI * GetStateFn)(DWORD, void *);
      static GetStateFn get = nullptr;
      static bool tried = false, primed = false;
      static unsigned mask = 0;
      static auto next = std::chrono::steady_clock::now();
      if (now >= next) {
        next = now + std::chrono::milliseconds(500);
        if (!tried) {
          tried = true;
          for (const wchar_t *dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"})
            if (HMODULE m = LoadLibraryW(dll)) { get = (GetStateFn)GetProcAddress(m, "XInputGetState"); if (get) break; }
        }
        if (get) {
          unsigned cur_mask = 0;
          char state[32];
          for (DWORD i = 0; i < 4; i++) if (get(i, state) == ERROR_SUCCESS) cur_mask |= 1u << i;
          if (!primed) primed = true;
          else if (cur_mask != mask) {
            for (int i = 0; i < 4; i++) {
              const std::string who = std::string(L("Jogador ", "Player ")) + std::to_string(i + 1);
              if ((cur_mask & ~mask) & (1u << i) && queue.size() < 6) queue.push_back({L("Controle conectado", "Controller connected"), who, 1});
              else if ((mask & ~cur_mask) & (1u << i) && queue.size() < 6) queue.push_back({L("Controle desconectado", "Controller disconnected"), who, 2});
            }
          }
          mask = cur_mask;
        }
      }
    }
    if (!active && !queue.empty()) {
      cur = queue.front();
      queue.erase(queue.begin());
      active = true;
      t0 = now;
    }
    if (active) {
      const double ms = std::chrono::duration<double, std::milli>(now - t0).count();
      const double in = 320.0, hold = 3800.0, out = 320.0, total = in + hold + out;
      if (ms >= total) active = false;
      else {
        auto ease = [](double t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t); };
        double pr = 1.0;
        if (ms < in) pr = ease(ms / in);
        else if (ms > total - out) pr = 1.0 - ease((ms - (total - out)) / out);
        const float ns = s * 0.7f; // notices are smaller than the menu
        const float cw2 = std::min(250.f * ns, float(w) - 24.f * ns), chh = 56.f * ns, margin = 12.f * ns;
        const float x = float(w) - cw2 - margin, y = float(h) - (chh + margin) * float(pr); // slides up from below the bottom edge
        const float bd = std::max(2.f, 2.f * ns);
        tr::ui_rect(x, y, cw2, chh, pink);
        tr::ui_gradient(x + bd, y + bd, cw2 - 2 * bd, chh - 2 * bd, {0.22f, 0.09f, 0.36f, 1.f}, {0.06f, 0.04f, 0.16f, 1.f});
        const bool off = cur.kind == 2;
        tr::ui_gradient(x + bd, y + bd, chh - bd, chh - 2 * bd, off ? Color{0.47f, 0.24f, 0.51f, 1.f} : Color{0.78f, 0.16f, 0.45f, 1.f},
                        off ? Color{0.20f, 0.10f, 0.31f, 1.f} : Color{0.36f, 0.08f, 0.40f, 1.f});
        const Color ic = off ? Color{0.75f, 0.69f, 0.84f, 1.f} : gold;
        const float cx = x + chh * 0.5f, cy = y + chh * 0.5f, u = chh / 84.f * 0.9f;
        if (cur.kind == 0) { // camera
          tr::ui_rect(cx - 20 * u, cy - 11 * u, 40 * u, 28 * u, ic);
          tr::ui_rect(cx - 9 * u, cy - 17 * u, 18 * u, 8 * u, ic);
          tr::ui_rect(cx - 8 * u, cy - 4 * u, 16 * u, 14 * u, {0.22f, 0.09f, 0.36f, 1.f});
          tr::ui_rect(cx - 4 * u, cy, 8 * u, 6 * u, ic);
        } else { // game controller
          tr::ui_rect(cx - 22 * u, cy - 10 * u, 44 * u, 22 * u, ic);
          tr::ui_rect(cx - 22 * u, cy + 6 * u, 10 * u, 12 * u, ic);
          tr::ui_rect(cx + 12 * u, cy + 6 * u, 10 * u, 12 * u, ic);
          const Color dark{0.22f, 0.09f, 0.36f, 1.f};
          tr::ui_rect(cx - 16 * u, cy - 3 * u, 10 * u, 3 * u, dark);
          tr::ui_rect(cx - 12.5f * u, cy - 6.5f * u, 3 * u, 10 * u, dark);
          tr::ui_rect(cx + 8 * u, cy - 5 * u, 4 * u, 4 * u, dark);
          tr::ui_rect(cx + 14 * u, cy - 1 * u, 4 * u, 4 * u, dark);
        }
        const float tx = x + chh + 10.f * ns, room = cw2 - chh - 18.f * ns;
        tr::ui_text_outline(tx, y + 8.f * ns, cur.title.c_str(), 17.f * ns, gold, ink, 1.4f * ns);
        std::string line = cur.text;
        if (tr::ui_text_width(line.c_str(), 13.f * ns) > room) { // shorten (never loops: the text only ever gets shorter)
          std::string cut = line;
          while (cut.size() > 1) {
            cut.pop_back();
            while (cut.size() > 1 && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back(); // keep UTF-8 intact
            if (tr::ui_text_width((cut + "\342\200\246").c_str(), 13.f * ns) <= room) break;
          }
          line = cut + "\342\200\246";
        }
        tr::ui_text_outline(tx, y + 31.f * ns, line.c_str(), 13.f * ns, {0.91f, 0.86f, 1.f, 1.f}, ink, 1.1f * ns);
      }
    }
  }
  if (dialog) return; // system dialog on top: the F1 menu and hints wait
  if (!open) {
    if (frame_counter < 600) { // first ten seconds: tell the player where the options are
      const float a = frame_counter < 480 ? 1.f : float(600 - frame_counter) / 120.f;
      const char *hint = L("F1: Op\303\247\303\265es   \302\267   F11: Tela cheia", "F1: Options   \302\267   F11: Fullscreen");
      tr::ui_text_outline(14.f * s, float(h) - 38.f * s, hint, 22.f * s, {1.f, 1.f, 1.f, a}, {0.03f, 0.01f, 0.09f, a}, 2.2f * s);
    }
    return;
  }

  std::lock_guard<std::mutex> lock(menu.m);
  menu.sx = cw ? float(w) / float(cw) : 1.f;
  menu.sy = ch ? float(h) / float(ch) : 1.f;
  constexpr int kRows = 10; // settings + restore + back (pages with fewer rows are padded)
  const float rowh = 44.f * s, head_h = 70.f * s, tab_h = 42.f * s, fs = 25.f * s, hint_fs = 19.f * s;
  const float pw = std::min(float(w) - 20.f, 720.f * s);
  const Color soft{0.90f, 0.86f, 1.f, 1.f};
  // Footer hints, word-wrapped to the panel width.
  const std::string hint_texts[3] = {
      L("Clique para avan\303\247ar \302\267 roda do mouse ou bot\303\243o direito: voltar",
        "Click to advance \302\267 mouse wheel or right click: go back"),
      menu.page == 3 ? L("Trocar a API reinicia o jogo (o progresso n\303\243o salvo \303\251 perdido). Para voltar ao Vulkan, aperte F1 na janela do DirectX 12.",
                         "Switching the API restarts the game (unsaved progress is lost). To return to Vulkan, press F1 in the DirectX 12 window.")
      : menu.page == 2 ? L("Clique em uma a\303\247\303\243o e aperte a nova tecla (Esc cancela). Vale quando n\303\243o h\303\241 controle conectado.",
                         "Click an action and press the new key (Esc cancels). Applies when no controller is connected.")
      : menu.page == 0 ? L("AMD FSR 1.0 rende melhor com resolu\303\247\303\243o interna abaixo de 100%. Com FSR ligado, a Nitidez ajusta o RCAS.",
                         "AMD FSR 1.0 works best with an internal resolution below 100%. With FSR on, Sharpness drives RCAS.")
                     : L("Os filtros valem para a imagem inteira. \"Desligado\" volta ao original.",
                         "Filters apply to the whole image. \"Off\" restores the original."),
      L("F1 ou Esc fecha \302\267 as op\303\247\303\265es s\303\243o salvas sozinhas.", "F1 or Esc closes \302\267 options are saved automatically.")};
  std::vector<std::string> hint_lines;
  for (const std::string &t : hint_texts)
    for (const std::string &part : wrap_text(t, hint_fs, pw - 70.f * s)) hint_lines.push_back(part);
  const float hint_line_h = 25.f * s;
  const float foot_h = 24.f * s + hint_line_h * float(hint_lines.size());
  const float ph = head_h + tab_h + rowh * kRows + foot_h;
  const float px = std::max(0.f, (float(w) - pw) * 0.5f), py = std::max(0.f, (float(h) - ph) * 0.5f);
  menu.px = px; menu.py = py; menu.pw = pw; menu.ph = ph;
  menu.nhits = 0;
  auto add_hit = [&](int act, float x, float y, float rw, float rh) {
    if (menu.nhits >= kMaxHits) return;
    const int k = menu.nhits++;
    menu.act[k] = act; menu.rx[k] = x; menu.ry[k] = y; menu.rw[k] = rw; menu.rh[k] = rh;
  };

  // Backdrop and panel (violet gradient, double frame in gold and magenta).
  tr::ui_rect(0.f, 0.f, float(w), float(h), {0.02f, 0.0f, 0.08f, 0.62f});
  tr::ui_gradient(px - 7.f * s, py - 7.f * s, pw + 14.f * s, ph + 14.f * s, pink, {0.45f, 0.10f, 0.45f, 1.f});
  tr::ui_rect(px - 3.f * s, py - 3.f * s, pw + 6.f * s, ph + 6.f * s, gold);
  tr::ui_gradient(px, py, pw, ph, {0.22f, 0.09f, 0.36f, 0.99f}, {0.06f, 0.04f, 0.16f, 0.99f});
  // Slanted stripes across the panel for that Netherworld look.
  for (int k = 0; k < 9; k++) {
    const float sx0 = px + (k * 90.f - 20.f) * s;
    if (sx0 < px || sx0 + 26.f * s > px + pw) continue;
    tr::ui_gradient(sx0, py + head_h, 22.f * s, tab_h + rowh * kRows, {1.f, 1.f, 1.f, 0.025f}, {1.f, 1.f, 1.f, 0.0f}, 70.f * s);
  }
  // Title banner.
  tr::ui_gradient(px, py, pw, head_h, {0.78f, 0.16f, 0.45f, 1.f}, {0.36f, 0.08f, 0.40f, 1.f});
  tr::ui_rect(px, py + head_h, pw, 3.f * s, gold);
  tr::ui_diamond(px + 40.f * s, py + head_h * 0.5f, 10.f * s, gold);
  tr::ui_diamond(px + pw - 40.f * s, py + head_h * 0.5f, 10.f * s, gold);
  const char *title = L("OP\303\207\303\225ES", "OPTIONS");
  const float tw = tr::ui_text_width(title, 40.f * s);
  tr::ui_text_outline(px + (pw - tw) * 0.5f, py + (head_h - tr::ui_line_height(40.f * s)) * 0.5f, title, 40.f * s, white, ink, 2.8f * s);

  // Tabs.
  const char *tab_names[4] = {L("V\303\215DEO", "VIDEO"), L("FILTROS", "FILTERS"), L("CONTROLES", "CONTROLS"), L("SISTEMA", "SYSTEM")};
  const int tab_acts[4] = {A_TAB_VIDEO, A_TAB_FILTERS, A_TAB_CONTROLS, A_TAB_SYSTEM};
  const float tab_gap = 10.f * s, tab_w = (pw - 48.f * s - 3.f * tab_gap) / 4.f;
  for (int t = 0; t < 4; t++) {
    const float tx = px + 24.f * s + t * (tab_w + tab_gap), tww = tab_w, ty = py + head_h + 8.f * s;
    const bool active = menu.page == t, hot = menu.hover == tab_acts[t];
    add_hit(tab_acts[t], tx, ty, tww, tab_h - 8.f * s);
    if (active) {
      tr::ui_gradient(tx, ty, tww, tab_h - 8.f * s, {1.f, 0.35f, 0.65f, 1.f}, {0.62f, 0.12f, 0.50f, 1.f}, 8.f * s);
      tr::ui_rect(tx, ty + tab_h - 11.f * s, tww, 3.f * s, gold);
    } else {
      tr::ui_gradient(tx, ty, tww, tab_h - 8.f * s, hot ? Color{0.35f, 0.18f, 0.50f, 1.f} : Color{0.16f, 0.08f, 0.26f, 1.f}, {0.10f, 0.05f, 0.18f, 1.f}, 8.f * s);
    }
    const float nw = tr::ui_text_width(tab_names[t], 23.f * s);
    tr::ui_text_outline(tx + (tww - nw) * 0.5f, ty + (tab_h - 8.f * s - tr::ui_line_height(23.f * s)) * 0.5f, tab_names[t], 23.f * s,
                        active ? white : soft, ink, 2.f * s);
  }

  const char *level_names[4] = {L("\302\253 Desligado \302\273", "\302\253 Off \302\273"), L("\302\253 Leve \302\273", "\302\253 Low \302\273"),
                                L("\302\253 M\303\251dio \302\273", "\302\253 Medium \302\273"), L("\302\253 Forte \302\273", "\302\253 Strong \302\273")};
  const char *on_off[2] = {L("\302\253 Desligado \302\273", "\302\253 Off \302\273"), L("\302\253 Ligado \302\273", "\302\253 On \302\273")};
  char res[48], scale_text[64];
  std::snprintf(res, sizeof res, "\302\253 %u \303\227 %u \302\273", g_settings.width, g_settings.height);
  std::snprintf(scale_text, sizeof scale_text, "\302\253 %u%%  (%u \303\227 %u) \302\273", g_settings.scale_pct,
                unsigned(cw * g_settings.scale_pct / 100.f + 0.5f), unsigned(ch * g_settings.scale_pct / 100.f + 0.5f));
  struct Row { int act; const char *label; const char *value; bool enabled; bool button; };
  const Row video[kRows] = {
      {A_MODE, L("Modo de tela", "Display mode"), g.fullscreen ? L("\302\253 Tela cheia \302\273", "\302\253 Fullscreen \302\273") : L("\302\253 Janela \302\273", "\302\253 Windowed \302\273"), true, false},
      {A_WINSIZE, L("Tamanho da janela", "Window size"), g.fullscreen ? L("s\303\263 em modo janela", "windowed mode only") : res, !g.fullscreen, false},
      {A_SCALE, L("Resolu\303\247\303\243o interna", "Internal resolution"), scale_text, true, false},
      {A_VSYNC, "V-Sync", on_off[g.vsync ? 1 : 0], true, false},
      {A_FPS, L("Mostrar FPS", "Show FPS"), on_off[g_settings.show_fps ? 1 : 0], true, false},
      {A_AA, L("Suaviza\303\247\303\243o (FXAA)", "Anti-aliasing (FXAA)"), on_off[g_settings.aa ? 1 : 0], !g_settings.upscaler, false},
      {A_UPSCALER, "Upscaler", g_settings.upscaler ? "\302\253 AMD FSR 1.0 \302\273" : "\302\253 Bilinear \302\273", true, false},
      {-1, "", "", false, false},
      {A_RESET, L("Restaurar padr\303\265es", "Restore defaults"), "", true, true},
      {A_CLOSE, L("Voltar ao jogo", "Back to game"), "", true, true},
  };
  const Row sys_rows[kRows] = {
      {A_LANG, L("Idioma", "Language"), g_settings.lang ? "\302\253 English \302\273" : "\302\253 Portugu\303\252s \302\273", true, false},
      {A_API, L("API gr\303\241fica", "Graphics API"), api_is_armed() ? L("\302\253 Clique: reiniciar em DirectX 12 \302\273", "\302\253 Click: restart in DirectX 12 \302\273") : "\302\253 Vulkan \302\273", true, false},
      {A_TEXDUMP, L("Salvar texturas (PNG)", "Save textures (PNG)"), on_off[d2_texture_dump_enabled() ? 1 : 0], true, false},
      {-1, "", "", false, false}, {-1, "", "", false, false},
      {-1, "", "", false, false}, {-1, "", "", false, false}, {-1, "", "", false, false},
      {-1, "", "", false, false},
      {A_CLOSE, L("Voltar ao jogo", "Back to game"), "", true, true},
  };
  const Row filters[kRows] = {
      {A_SHARPEN, L("Nitidez", "Sharpness"), level_names[g_settings.sharpen], true, false},
      {A_SCAN, "Scanlines", level_names[g_settings.scanlines], true, false},
      {A_MASK, L("M\303\241scara CRT", "CRT mask"), level_names[g_settings.crt_mask], true, false},
      {A_VIGNETTE, L("Vinheta", "Vignette"), level_names[g_settings.vignette], true, false},
      {A_VIVID, L("Cores vivas", "Vivid colors"), level_names[g_settings.vivid], true, false},
      {-1, "", "", false, false},
      {-1, "", "", false, false},
      {-1, "", "", false, false},
      {A_RESET, L("Restaurar padr\303\265es", "Restore defaults"), "", true, true},
      {A_CLOSE, L("Voltar ao jogo", "Back to game"), "", true, true},
  };
  const Row controls[kRows] = {
      {-1, "", "", false, false}, {-1, "", "", false, false}, {-1, "", "", false, false}, {-1, "", "", false, false},
      {-1, "", "", false, false}, {-1, "", "", false, false}, {-1, "", "", false, false}, {-1, "", "", false, false},
      {A_KEYS_RESET, L("Restaurar teclas", "Restore keys"), "", true, true},
      {A_CLOSE, L("Voltar ao jogo", "Back to game"), "", true, true},
  };
  const Row *rows = menu.page == 0 ? video : menu.page == 1 ? filters : menu.page == 3 ? sys_rows : controls;
  const float rows_y = py + head_h + tab_h;
  if (menu.page == 2) { // two columns of pad actions, each a button showing its current key
    const char *names[kKeyActionCount] = {
        L("Cima", "Up"), L("Baixo", "Down"), L("Esquerda", "Left"), L("Direita", "Right"), L("Cruz", "Cross"), L("C\303\255rculo", "Circle"),
        L("Quadrado", "Square"), L("Tri\303\242ngulo", "Triangle"), "L1", "R1", "L2", "R2", "Start", "Select",
        L("Captura de tela", "Screenshot")};
    const float col_w = (pw - 60.f * s) * 0.5f, cfs = 23.f * s;
    for (int i = 0; i < kKeyActionCount; i++) {
      const int col = i / 8, row = i % 8;
      const float cx = px + 22.f * s + col * (col_w + 16.f * s), cy = rows_y + rowh * row;
      add_hit(A_KEY0 + i, cx, cy, col_w, rowh);
      const bool listening = g_listen.load() == i, hot = menu.hover == A_KEY0 + i;
      tr::ui_gradient(cx, cy + 3.f * s, col_w, rowh - 6.f * s,
                      listening ? Color{1.f, 0.55f, 0.20f, 0.95f} : hot ? Color{1.f, 0.35f, 0.65f, 0.95f} : Color{0.18f, 0.09f, 0.30f, 0.9f},
                      listening ? Color{0.75f, 0.25f, 0.10f, 0.95f} : hot ? Color{0.62f, 0.12f, 0.50f, 0.95f} : Color{0.10f, 0.05f, 0.20f, 0.9f}, 8.f * s);
      if (listening || hot) tr::ui_rect(cx, cy + 3.f * s, 5.f * s, rowh - 6.f * s, gold);
      const float ty = cy + (rowh - tr::ui_line_height(cfs)) * 0.5f;
      tr::ui_text_outline(cx + 16.f * s, ty, names[i], cfs, white, ink, 2.f * s);
      const std::string key = listening ? std::string(L("Pressione\342\200\246", "Press a key\342\200\246")) : vk_name(g_settings.keys[i]);
      const float kw = tr::ui_text_width(key.c_str(), cfs);
      tr::ui_text_outline(cx + col_w - 16.f * s - kw, ty, key.c_str(), cfs, gold, ink, 2.f * s);
    }
  }
  for (int i = 0; i < kRows; i++) {
    if (menu.page == 2 && i < 8) continue; // the key grid above replaced these rows
    const float ry = rows_y + rowh * i;
    add_hit(rows[i].act, px, ry, pw, rowh);
    if (i > 0 && !rows[i].button) tr::ui_rect(px + 30.f * s, ry, pw - 60.f * s, 1.f * s, {1.f, 0.82f, 0.3f, 0.18f});
    const bool hot = rows[i].act == menu.hover && rows[i].enabled;
    if (hot) {
      tr::ui_gradient(px + 14.f * s, ry + 3.f * s, pw - 28.f * s, rowh - 6.f * s, {1.f, 0.35f, 0.65f, 0.95f}, {0.62f, 0.12f, 0.50f, 0.95f}, 10.f * s);
      tr::ui_rect(px + 14.f * s, ry + 3.f * s, 5.f * s, rowh - 6.f * s, gold);
    }
    const float ty = ry + (rowh - tr::ui_line_height(fs)) * 0.5f;
    const Color label_color = rows[i].enabled ? white : Color{0.70f, 0.66f, 0.82f, 1.f};
    if (rows[i].button) {
      const float bw = tr::ui_text_width(rows[i].label, fs);
      tr::ui_text_outline(px + (pw - bw) * 0.5f, ty, rows[i].label, fs, hot ? gold : label_color, ink, 2.2f * s);
    } else {
      tr::ui_text_outline(px + 38.f * s, ty, rows[i].label, fs, label_color, ink, 2.2f * s);
      if (rows[i].value[0]) {
        const float vw = tr::ui_text_width(rows[i].value, fs);
        tr::ui_text_outline(px + pw - 38.f * s - vw, ty, rows[i].value, fs,
                            rows[i].enabled ? (hot ? white : gold) : Color{0.70f, 0.66f, 0.82f, 1.f}, ink, 2.2f * s);
      }
    }
  }
  float fy = rows_y + rowh * kRows + 6.f * s;
  tr::ui_rect(px + 30.f * s, fy, pw - 60.f * s, 2.f * s, {1.f, 0.28f, 0.58f, 0.8f});
  fy += 12.f * s;
  for (const std::string &line : hint_lines) {
    tr::ui_text_outline(px + 35.f * s, fy, line.c_str(), hint_fs, soft, ink, 1.8f * s);
    fy += hint_line_h;
  }
}
#endif
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
  apply_keymap(); // keyboard bindings chosen in the controls tab
  g.vsync = g_settings.vsync;
  try {
    g.hwnd = create_window(g_settings.width, g_settings.height, title);
#ifdef D2_VULKAN_GAME_BACKEND
    if (g.hwnd) { // window/taskbar icon from the exe's icon resource (id 1, see app.rc)
      HINSTANCE hi = GetModuleHandleW(nullptr);
      HICON big = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
      HICON small_icon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
      if (big) SendMessageW(g.hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
      if (small_icon) SendMessageW(g.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    }
#endif
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
    ps3_overlay_set_renderer(1); // save/load lists and confirmations are drawn by build_overlay
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
#ifdef D2_VULKAN_GAME_BACKEND
  { // pause while the window is not in front (D2_PAUSE_UNFOCUSED=0 turns this off)
    static const bool pause_on = [] { const char *e = std::getenv("D2_PAUSE_UNFOCUSED"); return !(e && e[0] == '0'); }();
    auto in_front = [] {
      if (IsIconic(g.hwnd)) return false;
      HWND fg = GetForegroundWindow();
      DWORD pid = 0;
      if (fg) GetWindowThreadProcessId(fg, &pid);
      return fg && pid == GetCurrentProcessId();
    };
    if (pause_on && !in_front()) {
      std::fprintf(stderr, "[D2FOCUS] paused (window not in front)\n");
      while (!g.closed && IsWindow(g.hwnd) && !in_front()) {
        if (rsx_vulkan_backend_pump_messages() < 0) return;
        Sleep(40);
      }
      std::fprintf(stderr, "[D2FOCUS] resumed\n");
    }
  }
#endif
  try {
    uint32_t w = 0, h = 0;
    if (!client_size(w, h))
      return; // minimized: skip presentation, keep the guest running
    g.renderer->setRenderScale(float(g_settings.scale_pct) / 100.f); // internal resolution (marks a resize if changed)
#ifdef D2_VULKAN_GAME_BACKEND
    apply_filters();
#endif
    if (g.resized || g.renderer->needsResize()) {
      if (!g.renderer->resize(w, h, g.vsync))
        return;
      g.resized = false;
    }
    bool presented = false;
#ifdef D2_VULKAN_GAME_BACKEND
    uint32_t rw = w, rh = h; // size of the image the game is drawn into (differs from the window when render-scaled)
    g.renderer->renderExtent(rw, rh);
    const bool shot = g_shot_wanted.exchange(false);
    if (shot) g.renderer->requestScreenshot(); // the interface is left out of this frame
    build_overlay(rw, rh, w, h, shot);
    presented = tr::submit(*g.renderer, rw, rh, g.clear, tr::guest_w, tr::guest_h);
    {
      std::vector<uint8_t> pixels;
      uint32_t sw = 0, sh = 0;
      bool bgra = false;
      if (g.renderer->takeScreenshot(pixels, sw, sh, bgra)) d2_screenshot_save(pixels.data(), sw, sh, bgra ? 1 : 0);
    }
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
