// Experimental Vulkan renderer and bounded synthetic RSX shader execution.
// Game/RSX command replay is not connected.
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "rsx_contract.h"
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
#include <type_traits>
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
  X(vkGetPhysicalDeviceFeatures)                                               \
  X(vkGetPhysicalDeviceProperties)                                             \
  X(vkGetPhysicalDeviceFormatProperties)                                       \
      X(vkGetPhysicalDeviceQueueFamilyProperties)                              \
          X(vkGetPhysicalDeviceMemoryProperties)                               \
              X(vkEnumerateDeviceExtensionProperties) X(vkCreateDevice)        \
                  X(vkGetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X)                                                     \
  X(vkDestroyDevice)                                                            \
  X(vkGetDeviceQueue)                                                           \
  X(vkDeviceWaitIdle)                                                           \
  X(vkCreateCommandPool)                                                        \
  X(vkDestroyCommandPool)                                                       \
  X(vkAllocateCommandBuffers) X(vkResetCommandBuffer) X(                        \
      vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCreateFence)              \
      X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(                  \
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
#define TEXTURE_FUNCTIONS(X)                                                   \
  X(vkCreateDescriptorSetLayout)                                               \
  X(vkDestroyDescriptorSetLayout)                                              \
  X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool)                         \
      X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateSampler) \
          X(vkDestroySampler) X(vkCmdCopyBufferToImage)                        \
              X(vkCmdBindDescriptorSets) X(vkCmdPushConstants)
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
  TEXTURE_FUNCTIONS(DECLARE)
  SURFACE_FUNCTIONS(DECLARE)
  SWAP_FUNCTIONS(DECLARE)
  DECLARE(vkCreateDebugUtilsMessengerEXT)
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
  // Offscreen MRT attachments 1..3. Attachment 0 remains owned_image.
  struct ColorTarget {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
  };
  std::vector<ColorTarget> extra_colors;
  uint32_t color_count = 1;
  bool independent_blend = false, depth_clamp_supported = false,
       bc_supported = false;
  RasterState raster_state{};
  std::array<VkSampler, 16> unit_samplers{};
  VkRenderPass pass = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  std::array<VkPipeline, 6> pipelines{};
  struct DepthTarget {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
  };
  std::vector<DepthTarget> depths;
  VkFormat depth_format = VK_FORMAT_UNDEFINED;
  VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
  VkBuffer readback = VK_NULL_HANDLE;
  VkDeviceMemory readback_memory = VK_NULL_HANDLE;
  VkBuffer geometry = VK_NULL_HANDLE;
  VkDeviceMemory geometry_memory = VK_NULL_HANDLE;
  VkDeviceSize index_offset = 0;
  uint32_t vertex_count = 0, index_count = 0;
  DrawParameters parameters{};
  VkDescriptorSetLayout texture_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet texture_set = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
  };
  // Slot 16 is an immutable white fallback, independent from unit 0.
  std::array<Texture, 17> textures{};
  struct Uniform {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
  };
  struct Constants {
    std::array<Uniform, 2> uniforms{};
    std::array<VkDescriptorSet, 2> sets{};
    bool ready = false;
    uint32_t fragment_bytes = 0;
  };
  struct Program {
    std::vector<uint32_t> vertex, fragment;
    RsxContract vs_contract, fp_contract;
    std::array<VkPipeline, 6> pipelines{};
  };
  std::vector<Program> programs;
  std::vector<Constants> constant_states;
  std::array<VkDescriptorSetLayout, 2> rsx_layouts{};
  VkDescriptorPool rsx_pool = VK_NULL_HANDLE;
  bool rsxMode() const { return !programs.empty(); }
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  bool texture_ready = false;
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
  void makePipeline(Program *program = nullptr) {
    VkShaderModuleCreateInfo shader{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize =
        rsxMode() ? program->vertex.size() * 4 : sizeof(triangle_vert);
    shader.pCode = rsxMode() ? program->vertex.data() : triangle_vert;
    OK(vkCreateShaderModule(device, &shader, nullptr, &vert));
    shader.codeSize =
        rsxMode() ? program->fragment.size() * 4 : sizeof(triangle_frag);
    shader.pCode = rsxMode() ? program->fragment.data() : triangle_frag;
    OK(vkCreateShaderModule(device, &shader, nullptr, &frag));
    VkPipelineLayoutCreateInfo lc{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.setLayoutCount = rsxMode() ? 2 : 1;
    lc.pSetLayouts = rsxMode() ? rsx_layouts.data() : &texture_layout;
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)};
    lc.pushConstantRangeCount = rsxMode() ? 0 : 1;
    lc.pPushConstantRanges = &push;
    if (!layout)
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
    VkVertexInputBindingDescription binding{
        0, uint32_t(rsxMode() ? sizeof(RsxVertex) : sizeof(Vertex)),
        VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)}};
    vertex.vertexBindingDescriptionCount = 1;
    vertex.pVertexBindingDescriptions = &binding;
    std::array<VkVertexInputAttributeDescription, 16> rsx_attributes{};
    if (program)
      for (uint32_t i = 0; i < program->vs_contract.inputs.size(); i++) {
        auto location = program->vs_contract.inputs[i];
        rsx_attributes[i] = {location, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                             location * 16};
      }
    vertex.vertexAttributeDescriptionCount =
        program ? uint32_t(program->vs_contract.inputs.size()) : 3;
    vertex.pVertexAttributeDescriptions =
        rsxMode() ? rsx_attributes.data() : attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = raster_state.cull == 1   ? VK_CULL_MODE_FRONT_BIT
                      : raster_state.cull == 2 ? VK_CULL_MODE_BACK_BIT
                                               : VK_CULL_MODE_NONE;
    raster.frontFace = raster_state.front_ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                              : VK_FRONT_FACE_CLOCKWISE;
    raster.depthClampEnable = raster_state.depth_clamp;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = raster_state.color_mask;
    attachment.blendEnable = VK_TRUE;
    auto factor = [](BlendFactor f) {
      const VkBlendFactor table[] = {VK_BLEND_FACTOR_ZERO,
                                     VK_BLEND_FACTOR_ONE,
                                     VK_BLEND_FACTOR_SRC_COLOR,
                                     VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                     VK_BLEND_FACTOR_DST_COLOR,
                                     VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                     VK_BLEND_FACTOR_SRC_ALPHA,
                                     VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                     VK_BLEND_FACTOR_DST_ALPHA,
                                     VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
                                     VK_BLEND_FACTOR_SRC_ALPHA_SATURATE};
      return table[uint32_t(f)];
    };
    attachment.srcColorBlendFactor = factor(raster_state.src_color);
    attachment.dstColorBlendFactor = factor(raster_state.dst_color);
    attachment.colorBlendOp = VkBlendOp(raster_state.color_op);
    attachment.srcAlphaBlendFactor = factor(raster_state.src_alpha);
    attachment.dstAlphaBlendFactor = factor(raster_state.dst_alpha);
    attachment.alphaBlendOp = VkBlendOp(raster_state.alpha_op);
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    std::vector<VkPipelineColorBlendAttachmentState> color_blends(color_count,
                                                                  attachment);
    // A shader may omit a bound target. Preserve its clear/previous contents,
    // instead of allowing undefined fragment outputs to overwrite them.
    for (uint32_t i = 0; i < color_count; i++)
      if ((program && std::find(program->fp_contract.outputs.begin(),
                                program->fp_contract.outputs.end(),
                                i) == program->fp_contract.outputs.end()) ||
          (!program && i != 0))
        color_blends[i].colorWriteMask = 0;
    blend.attachmentCount = color_count;
    blend.pAttachments = color_blends.data();
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
    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthCompareOp = VkCompareOp(raster_state.depth_compare);
    pc.pDepthStencilState = &ds;
    auto &destination = program ? program->pipelines : pipelines;
    for (unsigned mode = 0; mode < 3; mode++)
      for (unsigned alpha = 0; alpha < 2; alpha++) {
        ds.depthTestEnable = mode != 0;
        ds.depthWriteEnable = mode == 2;
        for (auto &a : color_blends)
          a.blendEnable = alpha;
        OK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pc, nullptr,
                                     &destination[mode * 2 + alpha]));
      }
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
    std::vector<VkAttachmentDescription> attachments(color_count + 1, color);
    auto &depth = attachments.back();
    depth = {};
    depth.format = depth_format;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depth_ref{
        color_count, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    std::vector<VkAttachmentReference> refs;
    for (uint32_t i = 0; i < color_count; i++)
      refs.push_back({i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = color_count;
    sub.pColorAttachments = refs.data();
    sub.pDepthStencilAttachment = &depth_ref;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = deps[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = surface ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                                   : VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].dstAccessMask = surface ? 0 : VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = uint32_t(attachments.size());
    rp.pAttachments = attachments.data();
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 2;
    rp.pDependencies = deps;
    OK(vkCreateRenderPass(device, &rp, nullptr, &pass));
    if (rsxMode()) {
      for (auto &program : programs)
        makePipeline(&program);
    } else
      makePipeline();
    for (VkImage image : images) {
      depths.emplace_back();
      auto &target = depths.back();
      VkImageCreateInfo di{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      di.imageType = VK_IMAGE_TYPE_2D;
      di.format = depth_format;
      di.extent = {extent.width, extent.height, 1};
      di.mipLevels = di.arrayLayers = 1;
      di.samples = VK_SAMPLE_COUNT_1_BIT;
      di.tiling = VK_IMAGE_TILING_OPTIMAL;
      di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
      OK(vkCreateImage(device, &di, nullptr, &target.image));
      VkMemoryRequirements req{};
      vkGetImageMemoryRequirements(device, target.image, &req);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = req.size;
      ai.memoryTypeIndex =
          memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      OK(vkAllocateMemory(device, &ai, nullptr, &target.memory));
      OK(vkBindImageMemory(device, target.image, target.memory, 0));
      VkImageViewCreateInfo dvi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      dvi.image = target.image;
      dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      dvi.format = depth_format;
      dvi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
      OK(vkCreateImageView(device, &dvi, nullptr, &target.view));
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
      std::vector<VkImageView> fb_views{views.back()};
      for (auto &c : extra_colors)
        fb_views.push_back(c.view);
      fb_views.push_back(target.view);
      fb.attachmentCount = uint32_t(fb_views.size());
      fb.pAttachments = fb_views.data();
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
    auto destroyPipelines = [this](auto &list) {
      for (auto &pipeline : list) {
        if (pipeline)
          vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
      }
    };
    destroyPipelines(pipelines);
    for (auto &program : programs)
      destroyPipelines(program.pipelines);
    for (auto &d : depths) {
      if (d.view)
        vkDestroyImageView(device, d.view, nullptr);
      if (d.image)
        vkDestroyImage(device, d.image, nullptr);
      if (d.memory)
        vkFreeMemory(device, d.memory, nullptr);
    }
    depths.clear();
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
      if (descriptor_pool)
        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
      if (rsx_pool)
        vkDestroyDescriptorPool(device, rsx_pool, nullptr);
      for (auto l : rsx_layouts)
        if (l)
          vkDestroyDescriptorSetLayout(device, l, nullptr);
      for (auto &state : constant_states)
        for (auto &u : state.uniforms) {
          if (u.buffer)
            vkDestroyBuffer(device, u.buffer, nullptr);
          if (u.memory)
            vkFreeMemory(device, u.memory, nullptr);
        }
      for (auto &t : textures)
        releaseTexture(t);
      for (auto s : unit_samplers)
        if (s)
          vkDestroySampler(device, s, nullptr);
      if (sampler)
        vkDestroySampler(device, sampler, nullptr);
      if (texture_layout)
        vkDestroyDescriptorSetLayout(device, texture_layout, nullptr);
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
      for (auto &c : extra_colors) {
        if (c.view)
          vkDestroyImageView(device, c.view, nullptr);
        if (c.image)
          vkDestroyImage(device, c.image, nullptr);
        if (c.memory)
          vkFreeMemory(device, c.memory, nullptr);
      }
      extra_colors.clear();
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
      extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
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
    VkValidationFeatureEnableEXT sync =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT vf{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    vf.enabledValidationFeatureCount = 1;
    vf.pEnabledValidationFeatures = &sync;
    vf.pNext = &dc;
    VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ic.pApplicationInfo = &app;
    ic.enabledExtensionCount = (uint32_t)extensions.size();
    ic.ppEnabledExtensionNames = extensions.data();
    ic.enabledLayerCount = (uint32_t)layers.size();
    ic.ppEnabledLayerNames = layers.data();
    if (useValidation)
      ic.pNext = &vf;
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
    for (auto candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
      VkFormatProperties features{};
      vkGetPhysicalDeviceFormatProperties(physical, candidate, &features);
      if (features.optimalTilingFeatures &
          VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        depth_format = candidate;
        break;
      }
    }
    if (depth_format == VK_FORMAT_UNDEFINED)
      throw Unavailable("No supported Vulkan depth format");
    float priority = 1;
    VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qc.queueFamilyIndex = family;
    qc.queueCount = 1;
    qc.pQueuePriorities = &priority;
    const char *swap = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo dev{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dev.queueCreateInfoCount = 1;
    dev.pQueueCreateInfos = &qc;
    VkPhysicalDeviceFeatures supported_features{};
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    VkPhysicalDeviceFeatures enabled_features{};
    enabled_features.independentBlend = supported_features.independentBlend;
    independent_blend = enabled_features.independentBlend != 0;
    enabled_features.depthClamp = supported_features.depthClamp;
    depth_clamp_supported = enabled_features.depthClamp != 0;
    enabled_features.textureCompressionBC =
        supported_features.textureCompressionBC;
    bc_supported = enabled_features.textureCompressionBC != 0;
    dev.pEnabledFeatures = &enabled_features;
    if (surface) {
      dev.enabledExtensionCount = 1;
      dev.ppEnabledExtensionNames = &swap;
    }
    OK(vkCreateDevice(physical, &dev, nullptr, &device));
#define LOAD(name) loadDevice(name, #name);
    DEVICE_FUNCTIONS(LOAD)
    TEXTURE_FUNCTIONS(LOAD)
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
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 1;
    dl.pBindings = &binding;
    OK(vkCreateDescriptorSetLayout(device, &dl, nullptr, &texture_layout));
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dp{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = 1;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &size;
    OK(vkCreateDescriptorPool(device, &dp, nullptr, &descriptor_pool));
    VkDescriptorSetAllocateInfo da{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = descriptor_pool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &texture_layout;
    OK(vkAllocateDescriptorSets(device, &da, &texture_set));
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    OK(vkCreateSampler(device, &si, nullptr, &sampler));
    const uint8_t white[] = {255, 255, 255, 255};
    uploadTexture(16, 1, 1, white);
    uploadTexture(0, 1, 1, white);
  }
  // Called only after the upload/draw fence or device idle has completed.
  void releaseTexture(Texture &t) {
    if (t.view)
      vkDestroyImageView(device, t.view, nullptr);
    if (t.image)
      vkDestroyImage(device, t.image, nullptr);
    if (t.memory)
      vkFreeMemory(device, t.memory, nullptr);
    if (staging)
      vkDestroyBuffer(device, staging, nullptr);
    if (staging_memory)
      vkFreeMemory(device, staging_memory, nullptr);
    t = {};
    staging = VK_NULL_HANDLE;
    staging_memory = VK_NULL_HANDLE;
  }
  void setTextureUnit(uint32_t unit, uint32_t w, uint32_t h,
                      std::span<const uint8_t> rgba) {
    if (unit >= 16)
      throw std::runtime_error("Invalid fragment texture unit");
    uploadTexture(unit, w, h, rgba);
  }
  void uploadTexture(uint32_t unit, uint32_t w, uint32_t h,
                     std::span<const uint8_t> rgba, uint32_t bc_format = 0,
                     uint32_t mapping = 0x688) {
    size_t expected = bc_format ? size_t((w + 3) / 4) * ((h + 3) / 4) *
                                      (bc_format == 1 ? 8 : 16)
                                : size_t(w) * h * 4;
    if (bc_format > 3 || (bc_format && !bc_supported))
      throw std::runtime_error("BC texture compression unavailable");
    for (unsigned c = 0; c < 4; ++c)
      if (((mapping >> (3 * c)) & 7) > 5)
        throw std::runtime_error("Invalid texture component mapping");
    if (mapping & ~0xfffu)
      throw std::runtime_error("Unknown texture mapping bits");
    if (!device || !texture_set || !w || !h || w > 4096 || h > 4096 ||
        rgba.size() != expected)
      throw std::runtime_error("Invalid or oversized texture payload");
    OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    texture_ready = false;
    auto &t = textures.at(unit);
    releaseTexture(t);
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = rgba.size();
    bc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    OK(vkCreateBuffer(device, &bc, nullptr, &staging));
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, staging, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    OK(vkAllocateMemory(device, &ai, nullptr, &staging_memory));
    OK(vkBindBufferMemory(device, staging, staging_memory, 0));
    void *mapped = nullptr;
    OK(vkMapMemory(device, staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    std::memcpy(mapped, rgba.data(), rgba.size());
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = staging_memory;
    range.size = VK_WHOLE_SIZE;
    auto flush = vkFlushMappedMemoryRanges(device, 1, &range);
    vkUnmapMemory(device, staging_memory);
    check(flush, "Texture flush");
    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = bc_format == 1   ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK
                : bc_format == 2 ? VK_FORMAT_BC2_UNORM_BLOCK
                : bc_format == 3 ? VK_FORMAT_BC3_UNORM_BLOCK
                                 : VK_FORMAT_R8G8B8A8_UNORM;
    ic.extent = {w, h, 1};
    ic.mipLevels = ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    OK(vkCreateImage(device, &ic, nullptr, &t.image));
    vkGetImageMemoryRequirements(device, t.image, &req);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    OK(vkAllocateMemory(device, &ai, nullptr, &t.memory));
    OK(vkBindImageMemory(device, t.image, t.memory, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ic.format;
    const VkComponentSwizzle selectors[] = {
        VK_COMPONENT_SWIZZLE_R,    VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,    VK_COMPONENT_SWIZZLE_A,
        VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE};
    vi.components = {selectors[mapping & 7], selectors[(mapping >> 3) & 7],
                     selectors[(mapping >> 6) & 7],
                     selectors[(mapping >> 9) & 7]};
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    OK(vkCreateImageView(device, &vi, nullptr, &t.view));
    OK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    OK(vkBeginCommandBuffer(cmd, &begin));
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    barrier.image = t.image;
    barrier.subresourceRange = vi.subresourceRange;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging, t.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
    OK(vkEndCommandBuffer(cmd));
    OK(vkResetFences(device, 1, &fence));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    OK(vkQueueSubmit(queue, 1, &submit, fence));
    // Retain upload resources on timeout; close() waits device idle before
    // release.
    OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    VkDescriptorImageInfo image{sampler, t.view,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texture_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    if (unit == 0)
      vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    if (rsx_pool)
      updateRsxTextures();
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, staging_memory, nullptr);
    staging = VK_NULL_HANDLE;
    staging_memory = VK_NULL_HANDLE;
    texture_ready = true;
  }
  void updateRsxTextures() {
    std::array<VkDescriptorImageInfo, 16> images{}, samplers{};
    for (size_t i = 0; i < 16; i++) {
      images[i].imageView =
          textures[i].view ? textures[i].view : textures[16].view;
      images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      samplers[i].sampler = unit_samplers[i] ? unit_samplers[i] : sampler;
    }
    VkWriteDescriptorSet writes[2]{};
    for (auto &w : writes) {
      w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      w.descriptorCount = 16;
    }
    writes[0].dstBinding = 32;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[0].pImageInfo = images.data();
    writes[1].dstBinding = 64;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[1].pImageInfo = samplers.data();
    for (auto &state : constant_states) {
      if (!state.sets[1])
        continue;
      for (auto &write : writes)
        write.dstSet = state.sets[1];
      vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
  }
  void setRsxProgram(std::span<const uint32_t> v, std::span<const uint32_t> f) {
    if (!device || pass || geometry || rsx_pool)
      throw std::runtime_error(
          "Install one RSX program before targets/geometry");
    auto program = checkedProgram(v, f);
    OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    VkDescriptorSetLayoutBinding vs{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                    VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    VkDescriptorSetLayoutBinding ps[] = {
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr},
        {32, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr},
        {64, VK_DESCRIPTOR_TYPE_SAMPLER, 16, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr}};
    VkDescriptorSetLayoutCreateInfo dl{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 1;
    dl.pBindings = &vs;
    OK(vkCreateDescriptorSetLayout(device, &dl, nullptr, &rsx_layouts[0]));
    dl.bindingCount = 3;
    dl.pBindings = ps;
    OK(vkCreateDescriptorSetLayout(device, &dl, nullptr, &rsx_layouts[1]));
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128},
                                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024},
                                    {VK_DESCRIPTOR_TYPE_SAMPLER, 1024}};
    VkDescriptorPoolCreateInfo dp{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = 128;
    dp.poolSizeCount = 3;
    dp.pPoolSizes = sizes;
    OK(vkCreateDescriptorPool(device, &dp, nullptr, &rsx_pool));
    allocateConstants(); // Default state 0, intentionally not ready yet.
    programs.push_back(std::move(program));
  }
  Program checkedProgram(std::span<const uint32_t> v,
                         std::span<const uint32_t> f) {
    auto vs_contract = validateRsxModule(v, false);
    auto fp_contract = validateRsxModule(f, true);
    for (auto input : fp_contract.inputs)
      if (std::find(vs_contract.outputs.begin(), vs_contract.outputs.end(),
                    input) == vs_contract.outputs.end())
        throw std::runtime_error("RSX varying missing from vertex program");
    return {{v.begin(), v.end()},
            {f.begin(), f.end()},
            std::move(vs_contract),
            std::move(fp_contract),
            {}};
  }
  uint32_t addRsxProgram(std::span<const uint32_t> v,
                         std::span<const uint32_t> f) {
    if (!device || !rsxMode() || pass)
      throw std::runtime_error("Register RSX programs before target creation");
    auto program = checkedProgram(v, f);
    for (uint32_t i = 0; i < programs.size(); i++)
      if (programs[i].vertex == program.vertex &&
          programs[i].fragment == program.fragment)
        return i;
    if (programs.size() >= 32)
      throw std::runtime_error("RSX program limit reached");
    programs.push_back(std::move(program));
    return uint32_t(programs.size() - 1);
  }
  uint32_t allocateConstants() {
    if (constant_states.size() >= 64)
      throw std::runtime_error("RSX constant snapshot limit reached");
    constant_states.emplace_back();
    auto &state = constant_states.back();
    VkDescriptorSetAllocateInfo da{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = rsx_pool;
    da.descriptorSetCount = 2;
    da.pSetLayouts = rsx_layouts.data();
    OK(vkAllocateDescriptorSets(device, &da, state.sets.data()));
    for (unsigned i = 0; i < 2; i++) {
      VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      bc.size = i ? 16384 : 8224;
      bc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
      OK(vkCreateBuffer(device, &bc, nullptr, &state.uniforms[i].buffer));
      VkMemoryRequirements req{};
      vkGetBufferMemoryRequirements(device, state.uniforms[i].buffer, &req);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = req.size;
      ai.memoryTypeIndex =
          memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
      OK(vkAllocateMemory(device, &ai, nullptr, &state.uniforms[i].memory));
      OK(vkBindBufferMemory(device, state.uniforms[i].buffer,
                            state.uniforms[i].memory, 0));
      VkDescriptorBufferInfo bi{state.uniforms[i].buffer, 0, bc.size};
      VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w.dstSet = state.sets[i];
      w.dstBinding = i ? 1 : 0;
      w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      w.descriptorCount = 1;
      w.pBufferInfo = &bi;
      vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    }
    updateRsxTextures();
    return uint32_t(constant_states.size() - 1);
  }
  void validateConstants(std::span<const std::array<float, 4>> v,
                         std::span<const std::array<float, 4>> f) {
    if (!device || !rsxMode() || v.size() != 514 || f.empty() ||
        f.size() > 1024)
      throw std::runtime_error("Invalid RSX constant buffer sizes");
    for (auto rows : {v, f})
      for (auto &row : rows)
        for (float c : row)
          if (!std::isfinite(c))
            throw std::runtime_error("Nonfinite RSX constant");
  }
  void setRsxConstants(std::span<const std::array<float, 4>> v,
                       std::span<const std::array<float, 4>> f) {
    validateConstants(v, f);
    if (f.size_bytes() < programs[0].fp_contract.uniform_bytes)
      throw std::runtime_error(
          "FP constants shorter than default program layout");
    writeConstants(0, v, f);
  }
  uint32_t createRsxConstants(std::span<const std::array<float, 4>> v,
                              std::span<const std::array<float, 4>> f) {
    validateConstants(v, f);
    OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    auto handle = allocateConstants();
    writeConstants(handle, v, f);
    return handle;
  }
  void writeConstants(uint32_t handle, std::span<const std::array<float, 4>> v,
                      std::span<const std::array<float, 4>> f) {
    auto &state = constant_states.at(handle);
    OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    state.ready = false;
    for (unsigned i = 0; i < 2; i++) {
      auto rows = i ? f : v;
      void *mapped = nullptr;
      OK(vkMapMemory(device, state.uniforms[i].memory, 0, VK_WHOLE_SIZE, 0,
                     &mapped));
      std::memset(mapped, 0, i ? 16384 : 8224);
      std::memcpy(mapped, rows.data(), rows.size_bytes());
      VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      range.memory = state.uniforms[i].memory;
      range.size = VK_WHOLE_SIZE;
      auto result = vkFlushMappedMemoryRanges(device, 1, &range);
      vkUnmapMemory(device, state.uniforms[i].memory);
      check(result, "RSX constant buffer flush");
    }
    state.fragment_bytes = uint32_t(f.size_bytes());
    state.ready = true;
  }
  void setRasterState(const RasterState &state) {
    if (!device || pass || state.cull > 2 || state.color_mask > 15 ||
        state.depth_compare > 7 || uint32_t(state.src_color) > 10 ||
        uint32_t(state.dst_color) > 10 || uint32_t(state.src_alpha) > 10 ||
        uint32_t(state.dst_alpha) > 10 || uint32_t(state.color_op) > 4 ||
        uint32_t(state.alpha_op) > 4 ||
        (state.depth_clamp && !depth_clamp_supported))
      throw std::runtime_error(
          "Unsupported raster state or target already created");
    raster_state = state;
  }
  void setSamplerUnit(uint32_t unit, bool linear, bool wrap) {
    if (!device || !rsxMode() || unit >= 16)
      throw std::runtime_error("Invalid RSX sampler unit");
    OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW =
        wrap ? VK_SAMPLER_ADDRESS_MODE_REPEAT
             : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler next = VK_NULL_HANDLE;
    OK(vkCreateSampler(device, &si, nullptr, &next));
    auto previous = unit_samplers[unit];
    unit_samplers[unit] = next;
    updateRsxTextures();
    if (previous)
      vkDestroySampler(device, previous, nullptr);
  }
  void validateColorTargets(uint32_t count) {
    if (!count || count > 4)
      throw std::runtime_error("Color attachment count must be 1..4");
    if (count > 1 && !independent_blend)
      throw std::runtime_error("MRT requires independentBlend on this device");
    for (const auto &program : programs)
      for (auto output : program.fp_contract.outputs)
        if (output >= count && !raster_state.discard_unbound_outputs)
          throw std::runtime_error(
              "RSX shader output has no bound color target");
  }
  void offscreen(uint32_t w, uint32_t h, uint32_t targets) {
    if (!device || owned_image || surface || !w || !h || w > 4096 || h > 4096)
      throw std::runtime_error("Invalid offscreen target request");
    validateColorTargets(
        targets); // Fail before allocating or mutating a target.
    color_count = targets;
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
    for (uint32_t i = 1; i < color_count; i++) {
      extra_colors.emplace_back();
      auto &c = extra_colors.back();
      OK(vkCreateImage(device, &ic, nullptr, &c.image));
      vkGetImageMemoryRequirements(device, c.image, &req);
      ai.allocationSize = req.size;
      ai.memoryTypeIndex =
          memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      OK(vkAllocateMemory(device, &ai, nullptr, &c.memory));
      OK(vkBindImageMemory(device, c.image, c.memory, 0));
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = c.image;
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = format;
      vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      OK(vkCreateImageView(device, &vi, nullptr, &c.view));
    }
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = (VkDeviceSize)w * h * 4 * color_count;
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
    if (!device || !surface)
      throw std::runtime_error("Resize requires a window surface");
    validateColorTargets(1); // Presentation currently binds only target 0.
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
    if (!std::isfinite(value.clear_depth) || value.clear_depth < 0 ||
        value.clear_depth > 1)
      throw std::runtime_error("Invalid clear depth");
    if (!std::isfinite(value.alpha_cutoff) || value.alpha_cutoff < -1 ||
        value.alpha_cutoff > 1)
      throw std::runtime_error("Invalid alpha cutoff");
    for (float c : value.clear_color)
      if (!std::isfinite(c))
        throw std::runtime_error("Nonfinite clear color");
    parameters = value;
  }
  template <class V>
  void uploadGeometry(std::span<const V> vertices,
                      std::span<const uint32_t> indices) {
    if (!device || vertices.size() > 1048576 || indices.size() > 3145728 ||
        (indices.empty() ? vertices.size() % 3 : indices.size() % 3))
      throw std::runtime_error("Invalid or oversized triangle-list geometry");
    for (const auto &v : vertices) {
      if constexpr (std::is_same_v<V, RsxVertex>) {
        for (const auto &a : v.attributes)
          for (float c : a)
            if (!std::isfinite(c))
              throw std::runtime_error("Nonfinite RSX attribute");
      } else {
        for (float c : v.uv)
          if (!std::isfinite(c))
            throw std::runtime_error("Nonfinite UV");
        for (float c : v.position)
          if (!std::isfinite(c))
            throw std::runtime_error("Nonfinite position");
        for (float c : v.color)
          if (!std::isfinite(c))
            throw std::runtime_error("Nonfinite color");
      }
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
  void setGeometry(std::span<const Vertex> v, std::span<const uint32_t> i) {
    if (rsxMode())
      throw std::runtime_error("RSX program requires RSX geometry");
    uploadGeometry(v, i);
  }
  void setRsxGeometry(std::span<const RsxVertex> v,
                      std::span<const uint32_t> i) {
    if (!rsxMode())
      throw std::runtime_error("Install RSX program first");
    uploadGeometry(v, i);
  }
  bool draw() {
    DrawCommand command;
    command.count = index_count ? index_count : vertex_count;
    command.indexed = index_count != 0;
    command.viewport = parameters.viewport;
    command.scissor = parameters.scissor;
    command.alpha_cutoff = parameters.alpha_cutoff;
    return draw(std::span<const DrawCommand>(&command, 1));
  }
  bool draw(std::span<const DrawCommand> commands) {
    if (commands.size() > 4096)
      throw std::runtime_error("Too many draw commands");
    if (!device || framebuffers.empty() || !texture_ready)
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
    for (const auto &c : commands) {
      auto bound = c.indexed ? index_count : vertex_count;
      if (c.first > bound || c.count > bound - c.first || c.count % 3 ||
          uint32_t(c.depth) > 2 || !std::isfinite(c.alpha_cutoff) ||
          c.alpha_cutoff < -1 || c.alpha_cutoff > 1 ||
          (rsxMode() && c.alpha_cutoff != -1))
        throw std::runtime_error("Invalid draw command range/state");
      if (rsxMode()) {
        if (c.rsx_program >= programs.size() ||
            c.rsx_constants >= constant_states.size())
          throw std::runtime_error("Unknown RSX draw handle");
        auto &state = constant_states[c.rsx_constants];
        if (!state.ready ||
            state.fragment_bytes <
                programs[c.rsx_program].fp_contract.uniform_bytes)
          throw std::runtime_error(
              "RSX draw constants unavailable or too short");
      } else if (c.rsx_program || c.rsx_constants)
        throw std::runtime_error("RSX handles in legacy draw");
      rect(c.viewport);
      rect(c.scissor);
    }
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
    std::vector<VkClearValue> clear(color_count + 1);
    for (uint32_t i = 0; i < color_count; i++)
      std::copy(parameters.clear_color.begin(), parameters.clear_color.end(),
                clear[i].color.float32);
    clear.back().depthStencil.depth = parameters.clear_depth;
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = pass;
    rp.framebuffer = framebuffers.at(index);
    rp.renderArea.extent = extent;
    rp.clearValueCount = uint32_t(clear.size());
    rp.pClearValues = clear.data();
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    if (!rsxMode())
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                              1, &texture_set, 0, nullptr);
    for (const auto &c : commands) {
      if (!c.count)
        continue;
      auto &draw_pipelines =
          rsxMode() ? programs[c.rsx_program].pipelines : pipelines;
      vkCmdBindPipeline(
          cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
          draw_pipelines[uint32_t(c.depth) * 2 + unsigned(c.blend)]);
      if (rsxMode())
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                                2, constant_states[c.rsx_constants].sets.data(),
                                0, nullptr);
      if (!rsxMode())
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(float), &c.alpha_cutoff);
      auto viewport = rect(c.viewport);
      auto scissor = rect(c.scissor);
      VkViewport vp{float(viewport.offset.x),
                    float(viewport.offset.y),
                    float(viewport.extent.width),
                    float(viewport.extent.height),
                    0,
                    1};
      vkCmdSetViewport(cmd, 0, 1, &vp);
      vkCmdSetScissor(cmd, 0, 1, &scissor);
      VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &geometry, &offset);
      if (c.indexed) {
        vkCmdBindIndexBuffer(cmd, geometry, index_offset, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, c.count, 1, c.first, 0, 0);
      } else
        vkCmdDraw(cmd, c.count, 1, c.first, 0);
    }
    vkCmdEndRenderPass(cmd);
    if (!surface) {
      VkBufferImageCopy copy{};
      copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy.imageExtent = {extent.width, extent.height, 1};
      for (uint32_t i = 0; i < color_count; i++) {
        copy.bufferOffset = VkDeviceSize(i) * extent.width * extent.height * 4;
        auto image = i ? extra_colors[i - 1].image : owned_image;
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback, 1, &copy);
      }
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
  std::vector<uint8_t> readPixels(uint32_t attachment) {
    if (!device || surface || !readback || attachment >= color_count)
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
    auto data = static_cast<const uint8_t *>(pixels) +
                size_t(attachment) * extent.width * extent.height * 4;
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
void Renderer::setRasterState(const RasterState &state) {
  impl_->setRasterState(state);
}
void Renderer::setSamplerUnit(uint32_t unit, bool linear, bool wrap) {
  impl_->setSamplerUnit(unit, linear, wrap);
}
void Renderer::offscreen(uint32_t width, uint32_t height, uint32_t targets) {
  impl_->offscreen(width, height, targets);
}
bool Renderer::resize(uint32_t width, uint32_t height, bool vsync) {
  return impl_->resize(width, height, vsync);
}
void Renderer::setTextureUnitBlocks(uint32_t unit, uint32_t w, uint32_t h,
                                    uint32_t format, uint32_t mapping,
                                    std::span<const uint8_t> blocks) {
  if (unit >= 16 || format < 1 || format > 3)
    throw std::runtime_error("Invalid compressed texture unit/format");
  impl_->uploadTexture(unit, w, h, blocks, format, mapping);
}
bool Renderer::draw() { return impl_->draw(); }
bool Renderer::draw(std::span<const DrawCommand> commands) {
  return impl_->draw(commands);
}
bool Renderer::needsResize() const { return impl_->dirty; }
std::vector<uint8_t> Renderer::readPixels(uint32_t attachment) {
  return impl_->readPixels(attachment);
}
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
void Renderer::setTexture(uint32_t width, uint32_t height,
                          std::span<const uint8_t> rgba) {
  impl_->setTextureUnit(0, width, height, rgba);
}
void Renderer::setTextureUnit(uint32_t unit, uint32_t w, uint32_t h,
                              std::span<const uint8_t> rgba) {
  impl_->setTextureUnit(unit, w, h, rgba);
}
void Renderer::setRsxProgram(std::span<const uint32_t> v,
                             std::span<const uint32_t> f) {
  impl_->setRsxProgram(v, f);
}
void Renderer::setRsxGeometry(std::span<const RsxVertex> v,
                              std::span<const uint32_t> i) {
  impl_->setRsxGeometry(v, i);
}
void Renderer::setRsxConstants(std::span<const std::array<float, 4>> v,
                               std::span<const std::array<float, 4>> f) {
  impl_->setRsxConstants(v, f);
}
uint32_t Renderer::addRsxProgram(std::span<const uint32_t> v,
                                 std::span<const uint32_t> f) {
  return impl_->addRsxProgram(v, f);
}
uint32_t Renderer::createRsxConstants(std::span<const std::array<float, 4>> v,
                                      std::span<const std::array<float, 4>> f) {
  return impl_->createRsxConstants(v, f);
}
} // namespace d2::vulkan
