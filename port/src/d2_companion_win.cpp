/* Windows companion pipe: lets the launcher show the cheats window and the trophy pop-up on top of the running game.
 *
 * The cheat engine and the trophy event ring live inside this process. A small background thread serves a named pipe
 * (\\.\pipe\DisgaeaD2Companion, local clients only) with a line protocol that mirrors what the Android app uses: one request
 * per message, one text reply per message. Nothing here touches guest memory; every command goes through the same
 * queued, validated d2_cheats_* calls the other front ends use.
 *
 *   ping                       -> "pong <pid>"
 *   state                      -> "ok=0\n..." or "ok=1\nkey=value\n..." (same keys as the Android bridge)
 *   fields <0|1>               -> "key|label|min|max" per line (0 = general, 1 = character)
 *   toggle <name> <0|1>        -> result code
 *   general <key> <value>      -> result code
 *   select <unit>              -> "0"
 *   char <unit> <key> <value> <generation> -> result code
 *   exp <multiplier>           -> result code
 *   disable                    -> result code
 *   preset <save|load> <slot>  -> result code
 *   trophy                     -> "kind|id" (kind 0 recorded, 1 blocked by the lock) or "none"
 *   trophylock <0|1>           -> "0"
 *   trophytest <kind> <id>     -> "0" (queues a fake trophy event to test the pop-up)
 * Result codes: 0 queued, -1 refused (unknown name / out of range), -2 not available. */
#ifdef _WIN32
#include "d2_cheats.h"
#include <windows.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

extern "C" int d2_trophy_event_pop(int* kind, int* trophy);
extern "C" void d2_trophy_lock_set(int on);
extern "C" void d2_trophy_test_event(int kind, int trophy);
extern "C" int d2_cheats_used(void);

namespace {
constexpr DWORD BUFFER = 256 * 1024;
std::atomic<bool> started{false};

bool safe_name(const char* s)
{
    size_t n = std::strlen(s);
    if (n == 0 || n > 31) return false;
    for (size_t i = 0; i < n; i++)
        if (!(std::isalnum((unsigned char)s[i]) || s[i] == '_')) return false;
    return true;
}

std::string state_text()
{
    std::unique_ptr<D2CheatsSnapshot> snap(new D2CheatsSnapshot());
    std::memset(snap.get(), 0, sizeof(D2CheatsSnapshot));
    if (!d2_cheats_snapshot(snap.get())) return "ok=0\nstatus=waiting for the game\n";
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
    snprintf(line, sizeof line, "gen=%llu\nunit=%d\nparty=%u\ncheats_used=%d\n", (unsigned long long)snap->generation, snap->unit,
             snap->party_count, d2_cheats_used());
    out += line;
    const D2CheatsField* f = nullptr;
    unsigned n = d2_cheats_fields(D2_CHEATS_GENERAL, &f);
    for (unsigned i = 0; f && i < n && i < D2_CHEATS_MAX_FIELDS; i++) {
        snprintf(line, sizeof line, "g.%s=%llu\n", f[i].key, (unsigned long long)snap->general[i]);
        out += line;
    }
    if (snap->unit >= 0) {
        n = d2_cheats_fields(D2_CHEATS_CHARACTER, &f);
        for (unsigned i = 0; f && i < n && i < D2_CHEATS_MAX_FIELDS; i++) {
            snprintf(line, sizeof line, "c.%s=%llu\n", f[i].key, (unsigned long long)snap->character[i]);
            out += line;
        }
    }
    for (unsigned i = 0; i < snap->party_count && i < D2_CHEATS_MAX_PARTY; i++) {
        snap->party[i].name[sizeof snap->party[i].name - 1] = 0;
        snprintf(line, sizeof line, "p.%u=%s|%u|%u\n", i, snap->party[i].name, snap->party[i].level, snap->party[i].class_id);
        out += line;
    }
    return out;
}

std::string fields_text(int scope)
{
    std::string out;
    if (scope < D2_CHEATS_GENERAL || scope > D2_CHEATS_CHARACTER) return out;
    const D2CheatsField* f = nullptr;
    unsigned n = d2_cheats_fields(scope, &f);
    char line[256];
    for (unsigned i = 0; f && i < n; i++) {
        snprintf(line, sizeof line, "%s|%s|%llu|%llu\n", f[i].key, f[i].label, (unsigned long long)f[i].minimum,
                 (unsigned long long)f[i].maximum);
        out += line;
    }
    return out;
}

std::string code(int rc) { return std::to_string(rc); }

std::string handle(const std::string& request)
{
    char cmd[16] = {0}, a[40] = {0}, b[40] = {0}, c[40] = {0}, d[40] = {0};
    int got = std::sscanf(request.c_str(), "%15s %39s %39s %39s %39s", cmd, a, b, c, d);
    if (got < 1) return "";
    const std::string k = cmd;
    if (k == "ping") return "pong " + std::to_string(GetCurrentProcessId());
    if (k == "state") return state_text();
    if (k == "fields") return fields_text(got >= 2 ? std::atoi(a) : -1);
    if (k == "toggle") return got >= 3 && safe_name(a) ? code(d2_cheats_set_toggle(a, std::atoi(b) ? 1 : 0)) : "-1";
    if (k == "general") {
        if (got < 3 || !safe_name(a) || b[0] == '-') return "-1";
        return code(d2_cheats_set_general(a, std::strtoull(b, nullptr, 10), D2_CHEATS_ANY_GENERATION));
    }
    if (k == "select") { d2_cheats_select_unit(got >= 2 ? std::atoi(a) : -1); return "0"; }
    if (k == "char") {
        if (got < 5 || !safe_name(b) || a[0] == '-' || c[0] == '-') return "-1";
        return code(d2_cheats_set_character((unsigned)std::atoi(a), b, std::strtoull(c, nullptr, 10), std::strtoull(d, nullptr, 10)));
    }
    if (k == "exp") return got >= 2 && a[0] != '-' ? code(d2_cheats_set_exp_multiplier((unsigned)std::atoi(a))) : "-1";
    if (k == "disable") return code(d2_cheats_disable_all());
    if (k == "preset") {
        if (got < 3 || b[0] == '-') return "-1";
        return code(d2_cheats_preset(std::strcmp(a, "save") == 0 ? 1 : 0, (unsigned)std::atoi(b)));
    }
    if (k == "trophy") {
        int kind = 0, id = 0;
        if (!d2_trophy_event_pop(&kind, &id)) return "";
        return std::to_string(kind) + "|" + std::to_string(id);
    }
    if (k == "trophytest") { d2_trophy_test_event(got >= 2 && std::atoi(a) ? 1 : 0, got >= 3 ? std::atoi(b) : 1); return "0"; }
    if (k == "trophylock") { d2_trophy_lock_set(got >= 2 && std::atoi(a) ? 1 : 0); return "0"; }
    return "-1";
}

void serve()
{
    const char* name = "\\\\.\\pipe\\DisgaeaD2Companion";
    std::unique_ptr<char[]> buffer(new char[BUFFER]);
    for (;;) {
        HANDLE pipe = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       1, BUFFER, BUFFER, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) { Sleep(2000); continue; }
        if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
            for (;;) {
                DWORD n = 0;
                if (!ReadFile(pipe, buffer.get(), BUFFER - 1, &n, nullptr)) break;
                buffer[n] = 0;
                std::string request(buffer.get(), n);
                while (!request.empty() && (request.back() == '\n' || request.back() == '\r')) request.pop_back();
                std::string reply = handle(request);
                if (reply.empty()) reply = "none";   /* never write an empty message: some clients would block on it */
                if (reply.size() > BUFFER - 16) reply.resize(BUFFER - 16);
                DWORD written = 0;
                if (!WriteFile(pipe, reply.data(), (DWORD)reply.size(), &written, nullptr)) break;
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}
} // namespace

extern "C" void d2_companion_start(void)
{
    if (started.exchange(true)) return;
    std::thread(serve).detach();
    std::printf("[d2] companion pipe ready (cheats window / trophy pop-up from the launcher)\n");
}
#else
extern "C" void d2_companion_start(void) {}
#endif
