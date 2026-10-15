#include "../../graphics/backend_choice.h"
#include <stdexcept>
int main() {
  using namespace d2::graphics;
  auto check = [](bool value) {
    if (!value)
      throw std::runtime_error("Backend selection failure");
  };
  check(requested_api(nullptr) == Api::d3d12);
  check(requested_api("d3d12") == Api::d3d12);
  check(unavailable_reason(Api::d3d12) == nullptr);
  check(requested_api("vulkan") == Api::vulkan);
  check(unavailable_reason(Api::vulkan) != nullptr);
  for (auto invalid : {"", "opengl", "Vulkan", "d3d12 "}) {
    check(requested_api(invalid) == Api::invalid);
    check(unavailable_reason(requested_api(invalid)) != nullptr);
  }
}
