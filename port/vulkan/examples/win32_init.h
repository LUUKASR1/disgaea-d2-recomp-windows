#pragma once
// Independent C++20 Win32 initialization example. No game shader/assets.
// Build in an application with Vulkan headers and vulkan-1.lib (import
// library). The application's runtime uses the Vulkan loader supplied by its
// GPU driver.
#ifndef _WIN32
#error This example targets Win32.
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
// clang-format off: keep native platform types before Vulkan declarations.
#include <windows.h>
#include <vulkan/vulkan.h>
// clang-format on
namespace d2::examples {
inline void Check(VkResult result, const char *operation) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(std::string(operation) + ": " +
                             std::to_string(result));
}
// Bounded retry handles VK_INCOMPLETE when enumeration sizes change.
template <class T, class F> std::vector<T> Enumerate(F call) {
  for (unsigned attempt = 0; attempt < 4; attempt++) {
    uint32_t count = 0;
    Check(call(&count, nullptr), "Vulkan enumeration count");
    std::vector<T> result(count);
    if (!count)
      return result;
    auto status = call(&count, result.data());
    if (status == VK_INCOMPLETE)
      continue;
    Check(status, "Vulkan enumeration data");
    result.resize(count);
    return result;
  }
  throw std::runtime_error("Vulkan enumeration did not stabilize");
}
inline VkSurfaceKHR CreateWin32Surface(VkInstance instance, HWND hwnd,
                                       HINSTANCE hinstance) {
  if (!hwnd || !hinstance)
    throw std::invalid_argument("Invalid HWND/HINSTANCE");
  VkWin32SurfaceCreateInfoKHR info{
      VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
  info.hwnd = hwnd;
  info.hinstance = hinstance;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  Check(vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface),
        "vkCreateWin32SurfaceKHR");
  return surface;
}
class Win32VulkanContext {
public:
  VkInstance instance = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE; // borrowed from instance
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphicsQueue = VK_NULL_HANDLE, presentQueue = VK_NULL_HANDLE;
  uint32_t queueFamily = std::numeric_limits<uint32_t>::max();

  Win32VulkanContext() = default;
  Win32VulkanContext(const Win32VulkanContext &) = delete;
  Win32VulkanContext &operator=(const Win32VulkanContext &) = delete;
  ~Win32VulkanContext() { Shutdown(); }

  void Initialize(HWND hwnd, HINSTANCE hinstance) {
    if (instance)
      throw std::logic_error("Vulkan already initialized");
    try {
      const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                  VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
      auto available =
          Enumerate<VkExtensionProperties>([](auto count, auto data) {
            return vkEnumerateInstanceExtensionProperties(nullptr, count, data);
          });
      for (auto required : extensions)
        if (std::none_of(available.begin(), available.end(), [&](auto &e) {
              return std::strcmp(e.extensionName, required) == 0;
            }))
          throw std::runtime_error(
              std::string("Required instance extension missing: ") + required);
      VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
      application.pApplicationName = "Disgaea D2 Vulkan initialization example";
      application.apiVersion = VK_API_VERSION_1_1;
      VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
      create.pApplicationInfo = &application;
      create.enabledExtensionCount = 2;
      create.ppEnabledExtensionNames = extensions;
      Check(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance");
      surface = CreateWin32Surface(instance, hwnd, hinstance);
      auto adapters = Enumerate<VkPhysicalDevice>([&](auto count, auto data) {
        return vkEnumeratePhysicalDevices(instance, count, data);
      });
      int best = -1;
      for (auto candidate : adapters) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_1)
          continue;
        auto deviceExtensions =
            Enumerate<VkExtensionProperties>([&](auto count, auto data) {
              return vkEnumerateDeviceExtensionProperties(candidate, nullptr,
                                                          count, data);
            });
        if (std::none_of(
                deviceExtensions.begin(), deviceExtensions.end(), [](auto &e) {
                  return std::strcmp(e.extensionName,
                                     VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                }))
          continue;
        auto formats =
            Enumerate<VkSurfaceFormatKHR>([&](auto count, auto data) {
              return vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface,
                                                          count, data);
            });
        auto modes = Enumerate<VkPresentModeKHR>([&](auto count, auto data) {
          return vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface,
                                                           count, data);
        });
        if (formats.empty() || modes.empty())
          continue;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count,
                                                 families.data());
        for (uint32_t i = 0; i < count; i++) {
          VkBool32 present = VK_FALSE;
          Check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface,
                                                     &present),
                "vkGetPhysicalDeviceSurfaceSupportKHR");
          // This small example requires a common graphics/present queue family.
          if (!families[i].queueCount ||
              !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present)
            continue;
          int score =
              properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
              : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
                  ? 2
                  : 1;
          if (score > best) {
            best = score;
            physicalDevice = candidate;
            queueFamily = i;
          }
          break;
        }
      }
      if (!physicalDevice)
        throw std::runtime_error("No suitable Vulkan graphics/present adapter");
      float priority = 1.f;
      VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
      queue.queueFamilyIndex = queueFamily;
      queue.queueCount = 1;
      queue.pQueuePriorities = &priority;
      const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
      VkDeviceCreateInfo createDevice{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
      createDevice.queueCreateInfoCount = 1;
      createDevice.pQueueCreateInfos = &queue;
      createDevice.enabledExtensionCount = 1;
      createDevice.ppEnabledExtensionNames = deviceExtensions;
      Check(vkCreateDevice(physicalDevice, &createDevice, nullptr, &device),
            "vkCreateDevice");
      vkGetDeviceQueue(device, queueFamily, 0, &graphicsQueue);
      presentQueue = graphicsQueue;
    } catch (...) {
      Shutdown();
      throw;
    }
  }
  // Destroy caller-owned swapchains, pools, pipelines etc. BEFORE this context.
  // Keep the HWND alive until surface/swapchain destruction has completed.
  void Shutdown() noexcept {
    if (device) {
      vkDeviceWaitIdle(device);
      vkDestroyDevice(device, nullptr);
    }
    if (surface)
      vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance)
      vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE;
    surface = VK_NULL_HANDLE;
    instance = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    graphicsQueue = presentQueue = VK_NULL_HANDLE;
    queueFamily = std::numeric_limits<uint32_t>::max();
  }
};
} // namespace d2::examples
