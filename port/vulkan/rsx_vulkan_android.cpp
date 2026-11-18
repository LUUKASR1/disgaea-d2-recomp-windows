// Disgaea D2 - Vulkan RSX backend for Android.
//
// Same draw translation as the Windows Vulkan backend (game_translate.inc: RSX vertex/fragment programs -> HLSL ->
// SPIR-V, textures, draw lists), presented on the app's ANativeWindow. There is no Win32 window, settings menu or
// keyboard handling here: the Activity owns the surface and hands it over with d2_core_set_window().
#ifdef __ANDROID__
#include <android/log.h>
#include <android/native_window.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include "rsx_commands.h"
#include "compiler.h"
#include "fixtures.h"
#include "rsx_fp_decompiler.h"
#include "rsx_texture_layout.h"
#include "rsx_vertex_fetch.h"
#include "rsx_vp_decompiler.h"
#include "vulkan_renderer.h"
#include "rsx_contract.h"
#include "../graphics/graphics.h"

extern "C" uint8_t *vm_base;
extern "C" u32 cellGcmResolveLocated(int local, u32 offset);
extern "C" int cellGcmOffsetIsDisplay(u32 offset);
extern "C" int d2_fp_extract_consts(const u8 *, u32, float *, int) __asm__("rsx_fp_extract_consts");
extern "C" u32 d2_fp_code_hash(const u8 *, u32) __asm__("rsx_fp_code_hash");

// Texture dump / texture pack belong to the Windows build: always off here.
extern "C" {
void d2_texture_dump(const uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t, int, const void *, uint32_t) {}
void d2_texture_set_dump(int) {}
int d2_texture_dump_enabled(void) { return 0; }
int d2_texture_pack_enabled(void) { return 0; }
uint8_t *d2_texture_replacement(const uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t *) { return nullptr; }
void d2_texture_free(void *p) { std::free(p); }
}

namespace {

// ---- window handed over by the Activity ------------------------------------------------------------------------
std::mutex g_window_mutex;
std::condition_variable g_window_cv;
ANativeWindow *g_window = nullptr;
uint64_t g_window_gen = 1;   // bumped whenever the Activity hands over a different surface (or takes it away)

struct State {
  ANativeWindow *window = nullptr;
  std::unique_ptr<d2::vulkan::Renderer> renderer;
  rsx_backend backend{};
  std::array<float, 4> clear{0.f, 0.f, 0.f, 1.f};
  bool closed = false;
  bool initialized = false;
  uint64_t window_gen = 0;      // generation of g.window; differs from g_window_gen after a minimise/restore
  bool surface_lost = false;    // the swapchain reported VK_ERROR_SURFACE_LOST_KHR
  uint64_t frames = 0, draws = 0;
} g;
const rsx_state *g_state = nullptr;

#define D2LOG(...) do { std::fprintf(stderr, __VA_ARGS__); } while (0)

#include "game_translate.inc"

void cb_init_unused() {}
int cb_init(void *, u32, u32) { return 0; }
void cb_shutdown(void *) {}
void cb_begin_frame(void *) {}
void cb_end_frame(void *) {}
void cb_present(void *, u32) {}
void cb_set_state(void *, const rsx_state *state) { g_state = state; }

void cb_clear(void *, u32 flags, u32 color, float, u8) {
  if (!(flags & 0xF0u))
    return;
  g.clear = {((color >> 16) & 0xFF) / 255.f, ((color >> 8) & 0xFF) / 255.f,
             (color & 0xFF) / 255.f, ((color >> 24) & 0xFF) / 255.f};
}

void translate(bool indexed, u32 prim, u32 first, u32 count) {
  g.draws++;
  if (!tr::ready || !g.renderer) return;
  try {
    tr::record(*g.renderer, g_state, indexed, prim, first, count);
  } catch (const std::exception &e) {
    tr::cnt.skipped_other++;
    tr::log_once("record", "draw skipped: %.300s", e.what());
  }
}
void cb_draw_arrays(void *, u32 prim, u32 first, u32 count) { translate(false, prim, first, count); }
void cb_draw_indexed(void *, u32 prim, u32 first, u32 count) { translate(true, prim, first, count); }
void cb_bind_texture(void *, u32, const rsx_texture_state *) {}

} // namespace

// The Activity calls this (through libd2android) when its surface is created (non-null) or destroyed (null).
extern "C" __attribute__((visibility("default"))) void d2_core_set_window(void *window) {
  std::lock_guard<std::mutex> lock(g_window_mutex);
  ANativeWindow *next = static_cast<ANativeWindow *>(window);
  if (next != g_window) g_window_gen++;
  if (g_window) ANativeWindow_release(g_window);
  g_window = next;
  if (g_window) ANativeWindow_acquire(g_window);
  g_window_cv.notify_all();
}

extern "C" {

int rsx_vulkan_backend_init(uint32_t width, uint32_t height, const char *) {
  if (g.initialized) return 0;
  try {
    {
      // The Activity may still be creating its surface: wait for it instead of failing the whole boot.
      std::unique_lock<std::mutex> lock(g_window_mutex);
      if (!g_window_cv.wait_for(lock, std::chrono::seconds(20), [] { return g_window != nullptr; })) {
        D2LOG("[vulkan] no window from the Activity after 20 s\n");
        return -1;
      }
      g.window = g_window;
      g.window_gen = g_window_gen;
      ANativeWindow_acquire(g.window);
    }
    g.renderer = std::make_unique<d2::vulkan::Renderer>();
    g.renderer->start(g.window, std::getenv("D2_VULKAN_VALIDATION") != nullptr);
    tr::init(*g.renderer);
    uint32_t w = (uint32_t)ANativeWindow_getWidth(g.window), h = (uint32_t)ANativeWindow_getHeight(g.window);
    if (w < 2 || h < 2) { w = width; h = height; }
    if (!g.renderer->resize(w, h, true)) throw std::runtime_error("initial swapchain creation failed");
    const auto info = g.renderer->info();
    D2LOG("[vulkan] adapter: %s (api 0x%x), surface %ux%u\n", info.adapter.c_str(), info.api_version, w, h);
    __android_log_print(ANDROID_LOG_INFO, "d2core", "Vulkan adapter: %s", info.adapter.c_str());
  } catch (const std::exception &e) {
    D2LOG("[vulkan] init failed: %s\n", e.what());
    __android_log_print(ANDROID_LOG_ERROR, "d2core", "Vulkan init failed: %s", e.what());
    if (g.renderer) g.renderer->close();
    g.renderer.reset();
    if (g.window) { ANativeWindow_release(g.window); g.window = nullptr; }
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
  if (!g.initialized) return;
  tr::print_counters();
  rsx_set_backend(nullptr);
  if (g.renderer) g.renderer->close();
  g.renderer.reset();
  if (g.window) { ANativeWindow_release(g.window); g.window = nullptr; }
  g.initialized = false;
}

int rsx_vulkan_backend_pump_messages(void) { return g.closed ? -1 : 0; }

static bool window_force_resize = false;

// User settings written by the Android app next to the log ("d2_android.cfg", the core's working directory):
//   scale=<25..200>   internal resolution, percent of the screen size (100 = native)
//   showfps=<0|1>     FPS counter in the top-left corner
struct AndroidSettings { unsigned scale_pct = 100; bool show_fps = false; };
static AndroidSettings load_android_settings() {
  AndroidSettings st;
  if (FILE *f = std::fopen("d2_android.cfg", "r")) {
    char line[128];
    while (std::fgets(line, sizeof line, f)) {
      unsigned v = 0;
      if (std::sscanf(line, "scale=%u", &v) == 1 && v >= 25 && v <= 200) st.scale_pct = v;
      else if (std::sscanf(line, "showfps=%u", &v) == 1) st.show_fps = v != 0;
    }
    std::fclose(f);
  }
  D2LOG("[vulkan] android settings: internal resolution %u%%, FPS counter %s\n", st.scale_pct, st.show_fps ? "on" : "off");
  return st;
}

// Presented frames per second (what the player sees), refreshed twice a second, drawn with the shared UI font.
static void draw_fps_counter(uint32_t w, uint32_t h) {
  static auto last = std::chrono::steady_clock::now();
  static unsigned count = 0, shown = 0;
  count++;
  const auto now = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double, std::milli>(now - last).count();
  if (elapsed >= 500.0) { shown = unsigned(count * 1000.0 / elapsed + 0.5); count = 0; last = now; }
  const float s = std::max(0.6f, float(h) / 720.f);
  char text[32];
  std::snprintf(text, sizeof text, "FPS %u", shown);
  using Color = std::array<float, 4>;
  const Color good{0.55f, 1.f, 0.65f, 1.f}, warn{1.f, 0.82f, 0.30f, 1.f}, ink{0.03f, 0.01f, 0.09f, 1.f};
  tr::ui_begin(w, h);
  tr::ui_text_outline(14.f * s, 10.f * s, text, 22.f * s, shown >= 28 ? good : warn, ink, 1.5f * s);
}

void rsx_vulkan_backend_present(void) {
  if (!g.initialized || g.closed || !g.renderer) return;
  try {
    // The app was minimised (surface destroyed) or restored (new surface): pause here, then rebuild the swapchain.
    {
      std::unique_lock<std::mutex> lock(g_window_mutex);
      if (g.surface_lost || g_window == nullptr || g_window_gen != g.window_gen) {
        if (!g_window || g.surface_lost) D2LOG("[vulkan] surface %s; pausing until the window returns\n", g_window ? "lost" : "destroyed");
        const auto t0 = std::chrono::steady_clock::now();
        while (!g.closed) {
          const bool fresh = g_window != nullptr && g_window_gen != g.window_gen;
          if (fresh) break;
          // A lost surface the Activity never reported: after 3 s try the window we still have.
          if (g.surface_lost && g_window != nullptr && std::chrono::steady_clock::now() - t0 > std::chrono::seconds(3)) break;
          g_window_cv.wait_for(lock, std::chrono::milliseconds(100));
        }
        if (g.closed || !g_window) return;
        ANativeWindow *next = g_window;
        const uint64_t gen = g_window_gen;
        ANativeWindow_acquire(next);
        lock.unlock();
        if (!g.renderer->replaceWindow(next)) {
          ANativeWindow_release(next);
          D2LOG("[vulkan] could not create a surface on the new window\n");
          g.closed = true;
          return;
        }
        if (g.window) ANativeWindow_release(g.window);
        g.window = next;
        g.window_gen = gen;
        g.surface_lost = false;
        D2LOG("[vulkan] surface recreated after %.1f s\n",
              std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        window_force_resize = true;
      }
    }
    uint32_t w = (uint32_t)ANativeWindow_getWidth(g.window), h = (uint32_t)ANativeWindow_getHeight(g.window);
    if (w < 2 || h < 2) return;
    static const AndroidSettings settings = load_android_settings();
    g.renderer->setRenderScale(float(settings.scale_pct) / 100.f); // internal resolution (marks a resize if changed)
    static uint32_t last_w = 0, last_h = 0;
    if (window_force_resize) { last_w = last_h = 0; window_force_resize = false; }
    if (g.renderer->needsResize() || w != last_w || h != last_h) {
      if (w != last_w || h != last_h) D2LOG("[vulkan] window size now %ux%u\n", w, h);
      last_w = w; last_h = h;
      if (!g.renderer->resize(w, h, true)) return;
    }
    uint32_t rw = w, rh = h;
    g.renderer->renderExtent(rw, rh);
    {
      static uint32_t logged_w = 0, logged_h = 0;
      if (rw != logged_w || rh != logged_h) { logged_w = rw; logged_h = rh; D2LOG("[vulkan] rendering at %ux%u (window %ux%u)\n", rw, rh, w, h); }
    }
    if (settings.show_fps) draw_fps_counter(rw, rh);
    const bool presented = tr::submit(*g.renderer, rw, rh, g.clear, tr::guest_w, tr::guest_h);
    if (presented) {
      g.frames++;
      if (g.frames == 300 || (g.frames % 1800) == 0) {
        D2LOG("[vulkan] frames=%llu draws=%llu\n", (unsigned long long)g.frames, (unsigned long long)g.draws);
        tr::print_counters();
      }
    }
  } catch (const std::exception &e) {
    D2LOG("[vulkan] present failed: %s\n", e.what());
    // VK_ERROR_SURFACE_LOST_KHR (-1000000000) is the app being minimised, not a reason to end the game.
    if (std::strstr(e.what(), "-1000000000")) { g.surface_lost = true; tr::drop_frame(); }
    else g.closed = true;
  }
}

} // extern "C"
#endif // __ANDROID__
