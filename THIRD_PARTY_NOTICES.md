# Third-party notices

This project builds on, patches or links the following components. Each keeps
its own license; nothing here relicenses them.

| Component | Used for | License |
|---|---|---|
| [ps3recomp](https://github.com/sp00nznet/ps3recomp) (sp00nznet / Ned Heller) | Recompiler SDK and runtime. `patches/ps3recomp-d2-macos.diff` contains our modifications to it; `tools/bootstrap_sdk.sh` fetches upstream and applies them. | MIT, Copyright (c) 2026 Ned Heller / sp00nznet |
| at3_standalone (in ps3recomp `third_party/`, derived from PPSSPP / FFmpeg) | ATRAC3/ATRAC3plus audio decoding | LGPL-2.1 (see its `COPYING.LGPLv2.1`) |
| libudfread 1.2.0 (VideoLAN/VLC authors) | Read-only UDF image inspection; unchanged vendored sources in `port/vfs/vendor/libudfread`. Android links a separate shared library. Source and build instructions are included in this repository. | LGPL-2.1-or-later; see `port/vfs/vendor/libudfread/COPYING` |
| [SDL2](https://www.libsdl.org/) | Gamepad and audio output | zlib |
| [nlohmann/json](https://github.com/nlohmann/json) | Cheat/editor schema parsing | MIT |
| Apple frameworks (Metal, AppKit, VideoToolbox, CoreAudio) | Rendering, UI, movie decoding, audio | Apple SDK terms |
| Homebrew build dependencies (CMake, Ninja, FFmpeg, glslang, SPIRV-Cross) | Build tooling, installed by the user | Their respective licenses |

The ps3recomp MIT notice, reproduced as its license requires:

```
MIT License

Copyright (c) 2026 Ned Heller / sp00nznet

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Reference material consulted during development (not included): RPCS3 source
and issue tracker, community Cheat Engine / Apollo cheat documentation, and
public GameFAQs save files used only for local import testing.

## Windows port additions

The Windows SDK patch keeps the ps3recomp/at3 licenses above. Windows SDK headers and libraries (Direct3D 12, DXGI, WASAPI, XInput, Winsock, BCrypt) are supplied by the user through the Microsoft development tools, under their terms; they are not shipped here. The nlohmann/json header is also supplied separately by the user.

## Portable preparation tools

The launcher downloads pinned, SHA-256-verified LLVM-MinGW (20260616 UCRT x64)
and MinGit (2.56.0.2 x64) from their official releases. The extracted upstream
license files are retained in the private tools directory. They are not game
assets and are not added to this repository.

- LLVM-MinGW: https://github.com/mstorsjo/llvm-mingw
- Git for Windows: https://github.com/git-for-windows/git
- PS3 firmware is downloaded directly from Sony's official update server,
  verified against a pinned SHA-256, and remains local. It is not bundled.

The preparation helper requires system-installed Microsoft Visual C++ and
Vulkan runtimes. Missing runtimes are downloaded from their official vendor
servers and verified by pinned SHA-256 before the standard installers run.
These installers are not bundled. They may require Windows administrator
permission. No Visual Studio development environment or Vulkan SDK is installed.

- Microsoft Visual C++: https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist
- Vulkan runtime: https://vulkan.lunarg.com/sdk/home

## Independent package/SELF preparation

The standalone preparation modules are original implementations using published
PS3 format layouts and public constants. See docs/NATIVE-PREPARATION.md for
pinned primary references. RPCS3 implementation code and binaries are not
incorporated in these modules or invoked by the standalone preparation path.
AES operations use the already bundled PyCryptodome dependency and its existing
license notices. Owned update/ELF files and generated game code stay local.

## AMD FidelityFX FSR 1

The unchanged AMD FSR 1 headers in port/graphics/vendor/fsr1 are MIT-licensed.
The full license and pinned source/hash provenance are included in that folder.
The native graphics preview embeds their EASU/RCAS shader implementations.
DLSS and frame generation are not included or claimed as active.
