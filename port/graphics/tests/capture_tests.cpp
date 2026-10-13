#include "capture.h"
#include "graphics.h"
#include <windows.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <cstdio>
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        _putenv_s("D2_CAPTURE_EVENT", "");
        uint32_t index = 99;
        require(!d2_capture_next(&index, nullptr) && index == 99, "ordinary launch must not capture");
        d2_capture_shutdown();
        std::string name = "Local\\D2CaptureTest-"+std::to_string(GetCurrentProcessId());
        HANDLE event = CreateEventA(nullptr, FALSE, FALSE, name.c_str());
        require(event != nullptr, "create event");
        _putenv_s("D2_CAPTURE_EVENT", name.c_str());
        require(!d2_capture_next(&index, nullptr), "unsignaled event");
        for (uint32_t i=0;i<8;i++) {
            SetEvent(event);
            require(d2_capture_next(&index, nullptr) && index == i, "capture index");
            require(!d2_capture_next(&index, nullptr), "auto-reset prevents repeat");
        }
        SetEvent(event); require(!d2_capture_next(&index, nullptr), "capture session limit");
        d2_capture_shutdown(); CloseHandle(event);
        std::filesystem::path dir = std::filesystem::temp_directory_path()/L"D2-captura-\u00e7\u00e3";
        std::filesystem::create_directories(dir);
        auto bytes = dir.u8string(); std::string path(bytes.begin(), bytes.end());
        require(d2_graphics_configure(1280,720)==0, "configure");
        d2_capture_finish(path.c_str(),42,12);
        std::ifstream failed(dir/"complete.json"); std::string first((std::istreambuf_iterator<char>(failed)),{});
        require(first.find("failed") != std::string::npos, "missing payload must be reported");
        for (const char* name : {"manifest.txt", "backbuffer.bmp"}) {
            FILE* f = d2_capture_fopen((path+"/"+name).c_str(), "wb");
            require(f != nullptr, "Unicode path"); std::fputs("data",f); std::fclose(f);
        }
        d2_capture_finish(path.c_str(),42,12);
        std::ifstream complete(dir/"complete.json"); std::string second((std::istreambuf_iterator<char>(complete)),{});
        require(second.find("complete") != std::string::npos && second.find("1280") != std::string::npos,"completion marker");
        std::filesystem::remove_all(dir);
        std::puts("Capture disabled/event/one-shot/limit/Unicode/completion checks passed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
