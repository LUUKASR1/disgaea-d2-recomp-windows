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
cmake_bin="$(ls -d ${ANDROID_SDK_ROOT:-$ANDROID_HOME}/cmake/*/bin | head -1)"
export PATH="$cmake_bin:$PATH"
{
  cmake -S "$work/ps3recomp" -B "$work/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
  echo "== configure exit $? =="
  cmake --build "$work/build" --target ps3recomp_runtime -- -k 0
  echo "== build exit $? =="
} > "$root/probe.log" 2>&1
code=$?
grep -E "error:|FAILED|exit " "$root/probe.log" | head -60
echo "---- summary: $(grep -c 'error:' "$root/probe.log") compiler errors; $(grep -c '^FAILED' "$root/probe.log") failed objects"
tail -3 "$root/probe.log"
exit 0
