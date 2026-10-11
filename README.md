# CTR Native

A native port of Crash Team Racing (PS1, 1999), based on the
[CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) decompilation project.
The game and platform code build as C17 with SDL3. Windows and Linux use the
32-bit game path; macOS Apple Silicon has an experimental complete ARM64 executable.

Retail game assets are not included. Supply your own NTSC-U disc image to run
the game or enable the retail-asset tests.

## Current Status

The ARM64 port now builds and links the complete game with bundled SDL3, both
normally and with AddressSanitizer/UndefinedBehaviorSanitizer (ASan/UBSan).
Final startup probes ran both executables for 60 seconds and recorded over
1,300 frames each, including the transition from intro level 41 to main-menu
level 39, without crashes or sanitizer diagnostics.

With the supplied retail disc, all **34 CTest tests** pass in each complete-game
preset. The independent module presets contain **33 tests**. One retail test is
registered only when `assets/ctr-u.bin` is present at configuration time.

This verifies the tested startup path and module contracts. Menu interaction,
visual parity, racing, other game modes, and whole-game checkpoint/replay behavior
still require validation. The ARM64 game remains an explicit experimental opt-in;
default complete-game builds retain the 32-bit pointer guard.

See [ARM64 port progress](docs/ARM64_PORT.md) for implementation details and
validation limits, or [the Italian progress report](docs/ARM64_PROGRESS_IT.md).

## Build Requirements

All default test-enabled builds require **CMake 3.20 or newer** and **Python 3**.
SDL3 is compiled from vendored source and linked statically; no separate SDL
installation is needed. The executable still uses operating-system graphics,
audio, and windowing services.

### macOS / Apple Silicon

Use Xcode Command Line Tools, Apple Clang, CMake, Ninja, and Python 3. The macOS
presets select `/usr/bin/clang`, C17, and the `arm64` architecture.
Run these commands from the repository root:

```sh
cmake --preset macos-arm64-game
cmake --build --preset macos-arm64-game
ctest --preset macos-arm64-game
./build-macos-arm64-game/ctr_native --version
./build-macos-arm64-game/ctr_native
```

Place the retail disc in `assets/ctr-u.bin` before running the final command.
The first build also compiles SDL3; subsequent builds reuse its static library.

For the complete executable and tests with ASan/UBSan:

```sh
cmake --preset macos-arm64-game-sanitized
cmake --build --preset macos-arm64-game-sanitized
ctest --preset macos-arm64-game-sanitized
./build-macos-arm64-game-sanitized/ctr_native
```

The game presets enable `CTR_BUILD_GAME`, `CTR_ENABLE_EXPERIMENTAL_HOST64_GAME`,
and `CTR_NATIVE_DECODED_TERRAIN`. The host64 opt-in requires eight-byte pointers
and the decoded-terrain path.

To test the production modules independently of SDL and the complete executable:

```sh
cmake --preset macos-arm64-memory
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory

cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
```

### Windows / MSVC x86

Install Visual Studio 2022 or Build Tools 2022 with the **Desktop development
with C++** workload and a Windows SDK. Put CMake and Python 3 on `PATH`.
Then run:

```bat
build-msvc.bat
```

The script configures, builds Release, and runs CTest. The equivalent preset
commands are:

```bat
cmake --preset windows-msvc-x86
cmake --build --preset windows-msvc-x86-release --parallel
ctest --preset windows-msvc-x86-release
```

Use `windows-msvc-x86-debug` for the build and test presets when working in Debug;
it shares the `windows-msvc-x86` configure preset.

### Windows / MinGW i686

Install [MSYS2](https://www.msys2.org/). In its terminal, install the tools used by
`build.bat`:

```sh
pacman -Syu
pacman -S --needed git mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-make
```

If the update requests a terminal restart, reopen it before installing packages.
Add `C:\msys64\mingw32\bin` and Python 3 to `PATH`, then run from Command Prompt:

```bat
build.bat
```

Alternatively, use `windows-mingw-i686-debug` or `windows-mingw-i686-release`
for configure, build, and test presets. Presets use their own build directories;
`build.bat` uses `build/`.

### Linux / GCC i686

Use GCC with multilib support, CMake, a build tool, and Python 3. SDL needs the
platform development dependencies for X11, OpenGL, ALSA, udev, and D-Bus.
On Debian/Ubuntu, the build script lists:

```sh
sudo apt install gcc-multilib
sudo apt install libx11-dev libxext-dev libgl1-mesa-dev libasound2-dev libudev-dev libdbus-1-dev
```

The game uses `-m32`; required libraries must also be available for the 32-bit
target. Installing only the host's 64-bit development libraries may be insufficient.

```sh
bash build.sh
```

The script builds Release in `build/` and runs CTest. For separate configurations,
use `linux-gcc-i686-debug` or `linux-gcc-i686-release` for configure, build, and
test presets.

### Build Outputs

| Build | Executable |
| --- | --- |
| macOS ARM64 game | `build-macos-arm64-game/ctr_native` |
| macOS ARM64 game with sanitizers | `build-macos-arm64-game-sanitized/ctr_native` |
| MSVC Release script/preset | `build-msvc-x86/Release/ctr_native.exe` |
| MSVC Debug preset | `build-msvc-x86/Debug/ctr_native.exe` |
| MinGW script | `build/ctr_native.exe` |
| MinGW presets | `build-mingw-i686-{debug,release}/ctr_native.exe` |
| Linux script | `build/ctr_native` |
| Linux presets | `build-linux-gcc-i686-{debug,release}/ctr_native` |

List the presets available on your host with `cmake --list-presets`. They can
also be selected as CMake profiles in an IDE. For a clean compilation without
removing the configuration, use `cmake --build --preset PRESET --clean-first`.

## Game Assets and Running

Place a raw NTSC-U CTR disc image at `assets/ctr-u.bin`. For development builds,
the asset directory belongs in the repository root:

```text
ctr-native-mac-silicon/
  assets/
    ctr-u.bin
  build-macos-arm64-game/
    ctr_native
```

For a standalone distribution, put `assets/` next to the executable:

```text
CTR-Native/
  ctr_native.exe       # Windows; ctr_native on macOS/Linux
  assets/
    ctr-u.bin
```

The loader checks the executable directory and its parent directories for assets,
covering the build paths above. Startup prints the selected base and asset paths.
Run the executable listed for your build. `--version` checks executable loading
without initializing graphics, audio, or game assets.

The disc reader expects a single-track raw PSX BIN image with MODE2/2352 sectors
and the data track starting at byte zero. A cooked 2048-byte ISO does not preserve
the XA/STR sectors used by streamed audio and video.

### Extracted Asset Overrides

Extracted assets are optional for development, modding, and debugging. When
present, they override the corresponding files in `ctr-u.bin`; the disc remains
the fallback for missing extracted files.

```text
assets/
  BIGFILE.BIG
  SOUNDS/
    KART.HWL
  TEST.STR
  XA/
    ENG.XNF
    ENG/
      EXTRA/S00.XA ... S05.XA
      GAME/S00.XA ... S20.XA
    MUSIC/S00.XA ... S01.XA
```

`BIGFILE.BIG` and `SOUNDS/KART.HWL` must be available through extracted files or
the disc. Missing optional XA/XNF files or `TEST.STR` produce startup warnings;
affected streamed audio or Scrapbook video will be unavailable. A present but
invalid XA manifest is a startup error.

### Default Keyboard Mapping

| PS1 control | Keyboard |
| --- | --- |
| D-pad | Arrow keys |
| Cross | `C` |
| Square | `X` |
| Circle | `V` |
| Triangle | `Z` |
| Start | `Enter` |
| Select | `Space` |
| L1 / R1 | Left / right `Shift` |
| L2 / R2 | Left / right `Ctrl` |

SDL gamepad input is also implemented. Control behavior on ARM64 still needs
in-game validation beyond the startup probes.

## Tests and Diagnostics

CTest covers memory/pools, LNG and pointer-map decoding, asset readers,
resident graphs, model animation and vertex decoding, Q12/GTE math, VRAM,
rasterization, scene geometry, terrain/render-bucket workspaces, collision/effect
storage, runtime globals, and checkpoint relocation. Complete-game builds also
register the `--version` smoke test.

The optional retail test reads `assets/ctr-u.bin`, materializes resident graphs,
and checks asset checkpoint rebasing. It includes startup LEV entry 514, whose
empty animated-texture list previously blocked host64 loading. Synthetic tests
also cover channel-pool links, callback width, and cutscene arguments decoded
from four-byte wire values into host-width unions.

Run one group while investigating a regression:

```sh
ctest --preset macos-arm64-game-sanitized -R 'runtime_globals|resident_graph|asset_retail' --output-on-failure
```

Rerun CMake configuration after adding the disc so the optional retail test is
registered. Test counts and startup probes do not establish complete gameplay or
whole-game checkpoint correctness.

The asset validator can inspect real entries without opening the game window:

```sh
./build-macos-arm64-game/ctr_native_asset_validate disc-lev assets 514
```

Internal builds can collect frame timings:

```sh
./build-macos-arm64-game/ctr_native --perf-dir debug/perf
```

Startup failures should be investigated with the sanitized executable. Include
its output, build ID, selected asset paths, and the action that triggered the
failure in a bug report. See [replay documentation](docs/REPLAYS.md) for recording
and playback commands. Internal quick-state keys are `F5` to save and `F8` to load;
full ARM64 checkpoint/replay behavior remains unverified.

## Architecture and Source Layout

```text
main.c                Process startup and platform/game unity includes
CMakeLists.txt        Complete executable and independent test targets
CMakePresets.json     Platform, module-only, and sanitizer configurations
build-msvc.bat        Windows MSVC x86 build/test script
build.bat             Windows MinGW i686 build/test script
build.sh              Linux GCC i686 build/test script
game/                 Decompiled game code and native adaptations
  game_unity.h        Ordered game unity include chain
include/              Game declarations, PS1 facade, and native interfaces
platform/             Native assets, memory, renderer, audio, input, and state
tests/                Production-module, synthetic-asset, and retail tests
tools/                Asset validation, previews, and source audits
docs/                 Port progress, memory model, overlays, and replays
externals/SDL/        Vendored SDL3 source
```

First-party native code targets portable C17 with compiler extensions disabled.
`CTR_NATIVE` selects host-specific behavior; resident declarations use explicit
PS1 and host64 contracts. Serialized PS1 records keep their original field widths
and are decoded into separate resident objects. Host-width pointers are retained
in runtime fields, pools, callbacks, and graph ownership. GPU primitive links
use a native token bridge for the PS1-shaped packet interface.

The platform layer owns host services and PsyQ-compatible facades. The complete
game remains a unity translation unit, while independent targets exercise the
production modules with bounded fixtures and selected real assets.

Additional references:

- [Memory model](docs/MEMORY_MODEL.md)
- [Resident data sections](docs/DATA_SECTIONS.md)
- [Overlay behavior](docs/OVERLAYS.md)
- [ARM64 implementation and validation history](docs/ARM64_PORT.md)
- [Bug replays and state formats](docs/REPLAYS.md)

## Remaining Work

- Validate menu interaction and visual output in the complete ARM64 runtime.
- Make the first race playable and verify input, physics, collision, camera,
  rendering, and audio with real assets.
- Exercise repeated loading/unloading and whole-game checkpoint/replay restoration.
- Audit remaining direct-offset and scratchpad consumers in live game paths.
- Stabilize multiplayer, adventure, cutscenes, saves, and other modes.

The experimental host64 build remains opt-in until these runtime checks are complete.

## Credits and Licensing

- [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK): the decompilation foundation.
- [PsyCross](https://github.com/OpenDriver2/PsyCross): the starting point for portions
  of the native PS1 compatibility layer and facade headers.
- [SDL3](https://github.com/libsdl-org/SDL): cross-platform multimedia services.

See [LICENSE](LICENSE) for the project license and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for third-party licenses and
provenance. Keep the third-party notices with source and binary distributions.
Crash Team Racing and its retail assets belong to their respective rights holders.
