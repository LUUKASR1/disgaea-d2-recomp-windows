// Android platform and SDK primitive tests. No full runtime or game assets.
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <mutex>
#include <thread>
#include "runtime_probe.h"
#include "elf_probe.h"
#include "dispatch_probe.h"
#include "vulkan_renderer.h"

namespace {
constexpr const char *tag = "D2Android";
struct State {
    android_app *app = nullptr;
    std::unique_ptr<d2::vulkan::Renderer> renderer;
    bool resumed = false, focused = false, resize_pending = false;
    std::string status = "waiting-for-window", error, adapter;
    uint64_t frames = 0, starts = 0, stops = 0, resumes = 0, pauses = 0;
    uint64_t gamepad_keys = 0, joystick_events = 0, touch_downs = 0;
    int last_key = 0, width = 0, height = 0;
    uint32_t api = 0;
    float stick_x = 0, stick_y = 0;
    mutable std::mutex core_lock;
    std::string core_report = "core_status=running\n";
    std::thread core_worker;

    void startCoreProbe() {
        try {
        core_worker = std::thread([this] {
            char result[16384] = {};
            int core_result = d2_runtime_probe_run(result, sizeof result);
            std::string combined = result;
            // One worker, sequential ownership: core probe releases its guest VM
            // before the isolated ELF subset creates a new one. No game session.
            if (core_result == 0) {
                int elf_result = d2_elf_probe_run(app->activity->internalDataPath, result, sizeof result);
                combined += result;
                if (elf_result == 0) {
                    d2_dispatch_probe_run(app->activity->internalDataPath, result, sizeof result);
                    combined += result;
                } else combined += "dispatch_status=skipped-elf-failed\n";
            } else combined += "elf_status=skipped-core-failed\n";
            std::lock_guard<std::mutex> lock(core_lock);
            core_report = combined;
        });
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> lock(core_lock);
            core_report = std::string("core_status=failed\ncore_error=") + e.what() + '\n';
        }
    }
    ~State() { if (core_worker.joinable()) core_worker.join(); }

    void report() const {
        // Atomic replacement prevents Java's share button reading half a report.
        const std::string path = std::string(app->activity->internalDataPath) + "/android-vulkan-report.txt";
        const std::string temporary = path + ".tmp";
        std::ofstream out(temporary, std::ios::trunc);
        out << "Disgaea D2 Android bootstrap 0.0.12\n"
            << "game_runtime_linked=false\ngameplay_available=false\ndocument_picker_integrated=true\n"
#if defined(__aarch64__)
            << "abi=arm64-v8a\n"
#else
            << "abi=x86_64-ci-smoke-only\n"
#endif
            << "status=" << status << "\nerror=" << error << "\nadapter=" << adapter
            << "\nvulkan_api=" << api << "\nwindow=" << width << 'x' << height
            << "\npresented_test_frames=" << frames << "\nsurface_starts=" << starts
            << "\nsurface_stops=" << stops << "\nresumes=" << resumes << "\npauses=" << pauses
            << "\ngamepad_key_downs=" << gamepad_keys << "\nlast_gamepad_keycode=" << last_key
            << "\njoystick_events=" << joystick_events << "\nlast_stick=" << stick_x << ',' << stick_y
            << "\ntouch_downs=" << touch_downs << '\n';
        {
            std::lock_guard<std::mutex> lock(core_lock);
            out << core_report;
        }
        out.flush();
        const bool ok = out.good();
        out.close();
        if (ok) std::rename(temporary.c_str(), path.c_str());
    }
    void stop() {
        if (renderer) {
            // native_app_glue keeps ANativeWindow alive until TERM_WINDOW returns.
            // Destroy swapchain/surface before the framework releases that window.
            renderer.reset();
            ++stops;
        }
        resize_pending = false;
    }
    void fail(const std::exception &e) {
        error = e.what();
        __android_log_print(ANDROID_LOG_ERROR, tag, "%s", error.c_str());
        stop();
        status = "vulkan-error";
        report();
    }
    void start() {
        stop();
        if (!app->window) return;
        try {
            renderer = std::make_unique<d2::vulkan::Renderer>();
            renderer->start(app->window, false); // No validation layer required on users' devices.
            const auto info = renderer->info();
            adapter = info.adapter;
            api = info.api_version;
            // Upload once; steady frames don't recreate buffers or pipelines.
            using d2::vulkan::Vertex;
            const std::array<Vertex, 3> triangle{{
                {{{-.6f, .65f, .5f, 1.f}}, {{1.f, .2f, .3f, 1.f}}},
                {{{.6f, .65f, .5f, 1.f}}, {{.3f, .6f, 1.f, 1.f}}},
                {{{0.f, -.65f, .5f, 1.f}}, {{1.f, .7f, .3f, 1.f}}}
            }};
            renderer->setGeometry(triangle);
            const std::array<uint8_t, 4> white{255,255,255,255};
            renderer->setTexture(1, 1, white);
            ++starts;
            resize_pending = true;
            status = "surface-ready";
            error.clear();
            report();
        } catch (const std::exception &e) { fail(e); }
    }
    bool active() const { return resumed && focused && renderer && app->window; }
    void frame() {
        if (!active()) return;
        try {
            const int w = ANativeWindow_getWidth(app->window), h = ANativeWindow_getHeight(app->window);
            if (w <= 0 || h <= 0) return;
            if (resize_pending || renderer->needsResize() || w != width || h != height) {
                if (!renderer->resize(w, h, true)) return; // FIFO/VSync; no uncapped hot loop.
                width = w; height = h;
                resize_pending = false;
            }
            if (renderer->draw()) {
                const bool first_present = status != "presenting-test-triangle";
                ++frames;
                status = "presenting-test-triangle";
                if (first_present || frames % 120 == 0) report();
            } else resize_pending = true;
        } catch (const std::exception &e) { fail(e); }
    }
};

void command(android_app *app, int32_t cmd) {
    auto &s = *static_cast<State *>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: s.start(); break;
        case APP_CMD_TERM_WINDOW: s.stop(); s.status = "window-released"; s.report(); break;
        case APP_CMD_RESUME: s.resumed = true; ++s.resumes; s.report(); break;
        case APP_CMD_PAUSE: s.resumed = false; ++s.pauses; s.report(); break;
        case APP_CMD_GAINED_FOCUS: s.focused = true; break;
        case APP_CMD_LOST_FOCUS: s.focused = false; s.stick_x = s.stick_y = 0; s.report(); break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_CONTENT_RECT_CHANGED: s.resize_pending = true; break;
        default: break;
    }
}

int32_t input(android_app *app, AInputEvent *event) {
    auto &s = *static_cast<State *>(app->userData);
    if (!s.resumed || !s.focused) return 0;
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        const int key = AKeyEvent_getKeyCode(event);
        const bool pad = (key >= AKEYCODE_BUTTON_A && key <= AKEYCODE_BUTTON_MODE) ||
                         (key >= AKEYCODE_DPAD_UP && key <= AKEYCODE_DPAD_CENTER);
        if (!pad) return 0; // Back, system volume etc. remain with Android.
        if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN && AKeyEvent_getRepeatCount(event) == 0) {
            ++s.gamepad_keys; s.last_key = key; s.report();
        }
        return 1;
    }
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        if ((AInputEvent_getSource(event) & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK &&
            AMotionEvent_getPointerCount(event)) {
            ++s.joystick_events;
            s.stick_x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            s.stick_y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            return 1;
        }
        if ((AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_DOWN) {
            ++s.touch_downs; s.report();
        }
    }
    return 0;
}
} // namespace

extern "C" void android_main(android_app *app) {
    State state;
    state.app = app;
    app->userData = &state;
    app->onAppCmd = command;
    app->onInputEvent = input;
    state.report();
    state.startCoreProbe();
    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source *source = nullptr;
        const int result = ALooper_pollOnce(state.active() ? 0 : -1, nullptr, &events,
                                            reinterpret_cast<void **>(&source));
        if (result == ALOOPER_POLL_ERROR) break;
        if (source) source->process(app, source);
        if (app->destroyRequested) break;
        state.frame();
    }
    state.stop();
    if (state.core_worker.joinable()) state.core_worker.join();
    state.status = "closed";
    state.report();
    app->userData = nullptr;
}
