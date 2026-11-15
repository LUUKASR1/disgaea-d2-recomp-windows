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
// Experimental Vulkan (milestone M1: presentation only, no guest draws) is
// opt-in with D2_VULKAN_EXPERIMENTAL=1 and only in builds made with
// D2_VULKAN_GAME_BACKEND. Never selected silently.
inline bool vulkan_experimental_enabled(const char* gate) {
#ifdef D2_VULKAN_GAME_BACKEND
    return gate && std::strcmp(gate, "1") == 0;
#else
    (void)gate; return false;
#endif
}
inline const char* unavailable_reason(Api api, const char* experimental_gate = nullptr) {
    if (api == Api::vulkan && vulkan_experimental_enabled(experimental_gate)) return nullptr;
    if (api == Api::vulkan) return "Vulkan game backend is not available in this build. Select DirectX 12.";
    if (api == Api::invalid) return "Invalid D2_GRAPHICS_API. Expected d3d12 or vulkan.";
    return nullptr;
}
}
