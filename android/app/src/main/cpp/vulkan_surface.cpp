// Disgaea D2 Android: first platform layer. Creates a Vulkan swapchain on the Activity's Surface and
// presents a colour that reacts to the gamepad, proving surface, GPU, refresh rate and input on the device.
// The recompiled game core is not part of this milestone (it is generated from the user's ISO).
#include <jni.h>
#include "bridge.h"
#include <android/log.h>
#include <android/native_window_jni.h>

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define TAG "D2Android"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

std::atomic<int> g_buttons{0};
std::atomic<float> g_lx{0.f}, g_ly{0.f};
std::atomic<float> g_rx{0.f}, g_ry{0.f};
std::mutex g_info_mutex;
std::string g_info = "Vulkan: not started";

void set_info(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_info_mutex);
    g_info = text;
}

struct Renderer {
    ANativeWindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore acquire = VK_NULL_HANDLE;
    std::vector<VkSemaphore> render_done;
    VkFence fence = VK_NULL_HANDLE;
    std::string gpu;
    uint32_t api = 0;
    std::thread thread;
    std::atomic<bool> run{false};

    bool init(ANativeWindow* w) {
        window = w;
        const char* instance_ext[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "DisgaeaD2Android";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &app;
        ici.enabledExtensionCount = 2;
        ici.ppEnabledExtensionNames = instance_ext;
        if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) return fail("vkCreateInstance");

        VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        sci.window = window;
        if (vkCreateAndroidSurfaceKHR(instance, &sci, nullptr, &surface) != VK_SUCCESS) return fail("vkCreateAndroidSurfaceKHR");

        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (!count) return fail("no Vulkan device");
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());
        for (auto candidate : devices) {
            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qn, nullptr);
            std::vector<VkQueueFamilyProperties> props(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qn, props.data());
            for (uint32_t i = 0; i < qn; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present);
                if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    physical = candidate;
                    family = i;
                    break;
                }
            }
            if (physical) break;
        }
        if (!physical) return fail("no graphics+present queue");
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physical, &props);
        gpu = props.deviceName;
        api = props.apiVersion;

        float priority = 1.f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        const char* device_ext[] = {"VK_KHR_swapchain"};
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = device_ext;
        if (vkCreateDevice(physical, &dci, nullptr, &device) != VK_SUCCESS) return fail("vkCreateDevice");
        vkGetDeviceQueue(device, family, 0, &queue);

        if (!make_swapchain()) return false;

        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = family;
        if (vkCreateCommandPool(device, &pci, nullptr, &pool) != VK_SUCCESS) return fail("vkCreateCommandPool");
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &cai, &cmd) != VK_SUCCESS) return fail("vkAllocateCommandBuffers");

        VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(device, &semi, nullptr, &acquire);
        render_done.resize(images.size());
        for (auto& s : render_done) vkCreateSemaphore(device, &semi, nullptr, &s);
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device, &fi, nullptr, &fence);
        return true;
    }

    bool fail(const char* what) {
        LOGE("Vulkan failure: %s", what);
        set_info(std::string("Vulkan failure: ") + what);
        return false;
    }

    bool make_swapchain() {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
        uint32_t fn = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fn, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fn);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fn, formats.data());
        if (formats.empty()) return fail("no surface formats");
        VkSurfaceFormatKHR chosen = formats[0];
        for (auto& f : formats)
            if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = f; break; }
        format = chosen.format;
        extent = caps.currentExtent;
        if (extent.width == 0xFFFFFFFFu) {
            extent.width = (uint32_t)ANativeWindow_getWidth(window);
            extent.height = (uint32_t)ANativeWindow_getHeight(window);
        }
        uint32_t want = caps.minImageCount + 1;
        if (caps.maxImageCount && want > caps.maxImageCount) want = caps.maxImageCount;
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & alpha)) alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sci.surface = surface;
        sci.minImageCount = want;
        sci.imageFormat = chosen.format;
        sci.imageColorSpace = chosen.colorSpace;
        sci.imageExtent = extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform = caps.currentTransform;
        sci.compositeAlpha = alpha;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS) return fail("vkCreateSwapchainKHR");
        uint32_t n = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
        images.resize(n);
        vkGetSwapchainImagesKHR(device, swapchain, &n, images.data());
        return true;
    }

    void barrier(VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst,
                 VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from;
        b.newLayout = to;
        b.srcAccessMask = src;
        b.dstAccessMask = dst;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    void loop() {
        using clock = std::chrono::steady_clock;
        auto start = clock::now(), last = start;
        int frames = 0;
        while (run.load()) {
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
            uint32_t index = 0;
            VkResult r = vkAcquireNextImageKHR(device, swapchain, 1000000000ull, acquire, VK_NULL_HANDLE, &index);
            if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { if (r == VK_NOT_READY || r == VK_TIMEOUT) continue; break; }
            vkResetFences(device, 1, &fence);
            vkResetCommandBuffer(cmd, 0);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &bi);
            barrier(images[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            float t = std::chrono::duration<float>(clock::now() - start).count();
            int buttons = g_buttons.load();
            VkClearColorValue color{};
            color.float32[0] = 0.10f + 0.10f * (0.5f + 0.5f * std::sin(t)) + (buttons & 1 ? 0.6f : 0.f) + (g_lx.load() > 0 ? g_lx.load() * 0.3f : 0.f);
            color.float32[1] = 0.12f + (buttons & 2 ? 0.6f : 0.f) + (g_ly.load() > 0 ? g_ly.load() * 0.3f : 0.f);
            color.float32[2] = 0.25f + 0.15f * (0.5f + 0.5f * std::sin(t * 0.7f)) + (buttons & 4 ? 0.5f : 0.f);
            color.float32[3] = 1.f;
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
            barrier(images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
            vkEndCommandBuffer(cmd);
            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &acquire;
            si.pWaitDstStageMask = &wait_stage;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &render_done[index];
            vkQueueSubmit(queue, 1, &si, fence);
            VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &render_done[index];
            pi.swapchainCount = 1;
            pi.pSwapchains = &swapchain;
            pi.pImageIndices = &index;
            vkQueuePresentKHR(queue, &pi);
            ++frames;
            auto now = clock::now();
            if (now - last >= std::chrono::milliseconds(500)) {
                float fps = frames / std::chrono::duration<float>(now - last).count();
                char text[256];
                std::snprintf(text, sizeof text, "Vulkan %u.%u.%u | %s | %ux%u | %.0f fps", VK_API_VERSION_MAJOR(api),
                              VK_API_VERSION_MINOR(api), VK_API_VERSION_PATCH(api), gpu.c_str(), extent.width, extent.height, fps);
                set_info(text);
                frames = 0;
                last = now;
            }
        }
        vkDeviceWaitIdle(device);
    }

    void start() {
        run = true;
        thread = std::thread([this] { loop(); });
    }

    void destroy() {
        run = false;
        if (thread.joinable()) thread.join();
        if (device) vkDeviceWaitIdle(device);
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (acquire) vkDestroySemaphore(device, acquire, nullptr);
        for (auto s : render_done) vkDestroySemaphore(device, s, nullptr);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        if (device) vkDestroyDevice(device, nullptr);
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        if (window) ANativeWindow_release(window);
    }
};

Renderer* g_renderer = nullptr;
ANativeWindow* g_window = nullptr;   // our own reference to the Activity's window

}  // namespace

ANativeWindow* d2_current_window() { return g_window; }

void d2_demo_stop() {
    if (g_renderer) { g_renderer->destroy(); delete g_renderer; g_renderer = nullptr; }
    set_info("Vulkan: surface handed to the game core");
}

extern "C" {

JNIEXPORT void JNICALL Java_org_disgaead2_port_NativeBridge_setSurface(JNIEnv* env, jclass, jobject surface) {
    if (g_renderer) { g_renderer->destroy(); delete g_renderer; g_renderer = nullptr; }
    if (g_window) { ANativeWindow_release(g_window); g_window = nullptr; }
    if (!surface) { set_info("Vulkan: no surface"); d2_core_forward_window(nullptr); return; }
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) { set_info("Vulkan: no native window"); return; }
    g_window = window;   // keep this reference; the renderer / core take their own
    if (d2_core_active()) { d2_core_forward_window(window); return; }
    ANativeWindow_acquire(window);
    g_renderer = new Renderer();
    if (g_renderer->init(window)) g_renderer->start();
}

JNIEXPORT void JNICALL Java_org_disgaead2_port_NativeBridge_setInput(JNIEnv*, jclass, jint buttons, jfloat lx, jfloat ly, jfloat rx, jfloat ry) {
    g_buttons = buttons;
    g_lx = lx;
    g_ly = ly;
    g_rx = rx;
    g_ry = ry;
    d2_core_forward_pad(buttons, lx, ly, rx, ry);
}

JNIEXPORT jstring JNICALL Java_org_disgaead2_port_NativeBridge_info(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(g_info_mutex);
    return env->NewStringUTF(g_info.c_str());
}

}  // extern "C"
