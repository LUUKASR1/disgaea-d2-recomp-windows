#!/usr/bin/env bash
# Milestone 1 probe: can the ps3recomp runtime (independent of any game code) be built for ARM64 Android?
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
ndk="${ANDROID_SDK_ROOT:-$ANDROID_HOME}/ndk/28.2.13676358"
work="$root/_probe"; rm -rf "$work"; mkdir -p "$work"
lock="$root/patches/SDK-WINDOWS.lock"
repo=$(python3 -c "import json;print(json.load(open('$lock'))['repository'])")
base=$(python3 -c "import json;print(json.load(open('$lock'))['base'])")
git clone -q "$repo" "$work/ps3recomp" && git -C "$work/ps3recomp" checkout -q --detach "$base"
# x86-only flag: keep it for x86-64 hosts, drop it for ARM64.
sed -i 's/^\( *\)-msse4\.1\r\?$/\1$<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},x86_64>:-msse4.1>/' "$work/ps3recomp/CMakeLists.txt\"
cmake_bin="$(ls -d ${ANDROID_SDK_ROOT:-$ANDROID_HOME}/cmake/*/bin | head -1)"
export PATH="$cmake_bin:$PATH"
# The SDK needs SDL2 for pad/audio off Windows: build SDL2 for ARM64 first.
git clone -q --depth 1 --branch release-2.30.9 https://github.com/libsdl-org/SDL.git "$work/SDL"
{
  cmake -S "$work/SDL" -B "$work/sdl-build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$work/sdl-install"
  cmake --build "$work/sdl-build" && cmake --install "$work/sdl-build"
  echo "== sdl exit $? =="
  cmake -S "$work/ps3recomp" -B "$work/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF -DCMAKE_PREFIX_PATH="$work/sdl-install" -DSDL2_DIR="$work/sdl-install/lib/cmake/SDL2"
  echo "== configure exit $? =="
  cmake --build "$work/build" --target ps3recomp_runtime -- -k 0
  echo "== build exit $? =="
} > "$root/probe.log" 2>&1
code=$?
grep -E "error:|FAILED|exit |CMake Error" -A4 "$root/probe.log" | head -90
echo "---- summary: $(grep -c 'error:' "$root/probe.log") compiler errors; $(grep -c '^FAILED' "$root/probe.log") failed objects"
tail -3 "$root/probe.log"
exit 0
