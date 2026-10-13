# Third-party notices

This project builds on, patches or links the following components. Each keeps
its own license; nothing here relicenses them.

| Component | Used for | License |
|---|---|---|
| [ps3recomp](https://github.com/sp00nznet/ps3recomp) (sp00nznet / Ned Heller) | Recompiler SDK and runtime. `patches/ps3recomp-d2-macos.diff` contains our modifications to it; `tools/bootstrap_sdk.sh` fetches upstream and applies them. | MIT, Copyright (c) 2026 Ned Heller / sp00nznet |
| at3_standalone (in ps3recomp `third_party/`, derived from PPSSPP / FFmpeg) | ATRAC3/ATRAC3plus audio decoding | LGPL-2.1 (see its `COPYING.LGPLv2.1`) |
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

The helper's Microsoft Visual C++ runtime DLLs are downloaded from the pinned
Microsoft.VC.14.52.CRT.Redist.X64.base package on Microsoft's official
Visual Studio distribution server. Only the release CRT directory is used;
no development tools or debug DLLs are installed. Package and individual DLL
SHA-256 checks are mandatory. The DLLs stay beside the local preparation helper.

Microsoft deployment guidance: https://learn.microsoft.com/en-us/cpp/windows/deployment-in-visual-cpp

The bundled Vulkan loader is built from KhronosGroup/Vulkan-Loader v1.4.350
using its official sources, with a static C runtime. Its LICENSE.txt is retained
in vendor/vulkan. It is copied only beside the local preparation helper; no
Vulkan SDK or graphics driver is installed.

https://github.com/KhronosGroup/Vulkan-Loader
