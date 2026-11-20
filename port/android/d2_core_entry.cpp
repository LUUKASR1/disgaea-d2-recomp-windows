/* Exported entry point of libd2core.so (the recompiled game for Android ARM64).
 * The app loads this library from its private storage, calls d2_core_run on a worker thread and reads the log. */
#include <android/log.h>
#include <atomic>
#include <csignal>
#include <dlfcn.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <sched.h>
#include <map>
#include <thread>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int d2_game_main(int argc, char** argv);   /* port/main.cpp's main() */


/* Preview diagnostic: every 5 s, the busiest threads of the process (CPU % of one core, which core they last ran on)
 * and the current frequency of each CPU core. Shows whether one thread (the game's PPU thread, the renderer...) is the
 * bottleneck or everything is waiting. */
static void thread_report() {
    const long hz = sysconf(_SC_CLK_TCK);
    std::map<int, unsigned long long> last;
    auto last_t = std::chrono::steady_clock::now();
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - last_t).count();
        last_t = now;
        struct Row { double pct; int tid; std::string name; int core; };
        std::vector<Row> rows;
        double total = 0;
        if (DIR* d = opendir("/proc/self/task")) {
            while (dirent* e = readdir(d)) {
                const int tid = std::atoi(e->d_name);
                if (tid <= 0) continue;
                char path[64];
                std::snprintf(path, sizeof path, "/proc/self/task/%d/stat", tid);
                FILE* f = std::fopen(path, "r");
                if (!f) continue;
                char buf[1024] = {};
                const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
                std::fclose(f);
                buf[n] = 0;
                char* open = std::strchr(buf, '(');
                char* close = std::strrchr(buf, ')');
                if (!open || !close || close < open) continue;
                std::string name(open + 1, close);
                // After ") ": state is field 3; utime = field 14, stime = field 15, processor = field 39.
                unsigned long long ut = 0, st = 0;
                int core = -1;
                char* p = close + 2;
                int field = 3;
                for (char* tok = std::strtok(p, " "); tok; tok = std::strtok(nullptr, " "), field++) {
                    if (field == 14) ut = std::strtoull(tok, nullptr, 10);
                    else if (field == 15) st = std::strtoull(tok, nullptr, 10);
                    else if (field == 39) { core = std::atoi(tok); break; }
                }
                const unsigned long long ticks = ut + st;
                const auto it = last.find(tid);
                const double pct = (it == last.end() || elapsed <= 0) ? 0.0
                                   : 100.0 * double(ticks - it->second) / double(hz) / elapsed;
                last[tid] = ticks;
                total += pct;
                if (pct >= 3.0) rows.push_back({pct, tid, name, core});
            }
            closedir(d);
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.pct > b.pct; });
        std::fprintf(stderr, "[threads] 5s: process %.0f%% of one core |", total);
        for (size_t i = 0; i < rows.size() && i < 8; i++)
            std::fprintf(stderr, " %s(%d)=%.0f%%@cpu%d", rows[i].name.c_str(), rows[i].tid, rows[i].pct, rows[i].core);
        std::fprintf(stderr, "\n[cpufreq] MHz:");
        for (int c = 0; c < 8; c++) {
            char path[96];
            std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", c);
            if (FILE* f = std::fopen(path, "r")) {
                long khz = 0;
                if (std::fscanf(f, "%ld", &khz) == 1) std::fprintf(stderr, " %d:%ld", c, khz / 1000);
                std::fclose(f);
            }
        }
        std::fprintf(stderr, "\n");
    }
}


/* Preview profiler: samples the program counter of the two busiest threads (normally the guest PPU thread and the
 * render thread) with a real-time signal every 2 ms and every 30 s logs the hottest 64-byte code buckets as
 * "module+offset". Offsets inside libd2core.so resolve to lifted-function names with `nm -nC libd2core.so`.
 * D2_PROFILE_OFF=1 disables it. The handler only stores into a lock-free array. */
static constexpr int kProfSlots = 2, kProfRing = 16384;
static std::atomic<int> g_prof_tid[kProfSlots];
static std::atomic<unsigned> g_prof_n[kProfSlots];
static std::atomic<unsigned long long> g_prof_pc[kProfSlots][kProfRing];
static std::atomic<unsigned long long> g_prof_lr[kProfSlots][kProfRing];   // x30 at the sample: the caller of a leaf function

static void prof_handler(int, siginfo_t*, void* ctx) {
    const int tid = (int)syscall(SYS_gettid);
    for (int k = 0; k < kProfSlots; k++) {
        if (g_prof_tid[k].load(std::memory_order_relaxed) != tid) continue;
        const ucontext_t* uc = (const ucontext_t*)ctx;
#if defined(__aarch64__)
        const unsigned long long pc = uc->uc_mcontext.pc, lr = uc->uc_mcontext.regs[30];
#else
        const unsigned long long pc = 0, lr = 0; (void)uc;
#endif
        const unsigned i = g_prof_n[k].fetch_add(1, std::memory_order_relaxed);
        g_prof_pc[k][i % kProfRing].store(pc, std::memory_order_relaxed);
        g_prof_lr[k][i % kProfRing].store(lr, std::memory_order_relaxed);
        return;
    }
}

static int busiest_threads(int out[kProfSlots]) {
    // two threads with the most CPU time so far (utime+stime)
    unsigned long long best[kProfSlots] = {0, 0};
    for (int k = 0; k < kProfSlots; k++) out[k] = 0;
    if (DIR* d = opendir("/proc/self/task")) {
        while (dirent* e = readdir(d)) {
            const int tid = std::atoi(e->d_name);
            if (tid <= 0) continue;
            char path[64];
            std::snprintf(path, sizeof path, "/proc/self/task/%d/stat", tid);
            FILE* f = std::fopen(path, "r");
            if (!f) continue;
            char buf[1024] = {};
            const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
            std::fclose(f);
            buf[n] = 0;
            char* close = std::strrchr(buf, ')');
            if (!close) continue;
            unsigned long long ut = 0, st = 0;
            int field = 3;
            for (char* tok = std::strtok(close + 2, " "); tok; tok = std::strtok(nullptr, " "), field++) {
                if (field == 14) ut = std::strtoull(tok, nullptr, 10);
                else if (field == 15) { st = std::strtoull(tok, nullptr, 10); break; }
            }
            const unsigned long long t = ut + st;
            for (int k = 0; k < kProfSlots; k++)
                if (t > best[k]) {
                    for (int j = kProfSlots - 1; j > k; j--) { best[j] = best[j - 1]; out[j] = out[j - 1]; }
                    best[k] = t; out[k] = tid; break;
                }
        }
        closedir(d);
    }
    return out[0];
}

static void profiler_thread() {
    const int sig = SIGRTMIN + 2;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = prof_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(sig, &sa, nullptr);
    Dl_info self{};
    dladdr((void*)&profiler_thread, &self);
    const unsigned long long base = (unsigned long long)(uintptr_t)self.dli_fbase;
    std::this_thread::sleep_for(std::chrono::seconds(20));      // let the game boot
    for (;;) {
        int tids[kProfSlots];
        busiest_threads(tids);
        for (int k = 0; k < kProfSlots; k++) { g_prof_tid[k] = tids[k]; g_prof_n[k] = 0; }
        for (int i = 0; i < 15000; i++) {                       // 30 s at one sample per 2 ms
            for (int k = 0; k < kProfSlots; k++)
                if (tids[k] > 0) syscall(SYS_tgkill, getpid(), tids[k], sig);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        for (int k = 0; k < kProfSlots; k++) {
            const unsigned total = std::min<unsigned>(g_prof_n[k].load(), kProfRing);
            std::map<unsigned long long, unsigned> buckets;
            for (unsigned i = 0; i < total; i++) buckets[g_prof_pc[k][i].load() & ~63ull]++;
            std::vector<std::pair<unsigned, unsigned long long>> top;
            for (auto& b : buckets) top.push_back({b.second, b.first});
            std::sort(top.rbegin(), top.rend());
            std::fprintf(stderr, "[profile] thread %d: %u samples (last %u kept), hottest 64-byte buckets (d2core offsets):\n",
                         tids[k], g_prof_n[k].load(), total);
            // What a non-d2core address is: nearest exported symbol, or the mapping it lies in (/proc/self/maps).
            auto describe = [&](unsigned long long pc, char* out, size_t n) {
                Dl_info di{};
                if (dladdr((void*)(uintptr_t)pc, &di) && di.dli_fname) {
                    const unsigned long long fb = (unsigned long long)(uintptr_t)di.dli_fbase;
                    if (di.dli_sname) { std::snprintf(out, n, "%s %s+0x%llx", std::strrchr(di.dli_fname, '/') ? std::strrchr(di.dli_fname, '/') + 1 : di.dli_fname, di.dli_sname, pc - (unsigned long long)(uintptr_t)di.dli_saddr); return; }
                    std::snprintf(out, n, "%s+0x%llx", std::strrchr(di.dli_fname, '/') ? std::strrchr(di.dli_fname, '/') + 1 : di.dli_fname, pc - fb);
                    return;
                }
                std::snprintf(out, n, "?");
                if (FILE* m = std::fopen("/proc/self/maps", "r")) {
                    char line[512];
                    while (std::fgets(line, sizeof line, m)) {
                        unsigned long long lo = 0, hi = 0; char perms[8] = {}; unsigned long long off = 0; char rest[400] = {};
                        if (std::sscanf(line, "%llx-%llx %7s %llx %*s %*s %399[^\n]", &lo, &hi, perms, &off, rest) >= 4 && pc >= lo && pc < hi) {
                            const char* name = rest; while (*name == ' ') name++;
                            std::snprintf(out, n, "map[%llx-%llx %s %s]+0x%llx", lo, hi, perms, *name ? name : "anon", pc - lo);
                            break;
                        }
                    }
                    std::fclose(m);
                }
            };
            int detailed = 0;
            for (size_t i = 0; i < top.size() && i < 60; i++) {
                Dl_info di{};
                dladdr((void*)(uintptr_t)top[i].second, &di);
                if (di.dli_fbase == self.dli_fbase) {
                    std::fprintf(stderr, "[profile]   %5.1f%%  d2core+0x%llx\n", 100.0 * top[i].first / total, top[i].second - base);
                    continue;
                }
                char what[600];
                describe(top[i].second, what, sizeof what);
                std::fprintf(stderr, "[profile]   %5.1f%%  %s  (pc 0x%llx)\n", 100.0 * top[i].first / total, what, top[i].second);
                if (detailed++ < 6) {   // who called it: the most common return addresses among this bucket's samples
                    std::map<unsigned long long, unsigned> callers;
                    for (unsigned j = 0; j < total; j++)
                        if ((g_prof_pc[k][j].load() & ~63ull) == top[i].second) callers[g_prof_lr[k][j].load() & ~63ull]++;
                    std::vector<std::pair<unsigned, unsigned long long>> c;
                    for (auto& e : callers) c.push_back({e.second, e.first});
                    std::sort(c.rbegin(), c.rend());
                    for (size_t q = 0; q < c.size() && q < 4; q++) {
                        Dl_info ci{}; dladdr((void*)(uintptr_t)c[q].second, &ci);
                        if (ci.dli_fbase == self.dli_fbase) std::fprintf(stderr, "[profile]        called from d2core+0x%llx (%u samples)\n", c[q].second - base, c[q].first);
                        else { char w2[300]; describe(c[q].second, w2, sizeof w2); std::fprintf(stderr, "[profile]        called from %s (%u samples)\n", w2, c[q].first); }
                    }
                }
            }
            g_prof_tid[k] = 0;
        }
    }
}

// Big.LITTLE: the game threads were seen drifting onto the 2.0 GHz little cores. Threads created from here inherit the
// mask, so restrict this worker (and everything it starts) to the cores whose maximum frequency is near the fastest.
static void pin_to_fast_cores() {
    if (std::getenv("D2_CPU_PIN_OFF")) return;
    const long n = sysconf(_SC_NPROCESSORS_CONF);
    if (n < 2 || n > 32) return;
    std::vector<long> fmax((size_t)n, 0);
    long best = 0;
    for (long i = 0; i < n; i++) {
        char path[96];
        std::snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%ld/cpufreq/cpuinfo_max_freq", i);
        if (FILE* f = std::fopen(path, "r")) { if (std::fscanf(f, "%ld", &fmax[(size_t)i]) != 1) fmax[(size_t)i] = 0; std::fclose(f); }
        best = std::max(best, fmax[(size_t)i]);
    }
    if (best <= 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    int picked = 0;
    for (long i = 0; i < n; i++)
        if (fmax[(size_t)i] * 100 >= best * 80) { CPU_SET((int)i, &set); picked++; }   // within 80% of the fastest core
    if (picked < 2 || picked == n) return;
    const int rc = sched_setaffinity(0, sizeof set, &set);
    std::fprintf(stderr, "[cpu] pinned to %d fast cores (of %ld): %s\n", picked, n, rc == 0 ? "ok" : "failed");
}

extern "C" __attribute__((visibility("default")))
int d2_core_run(const char* eboot_path, const char* vfs_root, const char* log_path)
{
    /* One diagnostic report per test: everything the game prints goes to log_path. */
    if (log_path && *log_path) {
        FILE* out = freopen(log_path, "w", stdout);
        if (out) { setvbuf(stdout, nullptr, _IONBF, 0); }
        FILE* err = freopen(log_path, "a", stderr);
        if (err) { setvbuf(stderr, nullptr, _IONBF, 0); }
    }
    if (vfs_root && *vfs_root) setenv("PS3_VFS_ROOT", vfs_root, 1);
    /* There is no H.264 decoder on Android yet: skip the opening movies cleanly instead of stalling on them. */
    setenv("D2_MOVIE_SKIP", "1", 0);
    setenv("AUDIO_RATE", "1", 0);  // preview: audio device/mixer health report every 5 s
    setenv("AUDIO_PEAK", "1", 0);
    pin_to_fast_cores();
    if (!std::getenv("D2_THREAD_REPORT_OFF")) std::thread(thread_report).detach();
    if (!std::getenv("D2_PROFILE_OFF")) std::thread(profiler_thread).detach();

    /* The process starts in a read-only directory. Give the game a writable "disc" for saves and caches next to the log. */
    if (log_path && *log_path) {
        std::string base(log_path);
        size_t slash = base.rfind('/');
        base = slash == std::string::npos ? "." : base.substr(0, slash);
        std::string hdd0 = base + "/dev_hdd0";
        std::string tmp = base + "/tmp";
        mkdir(hdd0.c_str(), 0775);
        mkdir(tmp.c_str(), 0775);
        setenv("PS3_HDD0_ROOT", hdd0.c_str(), 1);
        setenv("TMPDIR", tmp.c_str(), 1);
        if (chdir(base.c_str()) != 0) perror("chdir");
    }
    __android_log_print(ANDROID_LOG_INFO, "d2core", "starting: eboot=%s vfs=%s", eboot_path ? eboot_path : "(null)",
                        vfs_root ? vfs_root : "(null)");
    char program[] = "d2core";
    char* argv[3] = { program, const_cast<char*>(eboot_path), nullptr };
    int rc = d2_game_main(2, argv);
    __android_log_print(ANDROID_LOG_INFO, "d2core", "game main returned %d", rc);
    fflush(stdout); fflush(stderr);
    return rc;
}
