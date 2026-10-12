/* D2_WINDOWS_SHUTDOWN_V1: close Windows host after savedata quiesces.
 * Windows guest workers currently have no cooperative stop/join implementation.
 * Do not cancel individual guest threads or free their shared audio/GPU state.
 * Terminate the host process only after the persistent savedata gate reports
 * idle and has atomically blocked admission of new save/load operations.
 * This intentionally does not invoke CRT/DLL shutdown handlers while guest
 * workers can hold locks. Host-only captures are explicitly closed first.
 * Called exclusively by the frame thread; guest and vblank/FIFO processing
 * continue normally while an admitted savedata operation finishes. */
#ifndef D2_WINDOWS_SHUTDOWN_H
#define D2_WINDOWS_SHUTDOWN_H
extern "C" int cellSaveDataHostRequestStop(void);
extern "C" void rsx_d3d12_backend_flush_host_captures(void);

static void d2_shutdown_note(const char* text, DWORD size)
{
    DWORD written = 0;
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err && err != INVALID_HANDLE_VALUE) WriteFile(err, text, size, &written, nullptr);
}

static void d2_windows_shutdown_poll(void)
{
    if (!s_stop_requested.load()) return;
    static bool waiting_noted = false;
    static bool captures_saved = false;
    if (!cellSaveDataHostRequestStop()) {
        if (!waiting_noted) {
            static const char message[] = "[D2CLOSE] waiting for admitted savedata operation\n";
            d2_shutdown_note(message, sizeof(message) - 1);
            waiting_noted = true;
        }
        return;
    }
    if (!captures_saved) {
        d2_loop_save();
        rsx_d3d12_backend_flush_host_captures();
        captures_saved = true;
        static const char message[] = "[D2CLOSE] savedata quiescent; ending Windows host process\n";
        d2_shutdown_note(message, sizeof(message) - 1);
    }
    if (!TerminateProcess(GetCurrentProcess(), (UINT)s_stop_failure.load())) {
        static const char message[] = "[D2CLOSE] TerminateProcess failed; retrying without cancelling savedata\n";
        d2_shutdown_note(message, sizeof(message) - 1);
        Sleep(100);
    }
}
#endif
