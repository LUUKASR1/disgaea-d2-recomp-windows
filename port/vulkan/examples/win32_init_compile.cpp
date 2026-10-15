#include "win32_init.h"
// Compile-only target: no HWND is created or GPU test claimed by this file.
void CompileWin32InitializationExample(HWND hwnd, HINSTANCE hinstance) {
  d2::examples::Win32VulkanContext context;
  context.Initialize(hwnd, hinstance);
}
