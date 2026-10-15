#include "capture.h"
#include "graphics.h"
#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <set>
#include <cstring>
#include <bcrypt.h>

namespace {
HANDLE request = nullptr;
bool initialized = false, was_down = false;
uint32_t count = 0;
std::string directory;
constexpr uint32_t limit = 8;
std::wstring wide(const char* value) {
    if (!value) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (!size) return {};
    std::wstring result(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result.data(), size)) return {};
    result.resize(size-1); return result;
}
bool nonempty(const std::string& path) {
    auto name = wide(path.c_str());
    WIN32_FILE_ATTRIBUTE_DATA data{};
    return !name.empty() && GetFileAttributesExW(name.c_str(), GetFileExInfoStandard, &data) &&
        !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (data.nFileSizeHigh || data.nFileSizeLow);
}
}

#include "shader_corpus.inc"

extern "C" int d2_capture_next(uint32_t* index, void* window) {
    if (!initialized) {
        initialized = true;
        const char* name = std::getenv("D2_CAPTURE_EVENT");
        if (name && *name) {
            request = OpenEventA(SYNCHRONIZE, FALSE, name);
            if (!request) std::fprintf(stderr, "[D2CAPTURE] cannot open request event: %lu\n", GetLastError());
        }
    }
    if (!request || count >= limit || !index) return 0;
    bool down = window && GetForegroundWindow() == static_cast<HWND>(window) &&
        (GetAsyncKeyState(VK_F8) & 0x8000);
    bool key = down && !was_down;
    was_down = down;
    bool signaled = WaitForSingleObject(request, 0) == WAIT_OBJECT_0;
    if (!key && !signaled) return 0;
    *index = count++;
    std::fprintf(stderr, "[D2CAPTURE] capturing frame %u/%u\n", count, limit);
    return 1;
}
extern "C" void d2_capture_shutdown(void) {
    corpus_close();
    if (request) CloseHandle(request);
    request = nullptr; initialized = false; was_down = false; count = 0; directory.clear();
}
extern "C" const char* d2_capture_directory(void) {
    if (!directory.empty()) return directory.c_str();
    DWORD size = GetEnvironmentVariableW(L"RSX_CAP_DIR", nullptr, 0);
    if (!size) return nullptr;
    std::wstring path(size, L'\0');
    DWORD copied = GetEnvironmentVariableW(L"RSX_CAP_DIR", path.data(), size);
    if (!copied || copied >= size) return nullptr;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (!bytes) return nullptr;
    directory.resize(bytes);
    if (!WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, directory.data(), bytes, nullptr, nullptr)) {
        directory.clear(); return nullptr;
    }
    directory.resize(bytes-1); return directory.c_str();
}
extern "C" FILE* d2_capture_fopen(const char* path, const char* mode) {
    auto p = wide(path), m = wide(mode);
    return p.empty() || m.empty() ? nullptr : _wfopen(p.c_str(), m.c_str());
}
extern "C" void d2_capture_mkdir(const char* path) {
    auto p = wide(path); if (!p.empty()) CreateDirectoryW(p.c_str(), nullptr);
}
extern "C" void d2_capture_finish(const char* directory, uint64_t frame, uint32_t draws) {
    // Presentation diagnostics must not replace the dedicated draw result.
    char draw_capture[4] = {};
    if (GetEnvironmentVariableA("D2_DRAW_CAPTURE", draw_capture, sizeof(draw_capture)) == 1 && draw_capture[0] == '1') return;
    std::string base = directory;
    bool complete = nonempty(base+"/manifest.txt") && nonempty(base+"/backbuffer.bmp");
    FILE* status = d2_capture_fopen((base+"/complete.json").c_str(), "wb");
    if (!status) { std::fprintf(stderr, "[D2CAPTURE] cannot write completion marker\n"); return; }
    std::fprintf(status, "{\"schema\":1,\"status\":\"%s\",\"frame\":%llu,\"draws\":%u,"
        "\"render_width\":%u,\"render_height\":%u,\"output_width\":%u,\"output_height\":%u,"
        "\"image\":\"internal-scene-with-HUD\",\"limit_per_session\":8}\n",
        complete ? "complete" : "failed", static_cast<unsigned long long>(frame), draws,
        d2_graphics_render_width(), d2_graphics_render_height(),
        d2_graphics_output_width(), d2_graphics_output_height());
    std::fclose(status);
}
