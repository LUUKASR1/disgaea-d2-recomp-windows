# Steam Deck & Proton Optimization Guide

This port builds a native x86-64 Windows (`DisgaeaD2Recomp.exe`) binary that runs smoothly on SteamOS via **Proton Experimental** with native `16:10` (`1280×800`) **Vert+** presentation, low CPU usage, and a persistent DXBC/PSO disk shader cache.

---

## 1. Steam Deck OLED Measured Performance (Main Castle Hub)

Measured on Steam Deck OLED (`1280×800`, Main Castle Hub, ~`1,000` draws/frame, warm shader cache):

| Build / Stage | Warm `60 FPS` Total CPU | PPU Thread | RSX Thread | Package Power | Uncapped (`D2_GCM_UNLIMITED=1`) |
| --- | --- | --- | --- | --- | --- |
| Baseline (Pass 2: early batch + palette/surface caches) | `202.7%` (`152.0% USR` / `50.7% SYS`) | `74.9%` | `67.5%` | `7.52–9.87 W` | — |
| Pass 3 (`vertplus`, D3D12 upload & spin backoff) | `190.5%` (`152.0% USR` / `38.5% SYS`) | `64.6%` | `58.2%` | `6.30–7.13 W` | — |
| Pass 4 (HLE dispatch + cold-split `vm_read/write32`) | `191.7%` (`147.8% USR` / `43.9% SYS`) | `65.6%` | `58.7%` | `6.73–7.77 W` | `88.92 FPS` (`11.25 ms/fr`, `9.59–11.43 W`) |
| Pass 5 (`d2_vm_fast` + ThinLTO + AVX2 vertex/cache) | **`163.3%`** (`137.2% USR` / `26.2% SYS`) | **`44.7%`** | **`57.7%`** | **`4.43–6.72 W`** | **`103.82 FPS`** (`9.63 ms/fr`, `9.04–10.61 W`) |

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
