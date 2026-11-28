# Steam Deck & Proton Optimization Guide

This port builds a native x86-64 Windows (`DisgaeaD2Recomp.exe`) binary that runs smoothly on SteamOS via **Proton Experimental** with native `16:10` (`1280×800`) **Vert+** presentation, low CPU usage, and a persistent DXBC/PSO disk shader cache.

---

## 1. Steam Deck OLED Measured Performance (Main Castle Hub)

Measured live on Steam Deck OLED (Main Castle Hub, ~`942–1,021` draws/frame):

| Build / Stage | Warm `60 Hz` Rate | Total CPU (`Usr / Sys`) | PPU Thread | RSX Thread | Audio SPU (`_synth2`) | Package Power | Uncapped (`D2_GCM_UNLIMITED=1`) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **Stock Baseline** (`v1.0.53`, `640×400` FSR1, ~`1,009` draws/fr) | `32.21 FPS` (`31.04 ms`) | `~352%` *(`gcm_put_watch` `99.5%`)* | `85.4%` (`60% / 25%`) | `69.9%` | `97.1%` | `~10–12 W` *(pinned @ `2.87 GHz`)* | — *(CPU-bound below `60 Hz`)* |
| **Pass 1** (`1280×800` native Vert+, `d2_vm_fast`, SPU AVX2/FMA3, vertex context, `gcm_put_watch` wait, Vert+ WC shadow) | `59.30 FPS` (`16.86 ms`) | `270.6%` (`200.6% / 69.9%`) | `82.0%` (`47.4% / 34.6%`) | `79.8%` | `36.6%` | `10.0–11.6 W` | — |
| **Pass 2** (SPU LS `vpshufb`, O(1) RSX caches, `d2_copy`/`d2_crt`, SPU coh bloom/range, ThinLTO) | `59.32 FPS` (`16.86 ms`) | `202.7%` (`152.0% / 50.7%`) | `74.9%` (`47.0% / 27.9%`) | `67.5%` | `18.3%` | `7.52–9.87 W` | `74.15 FPS` (`13.49 ms`, `9.2–14.4 W`) |
| **Pass 3** (AVX2 constant-pair hashing, vertex decode inlining, `d2_bswap32_copy`) | `59.22 FPS` (`16.89 ms`) | `190.5%` (`152.0% / 38.5%`) | `64.6%` (`50.8% / 13.8%`) | `58.2%` | `21.5%` | `6.30–7.13 W` | `88.95 FPS` (`11.24 ms`, `9.9–11.4 W`) |
| **Pass 4** (O(1) HLE dispatch, cold-split `vm_read/write32`, lazy RA profiler, scalar `rsx_process_method`) | `59.20 FPS` (`16.89 ms`) | `191.7%` (`147.8% / 43.9%`) | `65.6%` (`46.6% / 19.0%`) | `58.7%` | `22.0%` | `6.73–7.77 W` | `88.92 FPS` (`11.25 ms`, `9.59–11.43 W`) |
| **Pass 5** (`NtDelayExecution` backoff/usleep, Zen 2 16B STLF fix, folded RSX debug) | **`59.22 FPS`** (`16.89 ms`) | **`163.3%`** (`137.2% / 26.2%`) | **`44.7%`** (`41.0% / 3.7%`) | **`57.7%`** | **`22.0%`** | **`4.43–6.72 W`** | **`103.82 FPS`** (`9.63 ms`, `9.04–10.61 W`) |

End-to-end gains from Stock Baseline (`v1.0.53`) to Pass 5:
- **Framerate & Resolution:** `32.21 FPS` (`31.04 ms/fr`) at `640×400` FSR1 → locked **`59.22 FPS`** (`16.89 ms/fr`) / **`103.82 FPS`** uncapped at native **`1280×800` Vert+**.
- **CPU & Thread Efficiency:** Total process CPU cut from `~352%` → **`163.3%`** (`gcm_put_watch` busy-spin `99.5%` → `< 1%`, SPU `_synth2` `97.1%` → `18.3–22.0%`, PPU thread `85.4%` → `44.7%`).
- **Power Draw:** APU package power reduced from `~10–12 W` (`2.87 GHz`-pinned) to **`4.43–6.72 W`** at `60 Hz`.

---

## 2. Building for Steam Deck (`x86-64-v3` + ThinLTO)

Standard builds keep `-DD2_TARGET_X86_64_V3=OFF` and `-DD2_ENABLE_THINLTO=OFF` so binaries remain portable across older x86-64 CPUs.

Steam Deck (Zen 2 / Van Gogh) supports AVX2, FMA, BMI1/2, MOVBE, and SSE4.2 (`x86-64-v3`). To enable the Steam Deck / modern-x64 optimization profile when configuring CMake (works with LLVM MinGW Clang or `clang-cl`):

```sh
cmake -B port/build-deck \
  -DD2_TARGET_X86_64_V3=ON \
  -DD2_ENABLE_THINLTO=ON
```

What these opt-in flags enable:

- `D2_TARGET_X86_64_V3=ON`: `-march=x86-64-v3` (enables AVX2 vertex/index conversion fast paths in `rsx_d3d12_backend`, SSE4.1 SPU SIMD codegen, and zero-`vzeroupper` scalar fast paths) and defines `D2_SPU_SIMD_SSE41=1` for generated SPU sources.
- `D2_ENABLE_THINLTO=ON`: `-flto=thin` across the C/C++ runtime, native guest overrides (`d2_copy`, `d2_crt`), and lifted PPU/SPU translation units.

---

## 3. Portable Folder Layout & `DisgaeaD2.ini`

Stage a self-contained game folder on the Steam Deck (e.g. `~/Games/DisgaeaD2/`) with relative paths inside `DisgaeaD2.ini` so no Windows drive letters or host-specific paths are needed:

```text
DisgaeaD2/
├── DisgaeaD2Recomp.exe
├── DisgaeaD2.ini
├── EBOOT.elf
├── game/
│   └── PS3_GAME/
│       └── USRDIR/
├── hdd0/
│   └── game/
│       └── BLUS31313/         # optional 1.40 update / DLC files
├── hdd1/
├── settings/                  # created automatically (holds settings.ini)
├── shader_cache/              # created automatically on first run
└── logs/                      # created automatically on first run
```

Sample portable `DisgaeaD2.ini` (parsed as flat `key=value` lines):

```ini
eboot=EBOOT.elf
cwd=.
env.PS3_VFS_ROOT=game
env.PS3_HDD0_ROOT=hdd0
env.PS3_HDD1_ROOT=hdd1
env.D2_SETTINGS_DIR=settings
env.D2_LOGS_DIR=logs
env.D2_SHADER_CACHE_DIR=shader_cache
env.D2_PAUSE_UNFOCUSED=0
env.D2_VERTPLUS=1
```

Relative values for `eboot`, `cwd`, `PS3_VFS_ROOT`, `PS3_HDD0_ROOT`, `PS3_HDD1_ROOT`, `D2_SETTINGS_DIR`, `D2_SHADER_CACHE_DIR`, and `D2_LOGS_DIR` resolve against the directory containing `DisgaeaD2.ini` / `DisgaeaD2Recomp.exe`.

> [!NOTE]
> Display and runtime overlay preferences live in `settings/settings.ini` (`width=1280`, `height=800`, `pause_focus=0`, `16:10` Vert+, `render_scale=125`). Press **F1** (or map a Steam Deck back button / **View + Menu** chord to `F1`) inside the game to open the live graphics & control overlay.

---

## 4. Running Under SteamOS & Proton

1. Add `DisgaeaD2Recomp.exe` in Steam Desktop Mode via **Add a Non-Steam Game to My Library...** (set **Start In** to the staged folder containing `DisgaeaD2.ini`).
2. Open **Properties → Compatibility**, check **Force the use of a specific Steam Play compatibility tool**, and select **Proton Experimental**.
3. Use a standard **Gamepad with Joystick Trackpad** Steam Input template (`XInput` maps directly to PS3 controller Port 0).
4. *(Optional)* Generate custom Steam Grid artwork (wide capsule, vertical box cover, hero banner, clear logo, and icon) from your own box art / key art scans with [`tools/make_steam_grid.py`](../tools/make_steam_grid.py):

```sh
python3 tools/make_steam_grid.py \
  --box path/to/box_art.jpg \
  --keyart path/to/keyart.jpg \
  --logo path/to/clear_logo.png \
  --out-dir ~/.local/share/Steam/userdata/<UID>/config/grid \
  --create-64bit-links
```

---

## 5. Useful Runtime Environment Variables

These can be set as `env.<VAR>=<VALUE>` lines in `DisgaeaD2.ini` or in Steam **Launch Options** (`VAR=value %command%`):

- `D2_VERTPLUS=1` / `0`: Enable or disable native `16:10` Vert+ vertical FOV expansion (auto-enabled on `16:10` displays such as `1280×800` when aspect is `auto`).
- `D2_PAUSE_UNFOCUSED=0`: Prevent accidental pausing when SteamOS Quick Access Menu or overlay briefly takes focus.
- `D2_GCM_UNLIMITED=1`: Uncap RSX/flip pacing for benchmarking (default `0` paces cleanly to 60 Hz).
- `D2_SHADER_CACHE_DIR=shader_cache`: Override the persistent DXBC/PSO cache folder.
- `PPU_VM_SLOW=1`: Force the slow PPU memory fallback path for debugging.
