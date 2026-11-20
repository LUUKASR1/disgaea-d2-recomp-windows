/* Optional trophy lock. When the player turns it on and has used a cheat in this session, trophy unlocks are not
 * recorded, so cheated progress does not earn trophies. The option is OFF by default: with it off the call goes straight
 * to the SDK's own sceNpTrophyUnlockTrophy and nothing changes.
 *
 * The handler is registered as a context-aware HLE function, which the runtime dispatches before its generic table, so
 * the pinned SDK is not modified. The setting comes from the D2_TROPHY_LOCK environment variable (Windows launcher) or
 * from a "trophylock=1" line in d2_android.cfg in the working directory (Android app), re-read every couple of seconds. */
#include "ppu_recomp.h"
#include "ps3emu/nid.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));
extern "C" int32_t sceNpTrophyUnlockTrophy(int32_t context, int32_t handle, int32_t trophyId, int32_t* platinumId);
extern "C" int d2_cheats_used(void);
extern "C" void vm_write32(uint64_t addr, uint32_t value);

namespace {
std::atomic<unsigned> blocked_count{0};
std::atomic<int> lock_override{-1};   /* -1 = use the env/config setting; 0/1 = set live by the Windows companion */

/* Small lock-free event ring read by the host app (Android pop-up): kind 0 = trophy recorded, 1 = blocked by the lock. */
constexpr unsigned EVENT_CAP = 16;
std::atomic<uint32_t> events[EVENT_CAP];   /* (kind << 16) | (trophy id & 0xFFFF), plus 0x80000000 = present */
std::atomic<unsigned> ev_head{0}, ev_tail{0};

void push_event(int kind, int trophy)
{
    const unsigned head = ev_head.load();
    if (head - ev_tail.load() >= EVENT_CAP) return;   /* nobody is listening: drop */
    events[head % EVENT_CAP].store(0x80000000u | (uint32_t(kind) << 16) | (uint32_t(trophy) & 0xFFFFu));
    ev_head.store(head + 1);
}

bool setting_on()
{
    const int forced = lock_override.load();
    if (forced >= 0) return forced != 0;
    static std::atomic<int64_t> next_check_ms{0};
    static std::atomic<bool> cached{false};
    static std::atomic<bool> env_on{false}, env_read{false};
    if (!env_read.exchange(true)) {
        const char* value = std::getenv("D2_TROPHY_LOCK");
        env_on = value && value[0] && std::strcmp(value, "0") != 0;
    }
    if (env_on) return true;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now < next_check_ms.load()) return cached.load();
    next_check_ms = now + 2000;
    bool on = false;
    std::ifstream cfg("d2_android.cfg");
    std::string line;
    while (std::getline(cfg, line))
        if (line.rfind("trophylock=", 0) == 0) on = line.size() > 11 && line[11] == '1';
    cached = on;
    return on;
}

void unlock_trophy(ppu_context* ctx)
{
    const int32_t context = int32_t(ctx->gpr[3]), handle = int32_t(ctx->gpr[4]), trophy = int32_t(ctx->gpr[5]);
    const uint32_t platinum = uint32_t(ctx->gpr[6]);
    if (setting_on() && d2_cheats_used()) {
        const unsigned n = ++blocked_count;
        std::printf("[d2] trophy lock: trophy #%d not recorded (cheats were used this session; %u blocked so far)\n", trophy, n);
        std::fflush(stdout);
        push_event(1, trophy);
        if (platinum) vm_write32(platinum, 0xFFFFFFFFu);   // "no platinum", the same value the SDK writes
        ctx->gpr[3] = 0;                                    // CELL_OK: the game must not retry or show an error
        return;
    }
    const int32_t result = sceNpTrophyUnlockTrophy(context, handle, trophy,
                                                   reinterpret_cast<int32_t*>(uintptr_t(platinum)));
    if (result == 0) push_event(0, trophy);   // newly recorded (already-unlocked and error results show nothing)
    ctx->gpr[3] = uint32_t(result);
}
} // namespace

/* Pops the oldest pending trophy event: returns 1 and fills *kind / *trophy, or 0 when there is none. */
extern "C" int d2_trophy_event_pop(int* kind, int* trophy)
{
    const unsigned tail = ev_tail.load();
    if (tail == ev_head.load()) return 0;
    const uint32_t v = events[tail % EVENT_CAP].load();
    ev_tail.store(tail + 1);
    if (kind) *kind = int((v >> 16) & 0xFF);
    if (trophy) *trophy = int(v & 0xFFFFu);
    return 1;
}

/* Live switch for the Windows companion (cheats window). */
extern "C" void d2_trophy_lock_set(int on) { lock_override = on ? 1 : 0; }

extern "C" unsigned d2_trophy_lock_blocked(void) { return blocked_count.load(); }

extern "C" void d2_register_trophy_lock(void)
{
    ps3_hle_register_ctx(ps3_compute_nid("sceNpTrophyUnlockTrophy"), "sceNpTrophyUnlockTrophy", unlock_trophy);
    std::printf("[d2] trophy lock handler registered (off unless enabled)\n");
}
