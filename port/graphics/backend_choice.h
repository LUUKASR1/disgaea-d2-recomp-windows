#pragma once
#include <cstring>

namespace d2::graphics {
enum class Api { d3d12, vulkan, invalid };
inline Api requested_api(const char* value) {
    if (!value || std::strcmp(value, "d3d12") == 0) return Api::d3d12;
    if (std::strcmp(value, "vulkan") == 0) return Api::vulkan;
    return Api::invalid;
}
// Runtime capability, not a driver capability. The standalone Vulkan probe
// does not implement the game's RSX backend and must never imply support.
inline const char* unavailable_reason(Api api) {
    if (api == Api::vulkan) return "Vulkan game backend is not implemented. Select DirectX 12.";
    if (api == Api::invalid) return "Invalid D2_GRAPHICS_API. Expected d3d12 or vulkan.";
    return nullptr;
}
}
