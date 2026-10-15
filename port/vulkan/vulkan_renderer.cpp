// Experimental native Vulkan foundation. Does not execute game/RSX commands.
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "triangle_spv.h"
#include "vulkan_renderer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
namespace d2::vulkan {
void check(VkResult r, const char *call) {
  if (r != VK_SUCCESS)
    throw std::runtime_error(std::string(call) +
                             ": VkResult=" + std::to_string(r));
}
#define OK(x) check((x), #x)
#define INSTANCE_FUNCTIONS(X)                                                  \
  X(vkDestroyInstance)                                                         \
  X(vkEnumeratePhysicalDevices)                                                \
  X(vkGetPhysicalDeviceProperties)                                             \
  X(vkGetPhysicalDeviceQueueFamilyProperties)                                  \
      X(vkGetPhysicalDeviceMemoryProperties)                                   \
          X(vkEnumerateDeviceExtensionProperties) X(vkCreateDevice)            \
              X(vkGetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X)                                                     \
  X(vkDestroyDevice)                                                            \
  X(vkGetDeviceQueue)                                                           \
  X(vkDeviceWaitIdle)                                                           \
  X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X( \
      vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer)       \
      X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X( \
          vkQueueSubmit) X(vkCreateSemaphore) X(vkDestroySemaphore)             \
          X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X( \
              vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory)            \
              X(vkCreateImageView) X(vkDestroyImageView) X(                     \
                  vkCreateRenderPass) X(vkDestroyRenderPass)                    \
                  X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(             \
                      vkCreateShaderModule) X(vkDestroyShaderModule)            \
                      X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(   \
                          vkCreateGraphicsPipelines) X(vkDestroyPipeline)       \
                          X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(      \
                              vkCmdBindPipeline) X(vkCmdSetViewport)            \
                              X(vkCmdSetScissor) X(vkCmdDraw) X(                \
                                  vkCreateBuffer) X(vkDestroyBuffer)            \
                                  X(vkGetBufferMemoryRequirements) X(           \
                                      vkBindBufferMemory)                       \
                                      X(vkCmdCopyImageToBuffer) X(              \
                                          vkCmdPipelineBarrier) X(vkMapMemory)  \
                                          X(vkUnmapMemory) X(                   \
                                              vkInvalidateMappedMemoryRanges)   \
                                              X(vkFlushMappedMemoryRanges)      \
                                                  X(vkCmdBindVertexBuffers)     \
                                                      X(vkCmdBindIndexBuffer)   \
                                                          X(vkCmdDrawIndexed)
#define SURFACE_FUNCTIONS(X)                                                   \
  X(vkGetPhysicalDeviceSurfaceSupportKHR)                                      \
  X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                 \
  X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                      \
  X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkDestroySurfaceKHR)
#define SWAP_FUNCTIONS(X)                                                      \
  X(vkCreateSwapchainKHR)                                                      \
  X(vkDestroySwapchainKHR)                                                     \
  X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR)
class Renderer::Impl {
#ifdef _WIN32
  HMODULE library = nullptr;
#else
  void *library = nullptr;
#endif
  PFN_vkGetInstanceProcAddr gipa = nullptr;
  PFN_vkCreateInstance vkCreateInstance = nullptr;
#define DECLARE(name) PFN_##name name = nullptr;
  INSTANCE_FUNCTIONS(DECLARE)
  DEVICE_FUNCTIONS(DECLARE)
  SURFACE_FUNCTIONS(DECLARE)
  SWAP_FUNCTIONS(DECLARE) DECLARE(vkCreateDebugUtilsMessengerEXT)
      DECLARE(vkDestroyDebugUtilsMessengerEXT)
#ifdef _WIN32
          DECLARE(vkCreateWin32SurfaceKHR)
#endif
#undef DECLARE
              VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t family = 0;
  VkPhysicalDeviceMemoryProperties memory{};
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkSemaphore acquire = VK_NULL_HANDLE;
  std::vector<VkSemaphore> finished;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkExtent2D extent{};
  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> framebuffers;
  VkImage owned_image = VK_NULL_HANDLE;
  VkDeviceMemory image_memory = VK_NULL_HANDLE;
  VkRenderPass pass = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
  VkBuffer readback = VK_NULL_HANDLE;
  VkDeviceMemory readback_memory = VK_NULL_HANDLE;
  VkBuffer geometry = VK_NULL_HANDLE;
  VkDeviceMemory geometry_memory = VK_NULL_HANDLE;
  VkDeviceSize index_offset = 0;
  uint32_t vertex_count = 0, index_count = 0;
  DrawParameters parameters{};
  bool started = false;
  template <class T> void load(T &fn, const char *name) {
    fn = reinterpret_cast<T>(gipa(instance, name));
    if (!fn)
      throw std::runtime_error(std::string("Missing Vulkan entry: ") + name);
  }
  template <class T> void loadDevice(T &fn, const char *name) {
    fn = reinterpret_cast<T>(vkGetDeviceProcAddr(device, name));
    if (!fn)
      throw std::runtime_error(std::string("Missing device entry: ") + name);
  }
  static VKAPI_ATTR VkBool32 VKAPI_CALL
  validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
             VkDebugUtilsMessageTypeFlagsEXT,
             const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
      static_cast<Impl *>(user)->validation_errors++;
    std::cerr << "[Vulkan validation] "
              << (data && data->pMessage ? data->pMessage : "") << '\n';
    return VK_FALSE;
  }
  uint32_t memoryType(uint32_t allowed, VkMemoryPropertyFlags required) {
    for (uint32_t i = 0; i < memory.memoryTypeCount; i++)
      if ((allowed & (1u << i)) &&
          (memory.memoryTypes[i].propertyFlags & required) == required)
        return i;
    throw std::runtime_error("Required Vulkan memory type unavailable");
  }
  void makePipeline() {
    VkShaderModuleCreateInfo shader{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize = sizeof(triangle_vert);
    shader.pCode = triangle_vert;
    OK(vkCreateShaderModule(device, &shader, nullptr, &vert));
    shader.codeSize = sizeof(triangle_frag);
    shader.pCode = triangle_frag;
    OK(vkCreateShaderModule(device, &shader, nullptr, &frag));
    VkPipelineLayoutCreateInfo lc{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    OK(vkCreatePipelineLayout(device, &lc, nullptr, &layout));
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto &s : stages) {
      s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      s.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    VkPipelineVertexInputStateCreateInfo vertex{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkVertexInputBindingDescription binding{0, sizeof(Vertex),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)}};
    vertex.vertexBindingDescriptionCount = 1;
    vertex.pVertexBindingDescriptions = &binding;
    vertex.vertexAttributeDescriptionCount = 2;
    vertex.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                 VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    VkGraphicsPipelineCreateInfo pc{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pc.stageCount = 2;
    pc.pStages = stages;
    pc.pVertexInputState = &vertex;
    pc.pInputAssemblyState = &assembly;
    pc.pViewportState = &vp;
    pc.pRasterizationState = &raster;
    pc.pMultisampleState = &ms;
    pc.pColorBlendState = &blend;
    pc.pDynamicState = &dynamic;
    pc.layout = layout;
    pc.renderPass = pass;
    OK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pc, nullptr,
                                 &pipeline));
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    vert = frag = VK_NULL_HANDLE;
  }
  void makeTargets() {
    VkAttachmentDescription color{};
    color.format = format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = surface ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = deps[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = surface ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                                   : VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].dstAccessMask = surface ? 0 : VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 2;
    rp.pDependencies = deps;
    OK(vkCreateRenderPass(device, &rp, nullptr, &pass));
    makePipeline();
    for (VkImage image : images) {
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = image;
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = format;
      vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VkImageView view = VK_NULL_HANDLE;
      OK(vkCreateImageView(device, &vi, nullptr, &view));
      views.push_back(view);
      VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fb.renderPass = pass;
      fb.attachmentCount = 1;
      fb.pAttachments = &views.back();
      fb.width = extent.width;
      fb.height = extent.height;
      fb.layers = 1;
      VkFramebuffer framebuffer = VK_NULL_HANDLE;
      OK(vkCreateFramebuffer(device, &fb, nullptr, &framebuffer));
      framebuffers.push_back(framebuffer);
      if (surface) {
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        OK(vkCreateSemaphore(device, &si, nullptr, &semaphore));
        finished.push_back(semaphore);
      }
    }
  }
  void releaseTargets() {
    for (auto f : framebuffers)
      vkDestroyFramebuffer(device, f, nullptr);
    framebuffers.clear();
    for (auto v : views)
      vkDestroyImageView(device, v, nullptr);
    views.clear();
    for (auto s : finished)
      vkDestroySemaphore(device, s, nullptr);
    finished.clear();
    if (pipeline)
      vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
    if (layout)
      vkDestroyPipelineLayout(device, layout, nullptr);
    layout = VK_NULL_HANDLE;
    if (pass)
      vkDestroyRenderPass(device, pass, nullptr);
    pass = VK_NULL_HANDLE;
    images.clear();
    if (swapchain)
      vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
  }

public:
  std::atomic<unsigned> validation_errors{0};
  std::string adapter;
  uint32_t api_version = 0, device_type = 0, present_mode = 0;
  bool dirty = false;
  ~Impl() { close(); }
  void close() noexcept {
    if (device && vkDeviceWaitIdle) {
      vkDeviceWaitIdle(device);
      releaseTargets();
      if (geometry)
        vkDestroyBuffer(device, geometry, nullptr);
      if (geometry_memory)
        vkFreeMemory(device, geometry_memory, nullptr);
      if (vert)
        vkDestroyShaderModule(device, vert, nullptr);
      if (frag)
        vkDestroyShaderModule(device, frag, nullptr);
      if (readback)
        vkDestroyBuffer(device, readback, nullptr);
      if (readback_memory)
        vkFreeMemory(device, readback_memory, nullptr);
      if (owned_image)
        vkDestroyImage(device, owned_image, nullptr);
      if (image_memory)
        vkFreeMemory(device, image_memory, nullptr);
      if (acquire)
        vkDestroySemaphore(device, acquire, nullptr);
      if (fence)
        vkDestroyFence(device, fence, nullptr);
      if (pool)
        vkDestroyCommandPool(device, pool, nullptr);
      vkDestroyDevice(device, nullptr);
      device = VK_NULL_HANDLE;
    }
    if (surface && vkDestroySurfaceKHR)
      vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
    if (debug && vkDestroyDebugUtilsMessengerEXT)
      vkDestroyDebugUtilsMessengerEXT(instance, debug, nullptr);
    debug = VK_NULL_HANDLE;
    if (instance && vkDestroyInstance)
      vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
#ifdef _WIN32
    if (library)
      FreeLibrary(library);
#else
    if (library)
      dlclose(library);
#endif
    library = nullptr;
  }
  void start(void *window, bool useValidation) {
    if (started)
      throw std::runtime_error("Renderer instances support one initialization");
    started = true;
#ifdef _WIN32
    library =
        LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library)
      throw Unavailable("Vulkan loader not found in Windows system directory");
    gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(library, "vkGetInstanceProcAddr"));
#else
    library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library)
      throw Unavailable("Vulkan loader unavailable");
    gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(library, "vkGetInstanceProcAddr"));
#endif
    if (!gipa)
      throw Unavailable("vkGetInstanceProcAddr unavailable");
    auto version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        gipa(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    uint32_t supported = VK_API_VERSION_1_0;
    if (version)
      OK(version(&supported));
    if (supported < VK_API_VERSION_1_1)
      throw Unavailable("Vulkan 1.1 required");
    std::vector<const char *> extensions, layers;
#ifdef _WIN32
    if (window) {
      extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
      extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    }
#else
    if (window)
      throw std::runtime_error(
          "Window presentation currently implemented only on Win32");
#endif
    if (useValidation) {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Disgaea D2 Vulkan foundation";
    app.applicationVersion = 1;
    app.apiVersion = VK_API_VERSION_1_1;
    VkDebugUtilsMessengerCreateInfoEXT dc{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    dc.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    dc.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dc.pfnUserCallback = validation;
    dc.pUserData = this;
    VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ic.pApplicationInfo = &app;
    ic.enabledExtensionCount = (uint32_t)extensions.size();
    ic.ppEnabledExtensionNames = extensions.data();
    ic.enabledLayerCount = (uint32_t)layers.size();
    ic.ppEnabledLayerNames = layers.data();
    if (useValidation)
      ic.pNext = &dc;
    load(vkCreateInstance, "vkCreateInstance");
    VkResult created = vkCreateInstance(&ic, nullptr, &instance);
    if (created == VK_ERROR_INCOMPATIBLE_DRIVER)
      throw Unavailable("No compatible Vulkan driver");
    check(created, "vkCreateInstance");
#define LOAD(name) load(name, #name);
    INSTANCE_FUNCTIONS(LOAD)
    if (useValidation) {
      LOAD(vkCreateDebugUtilsMessengerEXT)
      LOAD(vkDestroyDebugUtilsMessengerEXT)
      OK(vkCreateDebugUtilsMessengerEXT(instance, &dc, nullptr, &debug));
    }
    if (window) {
      SURFACE_FUNCTIONS(LOAD)
#ifdef _WIN32
      LOAD(vkCreateWin32SurfaceKHR)
      VkWin32SurfaceCreateInfoKHR si{
          VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
      si.hinstance = GetModuleHandleW(nullptr);
      si.hwnd = (HWND)window;
      OK(vkCreateWin32SurfaceKHR(instance, &si, nullptr, &surface));
#endif
    }
#undef LOAD
    uint32_t count = 0;
    OK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count)
      throw Unavailable("No Vulkan physical device");
    std::vector<VkPhysicalDevice> devices(count);
    OK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    int best = -1;
    for (auto candidate : devices) {
      VkPhysicalDeviceProperties properties{};
      vkGetPhysicalDeviceProperties(candidate, &properties);
      if (properties.apiVersion < VK_API_VERSION_1_1)
        continue;
      if (surface) {
        uint32_t n = 0;
        OK(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &n,
                                                nullptr));
        std::vector<VkExtensionProperties> ex(n);
        OK(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &n,
                                                ex.data()));
        if (std::none_of(ex.begin(), ex.end(), [](const auto &e) {
              return !std::strcmp(e.extensionName,
                                  VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            }))
          continue;
      }
      uint32_t n = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
      std::vector<VkQueueFamilyProperties> families(n);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, families.data());
      for (uint32_t i = 0; i < n; i++)
        if (families[i].queueCount &&
            (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
          VkBool32 present = VK_TRUE;
          if (surface)
            OK(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface,
                                                    &present));
          if (!present)
            continue;
          int score =
              properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
              : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
                  ? 2
                  : 1;
          if (score > best) {
            best = score;
            physical = candidate;
            family = i;
            adapter = properties.deviceName;
            api_version = properties.apiVersion;
            device_type = properties.deviceType;
          }
          break;
        }
    }
    if (!physical)
      throw Unavailable("No suitable Vulkan graphics/present queue");
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    float priority = 1;
    VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qc.queueFamilyIndex = family;
    qc.queueCount = 1;
    qc.pQueuePriorities = &priority;
    const char *swap = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo dev{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dev.queueCreateInfoCount = 1;
    dev.pQueueCreateInfos = &qc;
    if (surface) {
      dev.enabledExtensionCount = 1;
      dev.ppEnabledExtensionNames = &swap;
    }
    OK(vkCreateDevice(physical, &dev, nullptr, &device));
#define LOAD(name) loadDevice(name, #name);
    DEVICE_FUNCTIONS(LOAD)
    if (surface) {
      SWAP_FUNCTIONS(LOAD)
    }
#undef LOAD
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pc.queueFamilyIndex = family;
    pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    OK(vkCreateCommandPool(device, &pc, nullptr, &pool));
    VkCommandBufferAllocateInfo ac{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ac.commandPool = pool;
    ac.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ac.commandBufferCount = 1;
    OK(vkAllocateCommandBuffers(device, &ac, &cmd));
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    OK(vkCreateFence(device, &fc, nullptr, &fence));
    if (surface) {
      VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      OK(vkCreateSemaphore(device, &sc, nullptr, &acquire));
    }
  }
  void offscreen(uint32_t w, uint32_t h) {
    if (!device || owned_image || surface || !w || !h || w > 4096 || h > 4096)
      throw std::runtime_error("Invalid offscreen target request");
    extent = {w, h};
    format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = format;
    ic.extent = {w, h, 1};
    ic.mipLevels = ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    OK(vkCreateImage(device, &ic, nullptr, &owned_image));
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, owned_image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    OK(vkAllocateMemory(device, &ai, nullptr, &image_memory));
    OK(vkBindImageMemory(device, owned_image, image_memory, 0));
    images.push_back(owned_image);
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = (VkDeviceSize)w * h * 4;
    bc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    OK(vkCreateBuffer(device, &bc, nullptr, &readback));
    vkGetBufferMemoryRequirements(device, readback, &req);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    OK(vkAllocateMemory(device, &ai, nullptr, &readback_memory));
    OK(vkBindBufferMemory(device, readback, readback_memory, 0));
    makeTargets();
  }
  bool resize(uint32_t w, uint32_t h, bool vsync) {
    if (!w || !h)
      return false;
    OK(vkDeviceWaitIdle(device));
    releaseTargets();
    VkSurfaceCapabilitiesKHR caps{};
    OK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
      throw std::runtime_error("Surface cannot render color attachments");
    extent = caps.currentExtent.width != UINT32_MAX
                 ? caps.currentExtent
                 : VkExtent2D{std::clamp(w, caps.minImageExtent.width,
                                         caps.maxImageExtent.width),
                              std::clamp(h, caps.minImageExtent.height,
                                         caps.maxImageExtent.height)};
    if (!extent.width || !extent.height)
      return false;
    uint32_t count = 0;
    OK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count,
                                            nullptr));
    if (!count)
      throw std::runtime_error("Surface formats unavailable");
    std::vector<VkSurfaceFormatKHR> formats(count);
    OK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count,
                                            formats.data()));
    auto selected = formats[0];
    for (auto f : formats)
      if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
          f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        selected = f;
    if (selected.format == VK_FORMAT_UNDEFINED)
      selected.format = VK_FORMAT_B8G8R8A8_UNORM;
    format = selected.format;
    OK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count,
                                                 nullptr));
    std::vector<VkPresentModeKHR> modes(count);
    OK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count,
                                                 modes.data()));
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
      if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) !=
          modes.end())
        mode = VK_PRESENT_MODE_MAILBOX_KHR;
      else if (std::find(modes.begin(), modes.end(),
                         VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
        mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    present_mode = (uint32_t)mode;
    uint32_t images_count = caps.minImageCount + 1;
    if (caps.maxImageCount)
      images_count = std::min(images_count, caps.maxImageCount);
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha)) {
      for (unsigned bit = 0; bit < 4; bit++)
        if (caps.supportedCompositeAlpha & (1u << bit)) {
          alpha = (VkCompositeAlphaFlagBitsKHR)(1u << bit);
          break;
        }
    }
    VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sc.surface = surface;
    sc.minImageCount = images_count;
    sc.imageFormat = format;
    sc.imageColorSpace = selected.colorSpace;
    sc.imageExtent = extent;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = alpha;
    sc.presentMode = mode;
    sc.clipped = VK_TRUE;
    OK(vkCreateSwapchainKHR(device, &sc, nullptr, &swapchain));
    OK(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr));
    images.resize(count);
    OK(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()));
    makeTargets();
    dirty = false;
    return true;
  }
  void setParameters(const DrawParameters &value) {
    for (float c : value.clear_color)
      if (!std::isfinite(c))
        throw std::runtime_error("Nonfinite clear color");
    parameters = value;
  }
  void setGeometry(std::span<const Vertex> vertices,
                   std::span<const uint32_t> indices) {
    if (!device || vertices.size() > 1048576 || indices.size() > 3145728 ||
        (indices.empty() ? vertices.size() % 3 : indices.size() % 3))
      throw std::runtime_error("Invalid or oversized triangle-list geometry");
    for (const auto &v : vertices) {
      for (float c : v.position)
        if (!std::isfinite(c))
          throw std::runtime_error("Nonfinite position");
      for (float c : v.color)
        if (!std::isfinite(c))
          throw std::runtime_error("Nonfinite vertex color");
    }
    for (auto i : indices)
      if (i >= vertices.size())
        throw std::runtime_error("Vertex index out of range");
    OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    if (geometry)
      vkDestroyBuffer(device, geometry, nullptr);
    if (geometry_memory)
      vkFreeMemory(device, geometry_memory, nullptr);
    geometry = VK_NULL_HANDLE;
    geometry_memory = VK_NULL_HANDLE;
    vertex_count = index_count = 0;
    if (vertices.empty())
      return; // An explicit clear-only frame.
    index_offset = vertices.size_bytes();
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = index_offset + indices.size_bytes();
    bc.usage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    OK(vkCreateBuffer(device, &bc, nullptr, &geometry));
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, geometry, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    OK(vkAllocateMemory(device, &ai, nullptr, &geometry_memory));
    OK(vkBindBufferMemory(device, geometry, geometry_memory, 0));
    void *mapped = nullptr;
    OK(vkMapMemory(device, geometry_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    std::memcpy(mapped, vertices.data(), vertices.size_bytes());
    if (!indices.empty())
      std::memcpy(static_cast<uint8_t *>(mapped) + index_offset, indices.data(),
                  indices.size_bytes());
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = geometry_memory;
    range.size = VK_WHOLE_SIZE;
    auto flushed = vkFlushMappedMemoryRanges(device, 1, &range);
    vkUnmapMemory(device, geometry_memory);
    check(flushed, "vkFlushMappedMemoryRanges");
    vertex_count = static_cast<uint32_t>(vertices.size());
    index_count = static_cast<uint32_t>(indices.size());
  }
  bool draw() {
    if (!device || framebuffers.empty())
      throw std::runtime_error("Render target unavailable");
    auto rect = [this](Rect r) {
      if (!r.width && !r.height && !r.x && !r.y)
        return VkRect2D{{0, 0}, extent};
      if (!r.width || !r.height || r.x > extent.width || r.y > extent.height ||
          r.width > extent.width - r.x || r.height > extent.height - r.y)
        throw std::runtime_error("Viewport/scissor outside target");
      return VkRect2D{{static_cast<int32_t>(r.x), static_cast<int32_t>(r.y)},
                      {r.width, r.height}};
    };
    const auto viewport = rect(parameters.viewport);
    const auto scissor = rect(parameters.scissor);
    OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    uint32_t index = 0;
    if (surface) {
      VkResult r = vkAcquireNextImageKHR(device, swapchain, 1000000000ull,
                                         acquire, VK_NULL_HANDLE, &index);
      if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        dirty = true;
        return false;
      }
      if (r == VK_TIMEOUT || r == VK_NOT_READY)
        return false;
      if (r == VK_SUBOPTIMAL_KHR)
        dirty = true;
      else
        check(r, "vkAcquireNextImageKHR");
    }
    OK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    OK(vkBeginCommandBuffer(cmd, &begin));
    VkClearValue clear{};
    std::copy(parameters.clear_color.begin(), parameters.clear_color.end(),
              clear.color.float32);
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = pass;
    rp.framebuffer = framebuffers.at(index);
    rp.renderArea.extent = extent;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkViewport vp{(float)viewport.offset.x,
                  (float)viewport.offset.y,
                  (float)viewport.extent.width,
                  (float)viewport.extent.height,
                  0,
                  1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    if (vertex_count) {
      VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &geometry, &offset);
      if (index_count) {
        vkCmdBindIndexBuffer(cmd, geometry, index_offset, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
      } else
        vkCmdDraw(cmd, vertex_count, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmd);
    if (!surface) {
      VkBufferImageCopy copy{};
      copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy.imageExtent = {extent.width, extent.height, 1};
      vkCmdCopyImageToBuffer(cmd, owned_image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1,
                             &copy);
      VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
      b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.buffer = readback;
      b.size = VK_WHOLE_SIZE;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &b, 0,
                           nullptr);
    }
    OK(vkEndCommandBuffer(cmd));
    OK(vkResetFences(device, 1, &fence));
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    if (surface) {
      submit.waitSemaphoreCount = 1;
      submit.pWaitSemaphores = &acquire;
      submit.pWaitDstStageMask = &stage;
      submit.signalSemaphoreCount = 1;
      submit.pSignalSemaphores = &finished[index];
    }
    OK(vkQueueSubmit(queue, 1, &submit, fence));
    if (surface) {
      VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
      present.waitSemaphoreCount = 1;
      present.pWaitSemaphores = &finished[index];
      present.swapchainCount = 1;
      present.pSwapchains = &swapchain;
      present.pImageIndices = &index;
      VkResult r = vkQueuePresentKHR(queue, &present);
      if (r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_OUT_OF_DATE_KHR)
        dirty = true;
      else
        check(r, "vkQueuePresentKHR");
    }
    return true;
  }
  std::vector<uint8_t> readPixels() {
    if (!device || surface || !readback)
      throw std::runtime_error("Readback requires an offscreen target");
    OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    void *pixels = nullptr;
    OK(vkMapMemory(device, readback_memory, 0, VK_WHOLE_SIZE, 0, &pixels));
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = readback_memory;
    range.size = VK_WHOLE_SIZE;
    VkResult r = vkInvalidateMappedMemoryRanges(device, 1, &range);
    if (r != VK_SUCCESS) {
      vkUnmapMemory(device, readback_memory);
      check(r, "vkInvalidateMappedMemoryRanges");
    }
    auto data = static_cast<const uint8_t *>(pixels);
    std::vector<uint8_t> result(data, data + size_t(extent.width) *
                                                 extent.height * 4);
    vkUnmapMemory(device, readback_memory);
    return result;
  }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
void Renderer::start(void *window, bool validation) {
  impl_->start(window, validation);
}
void Renderer::close() noexcept { impl_->close(); }
void Renderer::offscreen(uint32_t width, uint32_t height) {
  impl_->offscreen(width, height);
}
bool Renderer::resize(uint32_t width, uint32_t height, bool vsync) {
  return impl_->resize(width, height, vsync);
}
bool Renderer::draw() { return impl_->draw(); }
bool Renderer::needsResize() const { return impl_->dirty; }
std::vector<uint8_t> Renderer::readPixels() { return impl_->readPixels(); }
Info Renderer::info() const {
  return {impl_->adapter, impl_->api_version, impl_->device_type,
          impl_->present_mode, impl_->validation_errors.load()};
}
void Renderer::setGeometry(std::span<const Vertex> vertices,
                           std::span<const uint32_t> indices) {
  impl_->setGeometry(vertices, indices);
}
void Renderer::setParameters(const DrawParameters &parameters) {
  impl_->setParameters(parameters);
}
} // namespace d2::vulkan
