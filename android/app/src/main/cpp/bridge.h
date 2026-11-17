// Calls shared by vulkan_surface.cpp (the Activity's surface and the milestone-0 colour renderer) and core_host.cpp
// (the recompiled game core).
#pragma once
#include <android/native_window.h>

// The Activity's window (we hold a reference), or nullptr.
ANativeWindow* d2_current_window();
// Stops the milestone-0 renderer so the game core can create its own swapchain on the same surface.
void d2_demo_stop();
// Hands the window (or nullptr) to the game core, if it is running.
void d2_core_forward_window(ANativeWindow* window);
// True once the core owns the surface.
bool d2_core_active();
void d2_core_forward_pad(int buttons, float lx, float ly, float rx, float ry);
