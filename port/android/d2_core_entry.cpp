/* Exported entry point of libd2core.so (the recompiled game for Android ARM64).
 * The app loads this library from its private storage, calls d2_core_run on a worker thread and reads the log. */
#include <android/log.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    __android_log_print(ANDROID_LOG_INFO, "d2core", "starting: eboot=%s vfs=%s", eboot_path ? eboot_path : "(null)",
                        vfs_root ? vfs_root : "(null)");
    char program[] = "d2core";
    char* argv[3] = { program, const_cast<char*>(eboot_path), nullptr };
    int rc = d2_game_main(2, argv);
    __android_log_print(ANDROID_LOG_INFO, "d2core", "game main returned %d", rc);
    fflush(stdout); fflush(stderr);
    return rc;
}
