/* Exported entry point of libd2core.so (the recompiled game for Android ARM64).
 * The app loads this library from its private storage, calls d2_core_run on a worker thread and reads the log. */
#include <android/log.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int d2_game_main(int argc, char** argv);   /* port/main.cpp's main() */

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
