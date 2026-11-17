#!/usr/bin/env bash
# CI probe: prepare the project-patched SDK for ARM64 and build its runtime with the NDK (no game code involved).
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
ndk="${ANDROID_SDK_ROOT:-$ANDROID_HOME}/ndk/28.2.13676358"
work="$root/_probe"; rm -rf "$work"; mkdir -p "$work"
lock="$root/patches/SDK-WINDOWS.lock"
repo=$(python3 -c "import json;print(json.load(open('$lock'))['repository'])")
base=$(python3 -c "import json;print(json.load(open('$lock'))['base'])")
patch=$(python3 -c "import json;print(json.load(open('$lock'))['patch'])")
git clone -q "$repo" "$work/ps3recomp" && git -C "$work/ps3recomp" checkout -q --detach "$base"
git -C "$work/ps3recomp" apply --whitespace=nowarn "$root/patches/$patch" 2> "$root/patch.log" || echo "patch apply failed" >> "$root/patch.log"
export PATH="$(ls -d ${ANDROID_SDK_ROOT:-$ANDROID_HOME}/cmake/*/bin | head -1):$PATH"
python3 "$root/tools/build_android_core.py" --sdk "$work/ps3recomp" --work "$work/w" --ndk "$ndk" --runtime-only > "$root/probe.log" 2>&1
echo "exit $?" >> "$root/probe.log"
grep -E "error:|CMake Error|^exit|Runtime ARM64" "$root/probe.log" | head -40
# Second probe: glslang plus the Android Vulkan backend sources (the shader compiler and renderer for the game core).
python3 "$root/tools/build_android_core.py" --sdk "$work/ps3recomp" --work "$work/w" --ndk "$ndk" --vulkan-probe > "$root/vulkan-probe.log" 2>&1
rc=$?
echo "exit $rc" >> "$root/vulkan-probe.log"
grep -E "error:|CMake Error|^exit|Backend Vulkan" "$root/vulkan-probe.log" | head -40
exit $rc
