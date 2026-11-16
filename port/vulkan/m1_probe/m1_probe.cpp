// Standalone M1 check for the experimental Vulkan RSX backend.
// Links rsx_vulkan_backend.cpp + d2_vulkan WITHOUT the game: it plays the part
// of the RSX frame clock (init -> clear/draw callbacks -> present -> pump).
// Passing proves: window + swapchain + present + resize/close handling work on
// this GPU. It does not prove any game rendering.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "rsx_commands.h"

// The probe does not link rsx_commands.c, so provide the registry.
static rsx_backend *g_backend = nullptr;
extern "C" void rsx_set_backend(rsx_backend *b) { g_backend = b; }
extern "C" rsx_backend *rsx_get_backend(void) { return g_backend; }

extern "C" int rsx_vulkan_backend_init(uint32_t, uint32_t, const char *);
extern "C" void rsx_vulkan_backend_shutdown(void);
extern "C" int rsx_vulkan_backend_pump_messages(void);
extern "C" void rsx_vulkan_backend_present(void);

int main(int argc, char **argv) {
  unsigned frames = 600;       // ~10 s at 60 Hz VSync
  unsigned max_seconds = 60;   // hard stop if VSync never paces
  for (int i = 1; i + 1 < argc; i++) {
    if (!std::strcmp(argv[i], "--frames")) frames = (unsigned)std::atoi(argv[i + 1]);
    if (!std::strcmp(argv[i], "--seconds")) max_seconds = (unsigned)std::atoi(argv[i + 1]);
  }
  _putenv_s("D2_VULKAN_EXPERIMENTAL", "1");
  std::printf("[m1] init\n");
  if (rsx_vulkan_backend_init(1280, 720, "Disgaea D2 - Vulkan M1 probe") != 0) {
    std::printf("[m1] FAIL: backend init\n");
    return 1;
  }
  rsx_backend *b = rsx_get_backend();
  if (!b || !b->clear || !b->draw_arrays || !b->bind_texture) {
    std::printf("[m1] FAIL: backend not registered\n");
    return 1;
  }
  const auto t0 = std::chrono::steady_clock::now();
  unsigned n = 0;
  bool closed = false;
  while (n < frames) {
    // Animate the guest clear colour so the result is visible: ARGB.
    const uint32_t r = (n * 3) & 0xFF, g = (n * 2) & 0xFF, bl = 255 - ((n * 3) & 0xFF);
    b->clear(b->userdata, 0xF0, 0xFF000000u | (r << 16) | (g << 8) | bl, 1.f, 0);
    b->draw_arrays(b->userdata, 5, 0, 3);       // must be counted and ignored
    b->bind_texture(b->userdata, 0, nullptr);   // must be ignored
    rsx_vulkan_backend_present();
    if (rsx_vulkan_backend_pump_messages() != 0) { closed = true; break; }
    n++;
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > max_seconds) break;
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  rsx_vulkan_backend_shutdown();
  std::printf("[m1] %s after %u frames in %.2f s (%.1f fps)\n",
              closed ? "window closed by user" : "finished", n, secs, secs > 0 ? n / secs : 0.0);
  std::printf("[m1] PASS\n");
  return 0;
}
