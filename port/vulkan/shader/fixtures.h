#pragma once
#include <string>
namespace d2::vulkan::shader::fixtures {
std::string vertex(bool texture = false, unsigned position_attribute = 0);
std::string fragment(bool texture);
std::string bufferedFragment();
} // namespace d2::vulkan::shader::fixtures
