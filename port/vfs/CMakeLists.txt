cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(D2IsoVfs LANGUAGES C CXX)
    include(CTest)
endif()

# Sources are vendored and unchanged. No dependency download at configure time.
add_library(d2_udfread STATIC
    vendor/libudfread/src/udfread.c
    vendor/libudfread/src/ecma167.c
    vendor/libudfread/src/default_blockinput.c)
target_include_directories(d2_udfread PUBLIC vendor/libudfread/src)
if(NOT WIN32)
    target_compile_definitions(d2_udfread PRIVATE HAVE_UNISTD_H=1 HAVE_FCNTL_H=1 _FILE_OFFSET_BITS=64)
endif()
if(MSVC)
    target_compile_definitions(d2_udfread PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_library(d2_iso_vfs STATIC iso_vfs.cpp iso_runtime.cpp)
target_compile_features(d2_iso_vfs PUBLIC cxx_std_20)
target_include_directories(d2_iso_vfs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(d2_iso_vfs PUBLIC d2_udfread)
if(WIN32)
    target_compile_definitions(d2_iso_vfs PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_definitions(d2_iso_vfs PRIVATE _FILE_OFFSET_BITS=64)
endif()
find_package(Threads REQUIRED)
target_link_libraries(d2_iso_vfs PUBLIC Threads::Threads)

add_executable(d2_iso_probe iso_probe.cpp)
target_link_libraries(d2_iso_probe PRIVATE d2_iso_vfs)
if(WIN32)
    target_link_libraries(d2_iso_probe PRIVATE shell32 comdlg32 ole32 uuid)
endif()

# Compile-check the adapted, pinned SDK filesystem without ROM/lifted code.
set(D2_ISO_SDK_DIR "" CACHE PATH "Optional pinned SDK used for adapter checks")
if(D2_ISO_SDK_DIR)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(D2_ISO_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/sdk-adapter")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/adapt_sdk.py"
        "${D2_ISO_SDK_DIR}" "${D2_ISO_GENERATED}" COMMAND_ERROR_IS_FATAL ANY)
    add_library(d2_iso_sdk_check OBJECT
        "${D2_ISO_GENERATED}/ppu_fs.cpp"
        "${D2_ISO_GENERATED}/sys_fs.c"
        "${D2_ISO_GENERATED}/cellFs.c")
    target_compile_features(d2_iso_sdk_check PRIVATE cxx_std_20)
    target_include_directories(d2_iso_sdk_check PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/sdk_stub"
        "${D2_ISO_SDK_DIR}/include" "${D2_ISO_SDK_DIR}/runtime/ppu"
        "${D2_ISO_SDK_DIR}/runtime/syscalls" "${D2_ISO_SDK_DIR}/runtime/memory"
        "${D2_ISO_SDK_DIR}/runtime/platform")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(d2_iso_sdk_check PRIVATE -fms-extensions)
    endif()
    if(NOT WIN32)
        target_compile_definitions(d2_iso_sdk_check PRIVATE _GNU_SOURCE)
    endif()
    if(BUILD_TESTING)
        add_executable(d2_iso_sdk_tests tests/sdk_integration.cpp $<TARGET_OBJECTS:d2_iso_sdk_check>)
        target_include_directories(d2_iso_sdk_tests PRIVATE
            "${D2_ISO_SDK_DIR}/include" "${D2_ISO_SDK_DIR}/runtime/ppu"
            "${D2_ISO_SDK_DIR}/libs/filesystem")
        target_link_libraries(d2_iso_sdk_tests PRIVATE d2_iso_vfs ${CMAKE_DL_LIBS})
        if(WIN32)
            target_link_libraries(d2_iso_sdk_tests PRIVATE dbghelp)
        endif()
    endif()
endif()

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(d2_iso_tests tests/iso_tests.cpp)
    target_link_libraries(d2_iso_tests PRIVATE d2_iso_vfs)
    set(fixtures "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    add_test(NAME udf_fixture COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_fixture.py" "${fixtures}")
    set_tests_properties(udf_fixture PROPERTIES FIXTURES_SETUP udf_images)
    add_test(NAME iso_vfs COMMAND d2_iso_tests "${fixtures}")
    set_tests_properties(iso_vfs PROPERTIES FIXTURES_REQUIRED udf_images TIMEOUT 90)
    add_test(NAME iso_probe_complete COMMAND d2_iso_probe "${fixtures}/complete.iso")
    set_tests_properties(iso_probe_complete PROPERTIES FIXTURES_REQUIRED udf_images TIMEOUT 90)
    if(TARGET d2_iso_sdk_tests)
        add_test(NAME iso_sdk_integration COMMAND d2_iso_sdk_tests "${fixtures}")
        set_tests_properties(iso_sdk_integration PROPERTIES FIXTURES_REQUIRED udf_images TIMEOUT 90)
    endif()
endif()
