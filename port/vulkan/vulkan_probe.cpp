// Experimental Vulkan window and GPU readback tests; does not run game
// commands.
#ifdef _WIN32
#include <windows.h>
#endif
#include "vulkan_renderer.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
using d2::vulkan::Renderer;
using d2::vulkan::Unavailable;
namespace {
unsigned verifyTexture(Renderer &renderer) {
  auto pixels = renderer.readPixels();
  for (unsigned y = 0; y < 96; y++)
    for (unsigned x = 0; x < 128; x++) {
      unsigned quadrant = (y >= 48 ? 2 : 0) + (x >= 64 ? 1 : 0);
      const std::array<int, 4> colors[] = {{{255, 0, 0, 255}},
                                           {{0, 255, 0, 255}},
                                           {{0, 0, 255, 255}},
                                           {{128, 0, 127, 255}}};
      for (unsigned c = 0; c < 4; c++)
        if (std::abs(int(pixels[(y * 128 + x) * 4 + c]) - colors[quadrant][c]) >
            1)
          throw std::runtime_error("Texture GPU readback mismatch");
    }
  return 128 * 96;
}
void texturedQuad(Renderer &renderer) {
  const d2::vulkan::Vertex vertices[] = {
      {{-1, -1, .5f, 1}, {1, 1, 1, 1}, {0, 0}},
      {{1, -1, .5f, 1}, {1, 1, 1, 1}, {1, 0}},
      {{1, 1, .5f, 1}, {1, 1, 1, 1}, {1, 1}},
      {{-1, 1, .5f, 1}, {1, 1, 1, 1}, {0, 1}}};
  const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
  const uint8_t rgba[] = {255, 0, 0,   255, 0,   255, 0, 255,
                          255, 0, 255, 0,   255, 0,   0, 128};
  renderer.setGeometry(vertices, indices);
  renderer.setTexture(2, 2, rgba);
  d2::vulkan::DrawParameters p;
  p.clear_color = {0, 0, 1, 1};
  renderer.setParameters(p);
}
bool resized = false, closeRequested = false, wasMinimized = false;
unsigned sizeEvents = 0, minimizeEvents = 0, restoreEvents = 0;
std::string jsonString(const std::string &s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += (char)c;
    } else if (c < 32)
      out += ' ';
    else
      out += (char)c;
  }
  return out + '"';
}
#ifdef _WIN32
LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  // Keep HWND alive until Vulkan has released the surface and swapchain.
  if (message == WM_SIZE) {
    resized = true;
    sizeEvents++;
    if (w == SIZE_MINIMIZED) {
      minimizeEvents++;
      wasMinimized = true;
    } else if (wasMinimized) {
      restoreEvents++;
      wasMinimized = false;
    }
    return 0;
  }
  if (message == WM_CLOSE) {
    closeRequested = true;
    PostQuitMessage(0);
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, message, w, l);
}
#endif
} // namespace
int main(int argc, char **argv) {
  bool selfTest = false, validation = false, vsync = true;
  unsigned limit = 0; // Default: remain open until the user closes the window.
  std::string report = "DisgaeaD2-Vulkan-report.json";
  Renderer renderer;
  unsigned frames = 0, colored = 0;
  int code = 0;
  std::string status = "passed", error;
#ifdef _WIN32
  HWND window = nullptr;
#endif
  try {
    for (int i = 1; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--self-test")
        selfTest = true;
      else if (arg == "--validation")
        validation = true;
      else if (arg == "--no-vsync")
        vsync = false;
      else if (arg == "--report" && i + 1 < argc)
        report = argv[++i];
      else if (arg == "--frames" && i + 1 < argc) {
        limit = (unsigned)std::stoul(argv[++i]);
        if (!limit || limit > 100000)
          throw std::runtime_error("Invalid frame count");
      } else
        throw std::runtime_error("Unknown command-line argument");
    }
#ifndef _WIN32
    selfTest = true;
#else
    if (!selfTest) {
      WNDCLASSW wc{};
      wc.lpfnWndProc = windowProc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.lpszClassName = L"D2VulkanProbe";
      wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
      if (!RegisterClassW(&wc))
        throw std::runtime_error("Win32 class registration failed");
      RECT bounds{0, 0, 1280, 720};
      AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
      window = CreateWindowExW(
          0, wc.lpszClassName,
          L"Disgaea D2 - base Vulkan experimental (nao executa o jogo)",
          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
          bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr,
          nullptr, wc.hInstance, nullptr);
      if (!window)
        throw std::runtime_error("Win32 window creation failed");
    }
#endif
    void *native = nullptr;
#ifdef _WIN32
    native = window;
#endif
    renderer.start(native, validation);
    texturedQuad(renderer);
    if (selfTest) {
      renderer.offscreen(128, 96);
      for (unsigned i = 0; i < 4; i++) {
        renderer.draw();
        colored = verifyTexture(renderer);
        frames++;
      }
    }
#ifdef _WIN32
    else {
      ShowWindow(window, SW_SHOW);
      renderer.resize(1280, 720, vsync);
      bool quit = false;
      while (!quit && (!limit || frames < limit)) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
          if (message.message == WM_QUIT)
            quit = true;
          TranslateMessage(&message);
          DispatchMessageW(&message);
        }
        if (quit)
          break;
        RECT rect{};
        GetClientRect(window, &rect);
        if (!rect.right || !rect.bottom) {
          WaitMessage();
          continue;
        }
        if (resized || renderer.needsResize()) {
          if (!renderer.resize((uint32_t)rect.right, (uint32_t)rect.bottom,
                               vsync))
            continue;
          resized = false;
        }
        if (renderer.draw())
          frames++;
      }
    }
#endif
  } catch (const Unavailable &e) {
    status = "unavailable";
    error = e.what();
    code = 77;
  } catch (const std::exception &e) {
    status = "failed";
    error = e.what();
    code = 1;
  }
  renderer.close();
  const auto info = renderer.info();
  if (info.validation_errors) {
    status = "failed";
    error = "Vulkan validation errors";
    code = 1;
  }
#ifdef _WIN32
  if (window && IsWindow(window))
    DestroyWindow(window);
#endif
  std::ofstream file(report);
  if (!file) {
    std::cerr << "Could not write Vulkan report\n";
    return 1;
  }
  file << "{\"schema\":3,\"renderer_stage\":\"rgba-textures\","
          "\"validation_enabled\":"
       << (validation ? "true" : "false")
       << ",\"purpose\":\"native-vulkan-foundation\",\"game_"
          "backend_ready\":false,\"status\":"
       << jsonString(status) << ",\"adapter\":" << jsonString(info.adapter)
       << ",\"device_type\":" << info.device_type
       << ",\"api_version\":" << info.api_version
       << ",\"headless\":" << (selfTest ? "true" : "false")
       << ",\"frames\":" << frames << ",\"colored_readback_pixels\":" << colored
       << ",\"validation_errors\":" << info.validation_errors
       << ",\"present_mode\":" << info.present_mode
       << ",\"scene\":\"rgba-quadrants\",\"wm_size_events\":" << sizeEvents
       << ",\"minimize_events\":" << minimizeEvents
       << ",\"restore_events\":" << restoreEvents
       << ",\"wm_close_received\":" << (closeRequested ? "true" : "false")
       << ",\"error\":" << jsonString(error) << "}\n";
  std::cout << status << ": " << info.adapter << " frames=" << frames
            << " report=" << report << '\n';
  if (!error.empty())
    std::cerr << error << '\n';
  return code;
}
