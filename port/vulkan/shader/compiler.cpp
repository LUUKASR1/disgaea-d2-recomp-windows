#include "compiler.h"
#ifdef D2_SHADER_GLSLANG
// Android build: glslang's HLSL front end replaces DXC (there is no DXC for ARM64 Android).
#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <mutex>
#else
#ifdef _WIN32
// clang-format off: bcrypt requires Windows typedefs first.
#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#include <oleauto.h>
#include <bcrypt.h>
// clang-format on
#else
#include <dlfcn.h>
#endif
#include <dxcapi.h>
#endif
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <regex>
#include <stdexcept>
#include <utility>
#ifndef D2_SHADER_GLSLANG
#if defined(__MINGW32__) && defined(__clang__)
// llvm-mingw builds: dxcapi.h declares interface IIDs without a
// __declspec(uuid), so __uuidof falls back to __mingw_uuidof<T>() which has no
// definition. Provide the pinned DXC IIDs explicitly.
#define D2_UUID(T, a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)                    \
  template <> const GUID &__mingw_uuidof<T>() {                                \
    static const GUID g = {a, b, c, {d0, d1, d2, d3, d4, d5, d6, d7}};         \
    return g;                                                                  \
  }
D2_UUID(IDxcCompiler3, 0x228B4687, 0x5A6A, 0x4730, 0x90, 0x0C, 0x97, 0x02, 0xB2, 0x20, 0x3F, 0x54)
D2_UUID(IDxcResult, 0x58346CDA, 0xDDE7, 0x4497, 0x94, 0x61, 0x6F, 0x87, 0xAF, 0x5E, 0x06, 0x59)
D2_UUID(IDxcBlobUtf8, 0x3DA636C9, 0xBA71, 0x4024, 0xA3, 0x01, 0x30, 0xCB, 0xF1, 0x25, 0x30, 0x5B)
D2_UUID(IDxcBlob, 0x8BA5FB08, 0x5195, 0x40E2, 0xAC, 0x58, 0x0D, 0x98, 0x9C, 0x3A, 0x01, 0x02)
#undef D2_UUID
#endif
#endif // !D2_SHADER_GLSLANG
namespace d2::vulkan::shader {
#ifndef D2_SHADER_GLSLANG
namespace {
template <class T> struct Com {
  T *p = nullptr;
  ~Com() {
    if (p)
      p->Release();
  }
  T **put() {
    if (p)
      p->Release();
    p = nullptr;
    return &p;
  }
  T *operator->() const { return p; }
};
void checked(HRESULT result, const char *message) {
  if (FAILED(result))
    throw std::runtime_error(message);
}
} // namespace
#endif
std::string prepare_rsx_hlsl(const std::string &source, Stage stage) {
  if (source.empty() || source.size() > 2 * 1024 * 1024 ||
      source.find('\0') != std::string::npos ||
      source.find("#include") != std::string::npos ||
      source.find("vk::") != std::string::npos)
    throw std::runtime_error("Invalid/unbounded RSX HLSL input");
  if (stage != Stage::vertex && stage != Stage::fragment)
    throw std::runtime_error("Unsupported shader stage");
  if (std::regex_search(source, std::regex(R"(register\s*\([^)]*,)")))
    throw std::runtime_error("Explicit HLSL register spaces are not accepted");
  const std::regex registers(R"(register\s*\(\s*([btsu])([0-9]+)\s*\))");
  std::string mapped;
  size_t cursor = 0;
  for (std::sregex_iterator i(source.begin(), source.end(), registers), end;
       i != end; ++i) {
    const auto &m = *i;
    unsigned number = std::stoul(m[2].str());
    char kind = m[1].str()[0];
    if (number > (kind == 't' ? 31u : 15u))
      throw std::runtime_error("RSX register outside binding contract");
    mapped.append(source, cursor, m.position() - cursor);
    mapped += "register(" + m[1].str() + m[2].str() + ", space" +
              (stage == Stage::vertex ? "0" : "1") + ")";
    cursor = m.position() + m.length();
  }
  mapped.append(source, cursor, std::string::npos);
  const std::regex fields(
      R"((float4\s+[A-Za-z_][A-Za-z_0-9]*\s*:\s*)(ATTR([0-9]+)|COLOR([01])|FOG|TEXCOORD([0-7]))\s*;)");
  std::string output;
  cursor = 0;
  for (std::sregex_iterator i(mapped.begin(), mapped.end(), fields), end;
       i != end; ++i) {
    const auto &m = *i;
    unsigned location = 0;
    if (m[3].matched) {
      location = std::stoul(m[3].str());
      if (location > 15 || stage != Stage::vertex)
        throw std::runtime_error("Invalid ATTR semantic");
    } else if (m[4].matched)
      location = std::stoul(m[4].str());
    else if (m[5].matched)
      location = 3 + std::stoul(m[5].str());
    else
      location = 2; // FOG
    output.append(mapped, cursor, m.position() - cursor);
    output += "[[vk::location(" + std::to_string(location) + ")]] " + m.str();
    cursor = m.position() + m.length();
  }
  output.append(mapped, cursor, std::string::npos);
  return output;
}
Interface inspect(const std::vector<uint32_t> &words) {
  if (words.size() < 5 || words[0] != 0x07230203 ||
      words.size() > 1024 * 1024 || words[3] > 1024 * 1024)
    throw std::runtime_error("Invalid SPIR-V module header");
  struct Decoration {
    int set = -1, binding = -1, location = -1;
    bool builtin = false;
  };
  std::map<uint32_t, Decoration> dec;
  std::map<uint32_t, uint32_t> variables;
  for (size_t off = 5; off < words.size();) {
    uint32_t count = words[off] >> 16, op = words[off] & 0xffff;
    if (!count || count > words.size() - off)
      throw std::runtime_error("Malformed SPIR-V instruction");
    if (op == 71 && count >= 3) {
      auto &d = dec[words[off + 1]];
      auto kind = words[off + 2];
      if (kind == 11)
        d.builtin = true;
      if (count >= 4) {
        if (kind == 33)
          d.binding = words[off + 3];
        if (kind == 34)
          d.set = words[off + 3];
        if (kind == 30)
          d.location = words[off + 3];
      }
    }
    if (op == 59 && count >= 4)
      variables[words[off + 2]] = words[off + 3];
    off += count;
  }
  Interface result;
  for (auto [id, storage] : variables) {
    auto d = dec[id];
    if (d.binding >= 0) {
      if (d.set < 0)
        throw std::runtime_error("Descriptor without set");
      for (auto r : result.resources)
        if (r.set == uint32_t(d.set) && r.binding == uint32_t(d.binding))
          throw std::runtime_error("Descriptor collision");
      result.resources.push_back({uint32_t(d.set), uint32_t(d.binding)});
    }
    if (d.location >= 0 && !d.builtin) {
      if (storage == 1)
        result.inputs.push_back(d.location);
      if (storage == 3)
        result.outputs.push_back(d.location);
    }
  }
  std::sort(result.inputs.begin(), result.inputs.end());
  std::sort(result.outputs.begin(), result.outputs.end());
  return result;
}
#ifdef D2_SHADER_GLSLANG
class Compiler::Impl {
public:
  void start() {
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
  }
  Module compile(const std::string &raw, Stage stage) {
    auto source = prepare_rsx_hlsl(raw, stage);
    const EShLanguage language =
        stage == Stage::vertex ? EShLangVertex : EShLangFragment;
    const int set = stage == Stage::vertex ? 0 : 1;
    glslang::TShader shader(language);
    const char *text = source.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceHlsl, language,
                       glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan,
                        glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    shader.setEntryPoint("main");
    shader.setSourceEntryPoint("main");
    // Same binding contract the DXC build used: b# -> b, t# -> 32+n, s# -> 64+n, u# -> 96+n, one set per stage.
    // (One shader per compile, so the global shift is enough; register(.., spaceN) already carries the set.)
    (void)set;
    shader.setShiftBinding(glslang::EResUbo, 0);
    shader.setShiftBinding(glslang::EResTexture, 32);
    shader.setShiftBinding(glslang::EResSampler, 64);
    shader.setShiftBinding(glslang::EResImage, 96);
    shader.setShiftBinding(glslang::EResUav, 96);
    if (stage == Stage::vertex)
      shader.setInvertY(true);
    else
      shader.setDxPositionW(true); // SV_Position.w is 1/w in HLSL, as DXC does
    const EShMessages messages = static_cast<EShMessages>(
        EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl | EShMsgHlslOffsets);
    Module module;
    if (!shader.parse(GetDefaultResources(), 100, false, messages)) {
      module.diagnostics = std::string(shader.getInfoLog()) + shader.getInfoDebugLog();
      throw std::runtime_error("RSX HLSL compile failed: " + module.diagnostics);
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
      module.diagnostics = std::string(program.getInfoLog()) + program.getInfoDebugLog();
      throw std::runtime_error("RSX HLSL link failed: " + module.diagnostics);
    }
    // The register shifts above are applied by the IO mapper, which the glslang CLI runs after linking.
    if (!program.mapIO()) {
      module.diagnostics = std::string(program.getInfoLog()) + program.getInfoDebugLog();
      throw std::runtime_error("RSX HLSL binding map failed: " + module.diagnostics);
    }
    glslang::SpvOptions options;
    options.disableOptimizer = true;
    options.validate = false;
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(language), module.words, &logger, &options);
    module.diagnostics = std::string(shader.getInfoLog()) + logger.getAllMessages();
    if (module.words.empty())
      throw std::runtime_error("glslang produced no SPIR-V");
    module.metadata = inspect(module.words);
    for (auto resource : module.metadata.resources) {
      bool range = resource.binding < 16 ||
                   (resource.binding >= 32 && resource.binding < 80) ||
                   (resource.binding >= 96 && resource.binding < 112);
      if (resource.set != uint32_t(stage == Stage::fragment) || !range)
        throw std::runtime_error(
            "Compiled resource outside RSX binding contract");
    }
    return module;
  }
};
#else
class Compiler::Impl {
#ifdef _WIN32
  HMODULE library = nullptr;
  std::filesystem::path directory;
#else
  void *library = nullptr;
#endif
  Com<IDxcCompiler3> compiler;

public:
  ~Impl() {
    if (compiler.p) {
      compiler.p->Release();
      compiler.p = nullptr;
    }
#ifdef _WIN32
    if (library)
      FreeLibrary(library);
    if (!directory.empty()) {
      std::error_code error;
      std::filesystem::remove(directory / L"dxcompiler.dll", error);
      std::filesystem::remove(directory, error);
    }
#else
    if (library)
      dlclose(library);
#endif
  }
  void start() {
#ifdef _WIN32
    // Only the verified DLL embedded in this executable is loaded. Never
    // search the current directory or PATH for a compiler DLL.
    HMODULE exe = GetModuleHandleW(nullptr);
    auto resource =
        FindResourceW(exe, MAKEINTRESOURCEW(201), MAKEINTRESOURCEW(10));
    if (!resource)
      throw std::runtime_error("Embedded DXC resource missing");
    auto handle = LoadResource(exe, resource);
    auto size = SizeofResource(exe, resource);
    auto data = LockResource(handle);
    if (!data || !size)
      throw std::runtime_error("Invalid embedded DXC");
    wchar_t temporary[MAX_PATH + 1];
    auto length = GetTempPathW(MAX_PATH, temporary);
    if (!length || length >= MAX_PATH)
      throw std::runtime_error("Temporary directory unavailable");
    unsigned char random[16];
    if (BCryptGenRandom(nullptr, random, sizeof(random),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
      throw std::runtime_error("Random directory creation failed");
    std::wstring name = L"D2-DXC-";
    const wchar_t hex[] = L"0123456789abcdef";
    for (auto b : random) {
      name += hex[b >> 4];
      name += hex[b & 15];
    }
    auto candidate = std::filesystem::path(temporary) / name;
    if (!CreateDirectoryW(candidate.c_str(), nullptr))
      throw std::runtime_error("Private DXC directory creation failed");
    directory = candidate;
    auto path = directory / L"dxcompiler.dll";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE)
      throw std::runtime_error("DXC extraction failed");
    DWORD written = 0;
    bool ok = WriteFile(file, data, size, &written, nullptr) != 0;
    CloseHandle(file);
    if (!ok || written != size)
      throw std::runtime_error("DXC extraction incomplete");
    library = LoadLibraryExW(path.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                 LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto create = library ? reinterpret_cast<DxcCreateInstanceProc>(
                                GetProcAddress(library, "DxcCreateInstance"))
                          : nullptr;
#else
    library = dlopen(D2_DXC_LIBRARY, RTLD_NOW | RTLD_LOCAL);
    auto create = library ? reinterpret_cast<DxcCreateInstanceProc>(
                                dlsym(library, "DxcCreateInstance"))
                          : nullptr;
#endif
    if (!create)
      throw std::runtime_error("Pinned DXC could not be loaded");
    checked(create(CLSID_DxcCompiler, __uuidof(IDxcCompiler3),
                   reinterpret_cast<void **>(compiler.put())),
            "DXC initialization failed");
  }
  Module compile(const std::string &raw, Stage stage) {
    auto source = prepare_rsx_hlsl(raw, stage);
    std::wstring set = stage == Stage::vertex ? L"0" : L"1";
    std::vector<LPCWSTR> args = {L"-HV",
                                 L"2018",
                                 L"-spirv",
                                 L"-fspv-target-env=vulkan1.1",
                                 L"-fvk-use-dx-layout",
                                 L"-E",
                                 L"main",
                                 L"-T",
                                 stage == Stage::vertex ? L"vs_6_0" : L"ps_6_0",
                                 L"-fvk-b-shift",
                                 L"0",
                                 set.c_str(),
                                 L"-fvk-t-shift",
                                 L"32",
                                 set.c_str(),
                                 L"-fvk-s-shift",
                                 L"64",
                                 set.c_str(),
                                 L"-fvk-u-shift",
                                 L"96",
                                 set.c_str()};
    if (stage == Stage::vertex)
      args.push_back(L"-fvk-invert-y");
    DxcBuffer input{source.data(), source.size(), DXC_CP_UTF8};
    Com<IDxcResult> result;
    checked(compiler->Compile(&input, args.data(), uint32_t(args.size()),
                              nullptr, __uuidof(IDxcResult),
                              reinterpret_cast<void **>(result.put())),
            "DXC invocation failed");
    Com<IDxcBlobUtf8> log;
    result->GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8),
                      reinterpret_cast<void **>(log.put()), nullptr);
    Module module;
    if (log.p)
      module.diagnostics.assign(log->GetStringPointer(),
                                log->GetStringLength());
    HRESULT status;
    checked(result->GetStatus(&status), "DXC status unavailable");
    if (FAILED(status))
      throw std::runtime_error("RSX HLSL compile failed: " +
                               module.diagnostics);
    Com<IDxcBlob> binary;
    checked(result->GetOutput(DXC_OUT_OBJECT, __uuidof(IDxcBlob),
                              reinterpret_cast<void **>(binary.put()), nullptr),
            "SPIR-V unavailable");
    if (binary->GetBufferSize() % 4)
      throw std::runtime_error("Invalid SPIR-V byte length");
    module.words.resize(binary->GetBufferSize() / 4);
    std::memcpy(module.words.data(), binary->GetBufferPointer(),
                binary->GetBufferSize());
    module.metadata = inspect(module.words);
    for (auto resource : module.metadata.resources) {
      bool range = resource.binding < 16 ||
                   (resource.binding >= 32 && resource.binding < 80) ||
                   (resource.binding >= 96 && resource.binding < 112);
      if (resource.set != uint32_t(stage == Stage::fragment) || !range)
        throw std::runtime_error(
            "Compiled resource outside RSX binding contract");
    }
    return module;
  }
};
#endif
Compiler::Compiler() : impl_(std::make_unique<Impl>()) { impl_->start(); }
Compiler::~Compiler() = default;
Module Compiler::compile(const std::string &source, Stage stage) {
  return impl_->compile(source, stage);
}
} // namespace d2::vulkan::shader
