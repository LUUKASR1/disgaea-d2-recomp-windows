cmake_minimum_required(VERSION 3.22)
project(d2android LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(d2android SHARED vulkan_surface.cpp core_host.cpp)
target_include_directories(d2android PRIVATE ../../../../../port/src)   # d2_cheats.h: the cheat API the core exports
find_library(LOG_LIB log)
target_link_libraries(d2android vulkan android dl ${LOG_LIB})
