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

bool setting_on()
{
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
        if (platinum) vm_write32(platinum, 0xFFFFFFFFu);   // "no platinum", the same value the SDK writes
        ctx->gpr[3] = 0;                                    // CELL_OK: the game must not retry or show an error
        return;
    }
    const int32_t result = sceNpTrophyUnlockTrophy(context, handle, trophy,
                                                   reinterpret_cast<int32_t*>(uintptr_t(platinum)));
    ctx->gpr[3] = uint32_t(result);
}
} // namespace

extern "C" unsigned d2_trophy_lock_blocked(void) { return blocked_count.load(); }

extern "C" void d2_register_trophy_lock(void)
{
    ps3_hle_register_ctx(ps3_compute_nid("sceNpTrophyUnlockTrophy"), "sceNpTrophyUnlockTrophy", unlock_trophy);
    std::printf("[d2] trophy lock handler registered (off unless enabled)\n");
}
