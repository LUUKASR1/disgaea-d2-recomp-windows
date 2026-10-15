#include "capture.h"
#include "graphics.h"
#include <windows.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <cstring>
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        _putenv_s("D2_CAPTURE_EVENT", "");
        SetEnvironmentVariableA("D2_SHADER_CORPUS",nullptr);
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
        SetEnvironmentVariableW(L"RSX_CAP_DIR",dir.c_str());
        require(d2_capture_directory() && path==d2_capture_directory(), "Unicode environment path");
        require(d2_graphics_configure(1280,720)==0, "configure");
        d2_capture_finish(path.c_str(),42,12);
        std::ifstream failed(dir/"complete.json"); std::string first((std::istreambuf_iterator<char>(failed)),{});
        require(first.find("failed") != std::string::npos, "missing payload must be reported");
        failed.close();
        for (const char* name : {"manifest.txt", "backbuffer.bmp"}) {
            FILE* f = d2_capture_fopen((path+"/"+name).c_str(), "wb");
            require(f != nullptr, "Unicode path"); std::fputs("data",f); std::fclose(f);
        }
        d2_capture_finish(path.c_str(),42,12);
        std::ifstream complete(dir/"complete.json"); std::string second((std::istreambuf_iterator<char>(complete)),{});
        require(second.find("complete") != std::string::npos && second.find("1280") != std::string::npos,"completion marker");
        complete.close();
        d2_capture_shader("vp","disabled",8);
        require(!std::filesystem::exists(dir/"shaders"), "shader export must be opt-in");
        d2_capture_shutdown();
        SetEnvironmentVariableA("D2_SHADER_CORPUS","1");
        const char* code="float4 main():SV_TARGET{return float4(1,0,0,1);}";
        d2_capture_shader_program("vp",code,std::strlen(code),code,16,2,0,0);
        d2_capture_shader("vp",code,std::strlen(code));
        d2_capture_shader("fp",code,std::strlen(code));
        d2_capture_shader("bad",code,std::strlen(code));
        d2_capture_shader("vp",code,262145);
        unsigned files=0;
        for(auto &entry:std::filesystem::directory_iterator(dir/"shaders")) if(entry.path().extension()==".hlsl")files++;
        require(files==2,"stage-aware shader dedup/invalid source limits");
        require(std::filesystem::file_size(dir/"shaders/vp_000.hlsl.rsx")==16,"bounded raw shader export");
        for(unsigned i=2;i<256;i++) {
            auto unique=std::string(code)+" // fixture "+std::to_string(i);
            d2_capture_shader("fp",unique.c_str(),unique.size());
        }
        std::string overflow=std::string(code)+" // overflow";
        d2_capture_shader("fp",overflow.c_str(),overflow.size());
        d2_capture_shutdown();
        std::ifstream manifest(dir/"shaders/corpus.json");
        std::string metadata((std::istreambuf_iterator<char>(manifest)),{});manifest.close();
        require(metadata.find("\"status\":\"closed\"")!=std::string::npos && metadata.find("\"capped\":true")!=std::string::npos,"bounded corpus closure");
        files=0;
        for(auto &entry:std::filesystem::directory_iterator(dir/"shaders")) if(entry.path().extension()==".hlsl")files++;
        require(files==256,"shader corpus cap");
        SetEnvironmentVariableA("D2_SHADER_CORPUS",nullptr);
        std::filesystem::remove_all(dir);
        SetEnvironmentVariableW(L"RSX_CAP_DIR",nullptr); d2_capture_shutdown();
        std::puts("Capture disabled/event/one-shot/limit/Unicode/completion checks passed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
