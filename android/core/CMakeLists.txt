cmake_minimum_required(VERSION 3.22)
project(D2CorePrimitives LANGUAGES C CXX)
set(D2_CORE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../android-runtime-core" CACHE PATH "Prepared immutable SDK core")
file(READ "${D2_CORE_SDK_DIR}/core-provenance.json" receipt)
string(JSON tree GET "${receipt}" sdk_tree)
if(NOT tree STREQUAL "a23f74ffccaa440b686dc06694379b6362431038")
    message(FATAL_ERROR "Unexpected SDK core tree")
endif()
string(JSON count LENGTH "${receipt}" files)
math(EXPR last "${count} - 1")
foreach(i RANGE 0 ${last})
    string(JSON path MEMBER "${receipt}" files ${i})
    string(JSON expected GET "${receipt}" files "${path}")
    file(SHA256 "${D2_CORE_SDK_DIR}/${path}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Prepared SDK core changed: ${path}")
    endif()
endforeach()
# Reuse the desktop reader with a descriptor entry. Keep LGPL libudfread as
# a separate shared library in the APK, built from unchanged vendored sources.
set(vfs "${CMAKE_CURRENT_LIST_DIR}/../../port/vfs")
if(ANDROID)
    set(udf_type SHARED)
else()
    set(udf_type STATIC)
endif()
add_library(d2_android_udfread ${udf_type}
    "${vfs}/vendor/libudfread/src/udfread.c"
    "${vfs}/vendor/libudfread/src/ecma167.c"
    "${vfs}/vendor/libudfread/src/default_blockinput.c")
target_include_directories(d2_android_udfread PUBLIC "${vfs}/vendor/libudfread/src")
target_compile_definitions(d2_android_udfread PRIVATE HAVE_FCNTL_H=1 HAVE_UNISTD_H=1 _FILE_OFFSET_BITS=64)
set_target_properties(d2_android_udfread PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(ANDROID)
    target_link_options(d2_android_udfread PRIVATE "-Wl,-z,max-page-size=16384")
endif()
add_library(d2_android_disc STATIC "${vfs}/iso_vfs.cpp" "${vfs}/iso9660_reader.cpp")
target_compile_features(d2_android_disc PUBLIC cxx_std_20)
target_include_directories(d2_android_disc PUBLIC "${vfs}")
target_link_libraries(d2_android_disc PUBLIC d2_android_udfread)
set_target_properties(d2_android_disc PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(ppu "${D2_CORE_SDK_DIR}/runtime/ppu")
set(spu "${D2_CORE_SDK_DIR}/runtime/spu")
set(platform "${D2_CORE_SDK_DIR}/runtime/platform")
add_library(d2_android_core STATIC
    "${platform}/win32_compat.c" "${platform}/guest_poll.c" "${spu}/spu_coherency.c"
    "${spu}/tests/test_spu_helpers.c" "${spu}/tests/test_spu_numeric_semantics.c"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/runtime_probe.c"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/elf_probe.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/dispatch_probe.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/dispatch_memory.c"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/services_bridge.c"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/descriptor_file.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/document_probe.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/file_services.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/file_bundle_test.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/runtime_bundle.c"
    "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/runtime_bundle_test.cpp"
    "${D2_CORE_SDK_DIR}/runtime/syscalls/sys_memory.c"
    "${D2_CORE_SDK_DIR}/runtime/syscalls/sys_mutex.c"
    "${D2_CORE_SDK_DIR}/runtime/syscalls/sys_semaphore.c"
    "${D2_CORE_SDK_DIR}/runtime/syscalls/sys_ppu_thread.c"
    "${D2_CORE_SDK_DIR}/runtime/syscalls/job_complete_weak.c"
    "${D2_CORE_SDK_DIR}/runtime/syscalls/sys_timer.c"
    "${D2_CORE_SDK_DIR}/libs/system/cellRtc.c"
    "${D2_CORE_SDK_DIR}/generated/ppu_recomp.c"
    "${D2_CORE_SDK_DIR}/generated/ppu_elf_subset.cpp")
# Compile the full, unmodified SDK modules; retain only the selected clock
# handlers at link time. Timer/event workers and calendar helpers stay unlinked.
set_source_files_properties("${D2_CORE_SDK_DIR}/runtime/syscalls/sys_timer.c"
    "${D2_CORE_SDK_DIR}/runtime/syscalls/sys_memory.c"
    "${D2_CORE_SDK_DIR}/runtime/syscalls/sys_mutex.c"
    "${D2_CORE_SDK_DIR}/runtime/syscalls/sys_semaphore.c"
    "${D2_CORE_SDK_DIR}/runtime/syscalls/sys_ppu_thread.c"
    "${D2_CORE_SDK_DIR}/libs/system/cellRtc.c"
    PROPERTIES COMPILE_OPTIONS "-ffunction-sections;-fdata-sections")
target_link_options(d2_android_core INTERFACE "-Wl,--gc-sections")
set(lifted_definitions
    ppu_context=d2_fixture_context ppu_vr=d2_fixture_vr
    function_table=d2_fixture_function_table function_table_count=d2_fixture_function_table_count
    vm_read8=d2_lift_read8 vm_read16=d2_lift_read16 vm_read32=d2_lift_read32 vm_read64=d2_lift_read64
    vm_write8=d2_lift_write8 vm_write16=d2_lift_write16 vm_write32=d2_lift_write32 vm_write64=d2_lift_write64
    ps3_indirect_call=d2_lift_indirect_call lv2_syscall=d2_lift_syscall
    ps3_hle_call=d2_lift_hle_call g_trampoline_fn=d2_lift_trampoline)
set_source_files_properties("${D2_CORE_SDK_DIR}/generated/ppu_recomp.c"
    PROPERTIES LANGUAGE CXX COMPILE_DEFINITIONS "${lifted_definitions}")
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp/dispatch_probe.cpp"
    PROPERTIES COMPILE_DEFINITIONS "${lifted_definitions}")
set_source_files_properties("${spu}/tests/test_spu_helpers.c" PROPERTIES COMPILE_DEFINITIONS "main=d2_sdk_spu_helpers_test")
set_source_files_properties("${spu}/tests/test_spu_numeric_semantics.c" PROPERTIES COMPILE_DEFINITIONS "main=d2_sdk_spu_numeric_test")
set_target_properties(d2_android_core PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_compile_features(d2_android_core PRIVATE c_std_17)
target_compile_features(d2_android_core PRIVATE cxx_std_17)
target_compile_definitions(d2_android_core PRIVATE _GNU_SOURCE)
target_compile_options(d2_android_core PRIVATE -UNDEBUG)
target_include_directories(d2_android_core PRIVATE "${D2_CORE_SDK_DIR}" "${D2_CORE_SDK_DIR}/include"
    "${platform}" "${spu}" "${ppu}" "${D2_CORE_SDK_DIR}/generated")
target_include_directories(d2_android_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp")
target_link_libraries(d2_android_core PRIVATE d2_android_disc m ${CMAKE_DL_LIBS})
if(NOT ANDROID)
    find_package(Threads REQUIRED)
    target_link_libraries(d2_android_core PRIVATE Threads::Threads)
    add_executable(d2_core_probe_cli core_cli.c)
    target_link_libraries(d2_core_probe_cli PRIVATE d2_android_core)
    add_executable(d2_document_probe_cli document_cli.cpp)
    target_link_libraries(d2_document_probe_cli PRIVATE d2_android_core)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    enable_testing()
    add_test(NAME descriptor_disc_integration COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/test_disc_descriptor.py" "$<TARGET_FILE:d2_document_probe_cli>")
    set_tests_properties(descriptor_disc_integration PROPERTIES TIMEOUT 90)
    add_test(NAME sdk_core_elf_and_lifted_dispatch COMMAND d2_core_probe_cli)
    set_tests_properties(sdk_core_elf_and_lifted_dispatch PROPERTIES TIMEOUT 45)
endif()
