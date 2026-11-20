// Loads the recompiled game core (libd2core.so, generated on the user's PC from their own ISO) and runs it.
#include <jni.h>
#include "bridge.h"
#include <android/log.h>
#include <dlfcn.h>
#include <atomic>
#include <string>
#include <thread>

#define TAG "D2Host"

namespace {
std::atomic<bool> g_started{false};
std::atomic<int> g_result{-1000};   // -1000 = running / not finished
typedef int (*core_run_fn)(const char*, const char*, const char*);
std::atomic<void (*)(void*)> g_set_window{nullptr};
std::atomic<void (*)(int, float, float, float, float)> g_set_pad{nullptr};
std::atomic<int (*)()> g_request_stop{nullptr};

std::string str(JNIEnv* env, jstring s) {
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}
}  // namespace

bool d2_core_active() { return g_set_window.load() != nullptr; }

void d2_core_forward_pad(int buttons, float lx, float ly, float rx, float ry) {
    if (auto fn = g_set_pad.load()) fn(buttons, lx, ly, rx, ry);
}

void d2_core_forward_window(ANativeWindow* window) {
    if (auto fn = g_set_window.load()) fn(window);
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_disgaead2_port_NativeBridge_startCore(JNIEnv* env, jclass, jstring so, jstring eboot, jstring root, jstring log) {
    if (g_started.exchange(true)) return env->NewStringUTF("core already started");
    std::string so_path = str(env, so), eboot_path = str(env, eboot), root_path = str(env, root), log_path = str(env, log);
    void* lib = dlopen(so_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!lib) {
        g_started = false;
        const char* e = dlerror();
        return env->NewStringUTF((std::string("dlopen: ") + (e ? e : "?")).c_str());
    }
    auto run = reinterpret_cast<core_run_fn>(dlsym(lib, "d2_core_run"));
    if (!run) {
        g_started = false;
        return env->NewStringUTF("d2_core_run not found in the library");
    }
    g_set_pad = reinterpret_cast<void (*)(int, float, float, float, float)>(dlsym(lib, "d2_android_pad_set"));
    g_request_stop = reinterpret_cast<int (*)()>(dlsym(lib, "d2_core_request_stop"));
    g_result = -1000;
    // The core draws with its own Vulkan swapchain: stop the milestone-0 renderer, then give it the window.
    d2_demo_stop();
    auto set_window = reinterpret_cast<void (*)(void*)>(dlsym(lib, "d2_core_set_window"));
    if (set_window) {
        g_set_window = set_window;
        set_window(d2_current_window());
    }
    // Large stack: the game's own boot code runs on this thread.
    std::thread([run, eboot_path, root_path, log_path] {
        __android_log_print(ANDROID_LOG_INFO, TAG, "core thread started");
        int rc = run(eboot_path.c_str(), root_path.c_str(), log_path.c_str());
        __android_log_print(ANDROID_LOG_INFO, TAG, "core returned %d", rc);
        g_result = rc;
    }).detach();
    return nullptr;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_disgaead2_port_NativeBridge_coreResult(JNIEnv*, jclass) { return g_result.load(); }

// 1 = no save operation is running (the game can be ended), 0 = a save is being written, -1 = no core / old core.
extern "C" JNIEXPORT jint JNICALL
Java_org_disgaead2_port_NativeBridge_requestStop(JNIEnv*, jclass) {
    auto fn = g_request_stop.load();
    return fn ? fn() : -1;
}
