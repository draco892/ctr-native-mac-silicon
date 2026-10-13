# CTR Native

A native port of Crash Team Racing (PS1, 1999), based on the
[CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK) decompilation project.
The game and platform code build as C17 with SDL3. Windows and Linux use the
32-bit game path; macOS Apple Silicon has an experimental complete ARM64 executable.

Retail game assets are not included. Supply your own NTSC-U disc image to run
the game or enable the retail-asset tests.

Video settings and keyboard/controller bindings can be changed in
[`config.json`](config.json) without rebuilding. Restart the game after editing
the file; see [External Configuration](#external-configuration) for supported
settings and validation commands.

## Current Status

The ARM64 port now builds and links the complete game with bundled SDL3, both
normally and with AddressSanitizer/UndefinedBehaviorSanitizer (ASan/UBSan).
Final startup probes ran both executables for 60 seconds and recorded over
1,300 frames each, including the transition from intro level 41 to main-menu
level 39, without crashes or sanitizer diagnostics.

With the supplied retail disc, all **37 CTest tests** pass in each complete-game
preset. The independent module presets contain **35 tests**. One retail test is
registered only when `assets/ctr-u.bin` is present at configuration time.

Interactive ARM64 validation on October 11, 2026 exercised Arcade → Single →
1P → Easy with Crash Bandicoot on Crash Cove and Roo’s Tubes, keyboard acceleration, steering,
braking, item use, pause, restart, track changes, and quitting to the main menu. The loading
and race crashes found during these checks were fixed: host64 MEMPACK capacity,
BSP workspace allocation, runtime physics offsets, skidmark offsets, weapon
collision work, and stale camera weather probes after level unload.

Clipped terrain now uses the same GTE projection as adjacent triangles, avoiding
rounding cracks visible on the rocks. Water BSP leaves now use the shared
environment texture, animated vertex UVs, and explicit semi-transparency instead
of the ordinary terrain material. A shoreline check in Crash Cove shows blue,
textured water after this correction. Regression tests cover host arena capacity,
BSP workspace bounds, shared clipped/unclipped vertex projection, and water
material selection, UVs, blending, and invalid environment-map bounds.

Native simulation follows original NTSC timing: roughly **29.9 updates/s**, with
two VBlanks and a fixed 32-unit retail timestep per update. An independent host
scheduler emits VBlank callbacks for input, audio and timers, and runs simulation
even when `video.max_fps` is below 30. The renderer presents prepared GPU packets
at the configured ceiling (1–240, default 30), repeating the latest pose between
updates at higher rates. Motion interpolation remains future work. Loading,
scrapbook and internal recording/replay retain their synchronous retail timing.

The renderer's monotonic clock exposes elapsed time, presentation delta and a
visual animation delta capped at 250 ms after stalls through
`NativeRenderer_GetTime()`. The first presentation has a zero delta; late
presentations restart the deadline without catch-up bursts. Profiling writes
every swap to `presentations.csv` and measures simulation time separately in
`frame_times.csv`. Short intro runs at ceilings of 10, 30, 60 and 120 FPS preserve
about 29.9 updates/s; achieved presentation rates depend on GPU work and host
stalls. See the [scheduler checks](docs/FRAME_PIPELINE.md#independent-scheduler)
for measurements and compatibility limits.
The runtime prints an FPS average every 2,000 rendered frames.

Native frame preparation now runs once per game update; presentation executes its
prepared GPU packets without ticking gameplay. Profiling also records player/bot
simulation observations in `drivers.csv` for exact before/after replay comparisons.
See [frame pipeline and baseline checks](docs/FRAME_PIPELINE.md) for ownership,
validation commands, and the remaining work for interpolated motion.

These checks are smoke tests, not a completed playthrough. All tracks and game
modes, physical controllers, multiplayer, visual parity, and whole-game
checkpoint/replay behavior still require validation. The ARM64 game remains an
explicit experimental opt-in; default complete-game builds retain the 32-bit
pointer guard.

See [ARM64 port progress](docs/ARM64_PORT.md) for implementation details and
validation limits, or [the Italian progress report](docs/ARM64_PROGRESS_IT.md).

## System Requirements

### Running the Game

| Component | Requirement |
| --- | --- |
| macOS CPU | Apple Silicon (`arm64`) for the experimental native game build |
| Windows / Linux CPU | A system capable of running the current 32-bit (`x86`) executable, with matching runtime libraries |
| Graphics | A working OpenGL 3.x core context or newer; the macOS validation machine uses OpenGL 4.1 |
| Game data | Your own NTSC-U retail disc image at `assets/ctr-u.bin`; assets are not included |
| Input | A keyboard, or a controller recognized by SDL3's Gamepad API |
| Audio | An operating-system audio output device supported by SDL3 |
| Storage | Space for the disc image, executable, and writable save files; source builds also need space for build artifacts |

The ARM64 validation environment is **macOS 27.0.1** on a MacBook Pro with an
**Apple M5 Pro**; see Current Status for the scope of gameplay validation.
Minimum CPU performance, RAM, free disk space, and the oldest supported operating
system release have not been established through compatibility testing. Other
Apple Silicon models and Windows/Linux configurations need their own runtime
validation. SDL3 is bundled with the executable; a separate SDL installation is
not required.

### Building from Source

All default test-enabled builds require **CMake 3.20 or newer** and **Python 3**.
SDL3 is compiled from vendored source and linked statically; no separate SDL
installation is needed. The executable still uses operating-system graphics,
audio, and windowing services.

### macOS / Apple Silicon

Use Xcode Command Line Tools, Apple Clang, CMake, Ninja, and Python 3. The macOS
presets select `/usr/bin/clang`, C17, and the `arm64` architecture.

For a clean complete-game build, use the macOS script:

```sh
./build-macos.sh
./build-macos-arm64-game/ctr_native
```

The script requires **CMake 3.24 or newer**. It resets the CMake cache with
`--fresh`, cleans generated targets, rebuilds the game and bundled SDL3, runs
CTest, and verifies the executable version and `config.json`. It stops on the
first failure and preserves assets, configuration files, and saves. It can be
invoked from any working directory using its full path.

For a clean ASan/UBSan build, or to set build parallelism:

```sh
./build-macos.sh --sanitized
./build-macos.sh --jobs 8
```

By default, parallelism uses `CMAKE_BUILD_PARALLEL_LEVEL` when set, otherwise
the host CPU count. Use `./build-macos.sh --help` to list the options.

For incremental builds, run these commands from the repository root:

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

### External Configuration

Edit `config.json` and restart the game to change settings without recompiling.
The default file belongs in the selected base directory, alongside `assets/`:
the repository root for development builds, or beside the executable in a
standalone distribution. The startup log prints the exact file path and settings
used. A missing default file uses built-in defaults; an explicitly requested
missing file, invalid JSON, unknown/duplicate settings, invalid bindings, or
out-of-range values stop startup with an error. Files must be at most 16 KiB.
Omitted settings always use built-in defaults. JSON comments are not supported.

For example, this smaller file changes video settings and the accelerate/confirm
key while retaining the other defaults:

```json
{
  "video": {
    "width": 1280,
    "height": 720,
    "fullscreen": false,
    "max_fps": 30,
    "bilinear_filtering": false
  },
  "input": {
    "keyboard": { "cross": "Y" }
  }
}
```

Launch with another configuration, or validate it without opening the game:

```sh
./build-macos-arm64-game/ctr_native --config ./my-settings.json
./build-macos-arm64-game/ctr_native --config ./my-settings.json --check-config
./build-macos-arm64-game/ctr_native --check-config
```

Explicit relative paths are resolved from the directory where you launch the
command. Checking configuration does not require the retail disc or a window.
The complete default keyboard/controller mappings are provided in `config.json`.
`--check-config` requires the selected file to exist, even when checking the
default path; normal startup can fall back to built-in defaults when the default
file is absent. The loader does not create or overwrite configuration files.

To keep a separate local configuration, copy the supplied file and select it
when launching:

```sh
cp config.json my-settings.json
./build-macos-arm64-game/ctr_native --config ./my-settings.json --check-config
./build-macos-arm64-game/ctr_native --config ./my-settings.json
```

| Setting | Accepted values / behavior |
| --- | --- |
| `video.width`, `video.height` | Window dimensions: width 320–7680, height 240–4320; defaults 800×600 (1280×720 in a widescreen build) |
| `video.fullscreen` | `true` or `false`; default `false`. Fullscreen uses the desktop display mode; width/height set the window size when returning to windowed mode |
| `video.max_fps` | Integer 1–240; default 30. Presentation ceiling, not a guaranteed frame rate |
| `video.bilinear_filtering` | `true` or `false`; default `false` |
| `input.keyboard_slot` | Initial keyboard player slot 0–3; default 0. Hot-plugged controllers may move the keyboard to a free slot |
| `input.axis_deadzone` | SDL axis units 0–32766; default 500 |
| `input.keyboard` | PS1 button names mapped to SDL keyboard scancode names |
| `input.controller` | PS1 button and stick-axis names mapped to SDL gamepad bindings; shared by all controllers |

Keyboard and controller button names are `cross`, `circle`, `square`, `triangle`,
`l1`, `l2`, `l3`, `r1`, `r2`, `r3`, `start`, `select`, `left`, `right`, `up`, and
`down`. Keyboard values include `C`, `Y`, `Return`, `Space`, `Left Shift`,
`Left Ctrl`, and the arrow names. They describe physical keys, not typed
characters; single bindings do not accept key combinations. Use `none` to unbind
a key or controller input. Existing fullscreen/debug shortcuts remain active;
avoid assigning them or Alt/Option to game buttons.

SDL gamepad button values include `a` (south), `b` (east), `x` (west), `y` (north),
`leftshoulder`, `rightshoulder`, `leftstick`, `rightstick`, `start`, `back`,
`dpup`, `dpdown`, `dpleft`, and `dpright`. These are physical positions across
controller families. Axis values use `axis:leftx`, `axis:lefty`, `axis:rightx`,
`axis:righty`, `axis:lefttrigger`, or `axis:righttrigger`; a minus sign after the
colon inverts an axis, e.g. `axis:-leftx`. Controller stick settings
`axis_left_x`, `axis_left_y`, `axis_right_x`, and `axis_right_y` require an axis
binding or `none`.

Raising `max_fps` above 30 enables additional presentations of the latest prepared
pose. Simulation and existing animations remain at their original NTSC cadence;
there is no interpolation yet. A lower ceiling reduces presentation rate while
simulation continues independently. Resolution scales the output; it does not replace the game's
internal render resolution or change the compiled 4:3/widescreen game layout.
F11/fullscreen and F3/filter changes apply to the current session and do not
rewrite the JSON file.

Interactive configuration checks on the validation Mac exercised a 1024×768
window, fullscreen startup, keyboard navigation with `S`, confirmation with `Y`,
and Start with `P`. With a configured 20 FPS cap, measured presentation intervals
were never shorter than 50 ms; median throughput was about 19.9 FPS. These checks
ran with ASan/UBSan without diagnostics. Physical controller remapping still
requires hardware validation.

### Retail Assets

Place a raw NTSC-U CTR disc image at `assets/ctr-u.bin`. For development builds,
the asset directory belongs in the repository root:

```text
ctr-native-mac-silicon/
  config.json
  assets/
    ctr-u.bin
  build-macos-arm64-game/
    ctr_native
```

For a standalone distribution, put `assets/` next to the executable:

```text
CTR-Native/
  ctr_native.exe       # Windows; ctr_native on macOS/Linux
  config.json
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

Click the game window to give it keyboard focus. In menus, use the arrow keys
to select a row, **`C` to confirm**, and **`V` to go back**. Enter is the PS1
Start button, rather than the menu confirmation button.

| PS1 control | Keyboard | Default use |
| --- | --- | --- |
| D-pad | Arrow keys | Menu navigation / steering |
| Cross | `C` | Confirm / accelerate |
| Square | `X` | Brake |
| Circle | `V` | Back / use item |
| Triangle | `Z` | Triangle action |
| Start | `Enter` | Start / pause |
| Select | `Space` | Select action |
| L1 / R1 | Left / right `Shift` | Jump / power slide |
| L2 / R2 | Left / right `Ctrl` | L2 / R2 actions |
| L3 / R3 | `[` / `]` | Analog stick clicks |

### Controllers

The native input layer accepts devices recognized by **SDL3's Gamepad API**.
This includes Xbox-style, PlayStation DualShock/DualSense, and Nintendo
Switch-style controllers when the operating system and SDL mapping recognize
the device. USB and Bluetooth availability depends on the device and host.
These families are supported through SDL; physical hardware compatibility has
not been certified for every model on this ARM64 build.

| PS1 control | SDL gamepad position | Xbox-style label |
| --- | --- | --- |
| Cross | South face button | A |
| Circle | East face button | B |
| Square | West face button | X |
| Triangle | North face button | Y |
| D-pad | D-pad | D-pad |
| L1 / R1 | Left / right shoulder | LB / RB |
| L2 / R2 | Left / right trigger | LT / RT |
| L3 / R3 | Left / right stick click | LS / RS |
| Start | Start | Menu |
| Select | Back | View |

Both analog sticks, hot-plugging, and rumble are implemented. Up to four
controller slots are available; the pad bus uses the game's multitap interface
for more than two players. Multiplayer gameplay still requires validation.
The keyboard uses a free slot and moves away from a slot occupied by a gamepad.

For devices that SDL does not recognize automatically, the input layer loads
an optional `gamecontrollerdb.txt` from the selected base directory. Unmapped
generic joysticks are not opened as gamepads. Menu labels use the game's PS1
button names; Nintendo labels may differ from the physical-position mapping above.

### Window Size and Fullscreen

Drag the window borders to resize it. Press **F11** or **Alt + Enter** to toggle
fullscreen; on macOS, Alt is the Option key. Depending on macOS keyboard
settings, F11 may require the Fn key. Release Option/Alt before using game
controls, since holding it suppresses keyboard gamepad input.

The default window is 800×600 (4:3). Change `video.width`, `video.height`, and
`video.fullscreen` in `config.json`, then restart; no rebuild is needed.
Window sizing scales presentation; it does not change the game-controlled
internal render-target resolution.

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

Configuration and presentation timing have their own tests. They check partial
configuration defaults, malformed/truncated JSON, duplicate and unknown settings,
numeric bounds, SDL binding names, relative file paths, FPS caps, and recovery
after host stalls:

```sh
ctest --preset macos-arm64-game -R 'config|render_timing' --output-on-failure
ctest --preset macos-arm64-game-sanitized -R 'config|render_timing' --output-on-failure
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
config.json           External video settings and keyboard/controller bindings
build-msvc.bat        Windows MSVC x86 build/test script
build.bat             Windows MinGW i686 build/test script
build.sh              Linux GCC i686 build/test script
build-macos.sh        macOS Apple Silicon clean build/test script
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
