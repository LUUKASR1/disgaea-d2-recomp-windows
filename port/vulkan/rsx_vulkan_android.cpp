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

struct State {
  ANativeWindow *window = nullptr;
  std::unique_ptr<d2::vulkan::Renderer> renderer;
  rsx_backend backend{};
  std::array<float, 4> clear{0.f, 0.f, 0.f, 1.f};
  bool closed = false;
  bool initialized = false;
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
  if (g_window) ANativeWindow_release(g_window);
  g_window = static_cast<ANativeWindow *>(window);
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

void rsx_vulkan_backend_present(void) {
  if (!g.initialized || g.closed || !g.renderer) return;
  try {
    uint32_t w = (uint32_t)ANativeWindow_getWidth(g.window), h = (uint32_t)ANativeWindow_getHeight(g.window);
    if (w < 2 || h < 2) return;
    if (g.renderer->needsResize()) {
      if (!g.renderer->resize(w, h, true)) return;
    }
    uint32_t rw = w, rh = h;
    g.renderer->renderExtent(rw, rh);
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
    g.closed = true;
  }
}

} // extern "C"
#endif // __ANDROID__
