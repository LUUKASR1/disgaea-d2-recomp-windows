# Windows x64 — source-only candidate

This fresh-build workflow is prepared from the user's collected Windows
sources. A full clean Windows build has **not** been validated here.
Keep your working installation separate while testing this candidate.

## Required tools and inputs

- Visual Studio Build Tools/Community with Desktop development with C++,
  Windows SDK, LLVM clang-cl, CMake and Ninja. The tested environment used
  Clang 22.1.3; vanilla MSVC is not the compiler for this SPU build.
- Git and Python 3.11 or newer.
- nlohmann/json single header. Supply a directory containing
  `nlohmann/json.hpp`; the existing build used v3.11.3.
- Your own decrypted US 1.40 EBOOT.elf. Required SHA-256:
  `0ec183e580a270b0bb1b3a554794a9e4ec2d57a2be251ebdcf81fe21b78d225c`.
- Your own game data and update/DLC data, kept separately from the repository.
  Follow the original project's dumping/content instructions in
  [UPSTREAM-README.md](UPSTREAM-README.md). Nothing here downloads game content.

## Fresh compilation

Clone this repository or extract its source archive into a new directory.
Open **x64 Native Tools Command Prompt for VS**, enter the repository root:

```bat
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r tools\requirements-lifts.txt
.venv\Scripts\python.exe tools\bootstrap_windows.py
.venv\Scripts\python.exe tools\build_windows.py --elf "D:\OwnedFiles\EBOOT.elf" --json-include "D:\Dependencies\include"
```

`bootstrap_windows.py` fetches the SDK at the revision in
`patches/SDK-WINDOWS.lock`, checks the patch hash, applies it and checks the
resulting Git tree. It refuses to overwrite an existing SDK directory.
`build_windows.py` also checks the full SDK source tree with a temporary Git
index, preserving the real index, before generation/build.

The build script copies your ELF into ignored `work/v140/EBOOT.elf`, generates
PPU/SPU sources locally and creates a Release build in
`port/build-windows-140`. It explicitly enables SSE4.1 in lifted SPU sources.
Fresh builds refuse to overwrite existing build/generated directories.
To rebuild unchanged generated output after source edits:

```bat
cmake --build port\build-windows-140 --parallel 4
```

The original macOS `SDK.lock` remains unchanged. Windows uses its separate
lock and consolidated patch. Code generation currently uses the original
manifest's `--allow-sdk-worktree` mode **after** the Windows source tree is
verified by the new build script.

## Run

Copy `windows/config.exemplo.json` to `windows/config.local.json`. Set:

- `game_root`: directory containing `PS3_GAME/USRDIR`.
- `hdd0_root`: your local PS3 HDD data directory. The tested layout contains
  `game/BLUS31313/USRDIR/Data/START_7.dat` for 1.40 and optional NPUB31321 DLC.

`workspace` remains in the generic example for compatibility; this public
launcher locates the repository relative to its own script and runs from the
repository root. Then run:

```bat
.venv\Scripts\python.exe windows\iniciar_build.py
```

Normal mode disables detailed diagnostics, keeps the confirmed texture/font,
palette, depth/stencil and graphics idle settings, and enables particle batch
and constant caches. VP direct, ready-wake and vertex-context experiments are
disabled. The launcher does not switch the compiled SIMD option.
Saves remain under the configured local HDD data path.

## Before a public release

Alt+F4 process exit and save/reopen/load were confirmed in the user’s existing Windows build.
Validate a fresh Windows build, rendering/text, audio, castle hallway,
three Mothman pollen attacks, save/close/reopen/load and Alt+F4 process exit.
The close fix waits for the admitted savedata operation and then terminates
the Windows host process; it does not yet cooperatively join every guest
worker. A save operation that never completes keeps the process waiting.
Host frame captures are explicitly flushed; arbitrary CRT/DLL exit handlers
are not invoked by this close path.

Never attach the compiled game executable, generated PPU/SPU code, ELF,
game assets, update/DLC packages or saves to commits/releases/issues.

## Source provenance

Port and SDK changes came from the verified `fontes-para-revisao.zip` collected
on 2026-10-12, with SIMD ON. Source templates ending in `.in` were not included
by the first collector; this candidate retains those templates from the exact
original project commit. Their unchanged status on the Windows installation
has not been independently confirmed. The clean-build test must check them.
