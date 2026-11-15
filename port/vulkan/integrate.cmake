# Experimental Vulkan RSX backend (milestone M1). Included from port/CMakeLists.txt
# only when -DD2_VULKAN_GAME_BACKEND=ON on Windows. D3D12 stays the default;
# the backend is chosen at run time and needs D2_VULKAN_EXPERIMENTAL=1.
include(FetchContent)
set(D2_VULKAN_HEADERS_DIR "" CACHE PATH "Existing Khronos Vulkan-Headers checkout")
if(NOT TARGET Vulkan::Headers)
    if(D2_VULKAN_HEADERS_DIR)
        add_subdirectory("${D2_VULKAN_HEADERS_DIR}" "${CMAKE_BINARY_DIR}/vulkan_headers" EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(vulkan_headers GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
            GIT_TAG 29f979ee5aa58b7b005f805ea8df7a855c39ff37)
        FetchContent_MakeAvailable(vulkan_headers)
    endif()
endif()
# M2: shader compilation. Same pinned DXC and wrapper as the standalone
# probes; the DLL is embedded into the game executable (resource 201) and only
# extracted to a private temp directory at run time.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(D2_DXC_CACHE "${CMAKE_BINARY_DIR}/dxc-cache" CACHE PATH "Pinned DXC archive cache")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/shader/fetch_dxc.py"
    "${D2_DXC_CACHE}" windows COMMAND_ERROR_IS_FATAL ANY)
set(D2_DXC_ROOT "${D2_DXC_CACHE}/windows")
add_library(d2_shader_compiler STATIC "${CMAKE_CURRENT_LIST_DIR}/shader/compiler.cpp")
target_compile_features(d2_shader_compiler PUBLIC cxx_std_20)
target_include_directories(d2_shader_compiler
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/shader"
    PRIVATE "${D2_DXC_ROOT}/include")
target_compile_definitions(d2_shader_compiler PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(NOT MSVC)
    target_compile_options(d2_shader_compiler PRIVATE -fms-extensions)
endif()
target_link_libraries(d2_shader_compiler PRIVATE bcrypt)
file(WRITE "${CMAKE_BINARY_DIR}/embedded_dxc.rc" "201 RCDATA \"${D2_DXC_ROOT}/dxcompiler.dll\"\n")
set(D2_VULKAN_DXC_RC "${CMAKE_BINARY_DIR}/embedded_dxc.rc" CACHE INTERNAL "")
add_library(d2_vulkan_game STATIC
    "${CMAKE_CURRENT_LIST_DIR}/vulkan_renderer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rsx_vulkan_backend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader/fixtures.cpp")
target_compile_features(d2_vulkan_game PUBLIC cxx_std_20)
target_include_directories(d2_vulkan_game
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/shader"
    PRIVATE
        "${PS3RECOMP_DIR}/include"
        "${PS3RECOMP_DIR}/runtime/ppu"
        "${PS3RECOMP_DIR}/runtime/memory"
        "${PS3RECOMP_DIR}/runtime/platform"
        "${PS3RECOMP_DIR}/libs/video")
target_compile_definitions(d2_vulkan_game
    PUBLIC D2_VULKAN_GAME_BACKEND
    PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(d2_vulkan_game PUBLIC d2_shader_compiler PRIVATE Vulkan::Headers user32)
