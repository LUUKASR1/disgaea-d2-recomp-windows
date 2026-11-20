// Loads the recompiled game core (libd2core.so, generated on the user's PC from their own ISO) and runs it.
#include <jni.h>
#include "bridge.h"
#include <android/log.h>
#include <dlfcn.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include "d2_cheats.h"
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
std::atomic<int (*)()> g_save_lock{nullptr};
// Cheat API exported by the core (port/src/d2_cheats.cpp). Missing in an old core: every call then reports "unavailable".
struct CheatApi {
    int (*snapshot)(D2CheatsSnapshot*) = nullptr;
    unsigned (*fields)(int, const D2CheatsField**) = nullptr;
    int (*set_general)(const char*, uint64_t, uint64_t) = nullptr;
    int (*set_toggle)(const char*, int) = nullptr;
    int (*set_exp)(unsigned) = nullptr;
    int (*disable_all)() = nullptr;
    int (*preset)(int, unsigned) = nullptr;
    void (*select_unit)(int) = nullptr;
    int (*set_character)(unsigned, const char*, uint64_t, uint64_t) = nullptr;
};
CheatApi g_cheats;
std::atomic<void (*)()> g_save_unlock{nullptr};
std::atomic<int (*)(int*, int*)> g_trophy_event{nullptr};

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
    g_cheats.snapshot = reinterpret_cast<int (*)(D2CheatsSnapshot*)>(dlsym(lib, "d2_cheats_snapshot"));
    g_cheats.fields = reinterpret_cast<unsigned (*)(int, const D2CheatsField**)>(dlsym(lib, "d2_cheats_fields"));
    g_cheats.set_general = reinterpret_cast<int (*)(const char*, uint64_t, uint64_t)>(dlsym(lib, "d2_cheats_set_general"));
    g_cheats.set_toggle = reinterpret_cast<int (*)(const char*, int)>(dlsym(lib, "d2_cheats_set_toggle"));
    g_cheats.set_exp = reinterpret_cast<int (*)(unsigned)>(dlsym(lib, "d2_cheats_set_exp_multiplier"));
    g_cheats.disable_all = reinterpret_cast<int (*)()>(dlsym(lib, "d2_cheats_disable_all"));
    g_cheats.preset = reinterpret_cast<int (*)(int, unsigned)>(dlsym(lib, "d2_cheats_preset"));
    g_cheats.select_unit = reinterpret_cast<void (*)(int)>(dlsym(lib, "d2_cheats_select_unit"));
    g_cheats.set_character = reinterpret_cast<int (*)(unsigned, const char*, uint64_t, uint64_t)>(dlsym(lib, "d2_cheats_set_character"));
    g_save_lock = reinterpret_cast<int (*)()>(dlsym(lib, "d2_core_save_lock"));
    g_save_unlock = reinterpret_cast<void (*)()>(dlsym(lib, "d2_core_save_unlock"));
    g_trophy_event = reinterpret_cast<int (*)(int*, int*)>(dlsym(lib, "d2_core_trophy_event"));
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

// ---- Cheats (the engine lives in the core; this only passes text and numbers across) ----

// "ok=0" when the core has no cheat engine or nothing was published yet; otherwise "key=value" lines:
// ok, supported, verified, ready, version, status, hp_lock, sp_lock, one_hit, free_shop, exp, and g.<field key>=<value>.
extern "C" JNIEXPORT jstring JNICALL
Java_org_disgaead2_port_NativeBridge_cheatsState(JNIEnv* env, jclass) {
    if (!g_cheats.snapshot) return env->NewStringUTF("ok=0\nstatus=no cheat engine in this core\n");
    std::unique_ptr<D2CheatsSnapshot> snap(new D2CheatsSnapshot());
    std::memset(snap.get(), 0, sizeof(D2CheatsSnapshot));
    if (!g_cheats.snapshot(snap.get())) return env->NewStringUTF("ok=0\nstatus=waiting for the game\n");
    std::string out = "ok=1\n";
    char line[256];
    snprintf(line, sizeof line, "supported=%d\nverified=%d\nready=%d\n", snap->supported, snap->verified, snap->ready);
    out += line;
    snap->version[sizeof snap->version - 1] = 0;
    snap->status[sizeof snap->status - 1] = 0;
    out += std::string("version=") + snap->version + "\nstatus=" + snap->status + "\n";
    snprintf(line, sizeof line, "hp_lock=%d\nsp_lock=%d\none_hit=%d\nfree_shop=%d\nexp=%u\n", snap->hp_lock, snap->sp_lock,
             snap->one_hit, snap->free_shop, snap->exp_multiplier);
    out += line;
    snprintf(line, sizeof line, "gen=%llu\nunit=%d\nparty=%u\n", (unsigned long long)snap->generation, snap->unit, snap->party_count);
    out += line;
    if (g_cheats.fields) {
        const D2CheatsField* f = nullptr;
        unsigned n = g_cheats.fields(D2_CHEATS_GENERAL, &f);
        for (unsigned i = 0; f && i < n && i < D2_CHEATS_MAX_FIELDS; i++) {
            snprintf(line, sizeof line, "g.%s=%llu\n", f[i].key, (unsigned long long)snap->general[i]);
            out += line;
        }
        // Character fields of the selected unit (the values are only meaningful once a unit is selected).
        if (snap->unit >= 0) {
            n = g_cheats.fields(D2_CHEATS_CHARACTER, &f);
            for (unsigned i = 0; f && i < n && i < D2_CHEATS_MAX_FIELDS; i++) {
                snprintf(line, sizeof line, "c.%s=%llu\n", f[i].key, (unsigned long long)snap->character[i]);
                out += line;
            }
        }
    }
    for (unsigned i = 0; i < snap->party_count && i < D2_CHEATS_MAX_PARTY; i++) {
        snap->party[i].name[sizeof snap->party[i].name - 1] = 0;
        snprintf(line, sizeof line, "p.%u=%s|%u|%u\n", i, snap->party[i].name, snap->party[i].level, snap->party[i].class_id);
        out += line;
    }
    return env->NewStringUTF(out.c_str());
}

// "key|label|min|max" per general field, so the screen shows what the engine allows. Empty without the engine.
extern "C" JNIEXPORT jstring JNICALL
Java_org_disgaead2_port_NativeBridge_cheatsFields(JNIEnv* env, jclass, jint scope) {
    std::string out;
    if (g_cheats.fields && scope >= D2_CHEATS_GENERAL && scope <= D2_CHEATS_CHARACTER) {
        const D2CheatsField* f = nullptr;
        unsigned n = g_cheats.fields(scope, &f);
        char line[256];
        for (unsigned i = 0; f && i < n; i++) {
            snprintf(line, sizeof line, "%s|%s|%llu|%llu\n", f[i].key, f[i].label, (unsigned long long)f[i].minimum,
                     (unsigned long long)f[i].maximum);
            out += line;
        }
    }
    return env->NewStringUTF(out.c_str());
}

// 0 = queued, -1 = refused (unknown name or out of range), -2 = no cheat engine.
extern "C" JNIEXPORT jint JNICALL
Java_org_disgaead2_port_NativeBridge_cheatsSetToggle(JNIEnv* env, jclass, jstring name, jboolean on) {
    if (!g_cheats.set_toggle) return -2;
    const char* c = env->GetStringUTFChars(name, nullptr);
    int rc = g_cheats.set_toggle(c, on ? 1 : 0);
    env->ReleaseStringUTFChars(name, c);
    return rc;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_disgaead2_port_NativeBridge_cheatsSetGeneral(JNIEnv* env, jclass, jstring key, jlong value) {
    if (!g_cheats.set_general) return -2;
    if (value < 0) return -1;
    const char* c = env->GetStringUTFChars(key, nullptr);
    int rc = g_cheats.set_general(c, (uint64_t)value, D2_CHEATS_ANY_GENERATION);
    env->ReleaseStringUTFChars(key, c);
    return rc;
}

// Make the engine publish this party member's details (character values appear in cheatsState as c.<key>).
extern "C" JNIEXPORT void JNICALL
Java_org_disgaead2_port_NativeBridge_cheatsSelectUnit(JNIEnv*, jclass, jint unit) {
    if (g_cheats.select_unit) g_cheats.select_unit((int)unit);
}

// Edit one field of a party member. generation = the "gen" value of the state the edit was based on (stale edits are refused).
extern "C" JNIEXPORT jint JNICALL
Java_org_disgaead2_port_NativeBridge_cheatsSetCharacter(JNIEnv* env, jclass, jint unit, jstring key, jlong value, jlong generation) {
    if (!g_cheats.set_character) return -2;
    if (value < 0 || unit < 0) return -1;
    const char* c = env->GetStringUTFChars(key, nullptr);
    int rc = g_cheats.set_character((unsigned)unit, c, (uint64_t)value, (uint64_t)generation);
    env->ReleaseStringUTFChars(key, c);
    return rc;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_disgaead2_port_NativeBridge_cheatsSetExp(JNIEnv*, jclass, jint multiplier) {
    return g_cheats.set_exp ? g_cheats.set_exp((unsigned)multiplier) : -2;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_disgaead2_port_NativeBridge_cheatsDisableAll(JNIEnv*, jclass) {
    return g_cheats.disable_all ? g_cheats.disable_all() : -2;
}

// Save the current toggles as a preset (save=true) or load one (slot 0 = default, 1..3 = numbered).
extern "C" JNIEXPORT jint JNICALL
Java_org_disgaead2_port_NativeBridge_cheatsPreset(JNIEnv*, jclass, jboolean save, jint slot) {
    return g_cheats.preset ? g_cheats.preset(save ? 1 : 0, (unsigned)slot) : -2;
}

// Save slots while the game runs: 1 = gate held, 0 = a save is being written right now, -1 = old core without the gate.
extern "C" JNIEXPORT jint JNICALL
Java_org_disgaead2_port_NativeBridge_saveLock(JNIEnv*, jclass) {
    auto fn = g_save_lock.load();
    return fn ? fn() : -1;
}

extern "C" JNIEXPORT void JNICALL
Java_org_disgaead2_port_NativeBridge_saveUnlock(JNIEnv*, jclass) {
    auto fn = g_save_unlock.load();
    if (fn) fn();
}

// Next trophy event from the core as "kind|id" (kind 0 = recorded, 1 = blocked by the lock), or null when none / old core.
extern "C" JNIEXPORT jstring JNICALL
Java_org_disgaead2_port_NativeBridge_trophyEvent(JNIEnv* env, jclass) {
    auto fn = g_trophy_event.load();
    int kind = 0, id = 0;
    if (!fn || !fn(&kind, &id)) return nullptr;
    char text[32];
    snprintf(text, sizeof text, "%d|%d", kind, id);
    return env->NewStringUTF(text);
}

// 1 = no save operation is running (the game can be ended), 0 = a save is being written, -1 = no core / old core.
extern "C" JNIEXPORT jint JNICALL
Java_org_disgaead2_port_NativeBridge_requestStop(JNIEnv*, jclass) {
    auto fn = g_request_stop.load();
    return fn ? fn() : -1;
}
