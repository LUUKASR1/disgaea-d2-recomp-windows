/* Exported entry point of libd2core.so (the recompiled game for Android ARM64).
 * The app loads this library from its private storage, calls d2_core_run on a worker thread and reads the log. */
#include <android/log.h>
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
