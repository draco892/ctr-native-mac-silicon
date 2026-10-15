# Preparing configurable presentation rates

The native frame pipeline now separates tick-owned preparation from execution of
prepared GPU packets. Simulation follows retail NTSC (two VBlanks per update,
about 29.9 updates/s), while presentation can independently target 1–240 FPS.
By default, repeated presentations display the same prepared pose. The new visual
interpolation path is experimental and disabled by default.

## Ownership and order

In normal native state 3, the scheduler waits for a simulation tick before these
operations. Between ticks it emits VBlanks and presents the cached frame when the
renderer deadline is due. Each update runs:

1. `MainFrame_BeginUpdate`: hub loading hook and retail frame bookkeeping.
2. `MainFrame_ResetDB`: select/reset graphical buffers, OT and GPU link ranges.
3. `MainFrame_GameLogic`: existing simulation, except while loading.
4. `MainFrame_PrepareFrame`: build the frame's GPU packets. This owns the retail
   mixed update/draw routines, including menu input, HUD transitions, weather,
   texture animation, pause capture, fades, memory-card servicing, and race flag.
5. `DrawSync` and `RenderPrepareSubmit`: finish GPU bookkeeping, reset the flip
   countdown and select draw/display environments. The scheduler already owns
   VBlank waits. Synchronous compatibility paths retain `RenderVSYNC` instead.
6. `Platform_CommitPreparedFrame`: cache the prepared OT and present it if due.
   Presentation executes `MainFrame_RenderFrame` / `RenderSubmit` and ends the
   host frame without waiting for the FPS cap.
7. Existing end-of-update work, including adventure hints and replay.

`MainFrame_UpdateLate` owns pickup AI and lap statistics. Preparation calls it at
the original post-geometry position, preserving its loading, pause and geometry
gates. Moving it before HUD/geometry would change the original event ordering.

An extra call to `MainFrame_RenderFrame` does not call preparation, reset buffers,
wait for VBlank, consume input, or advance gameplay/animation counters. Its only
tracker write is the GPU draw-in-progress flag. The actual GPU backend updates its
own rendering state. Prepared packets and their link registrations are valid only
until the next buffer reset, load/unload or checkpoint restore; callers must not
retain them across those boundaries.

`MainFrame_ResetDB` is graphics-only on native: its former loading and tick hooks
belong to `MainFrame_BeginUpdate`. The PSX path retains the original sequence.

The canonical preparation boundary still includes mixed update/draw helpers.
The opt-in scene snapshot path copies unprojected sources independently and
regenerates standard 3D geometry without calling those helpers again. Verified specialized effects also provide copied 3D sources; unsupported
primitive variants still retain their prepared packets.

## Independent scheduler

`native_frame_scheduler.c` accumulates absolute host deadlines using the NTSC
ratio 897619 / 53693175 seconds per VBlank, retaining fractional counter ticks.
Every pair of VBlanks releases one update. `native_frame_loop.c` pumps host events,
emits the production VBlank callback (root counters, pad sampling and audio), and
services presentation deadlines separately. It wakes at least every 5 ms for
host events. `MainFrame_GameLogic` integrates one `ELAPSED_MS` (32 retail units)
step regardless of host elapsed time or presentation rate.

Short delays catch up at most eight due VBlanks, with updates interleaved after
each pair. Longer stalls or clock rollback discard overdue host time and rebase
the deadline, preserving an unfinished VBlank pair. This deliberately slows
simulation through a pathological stall instead of producing an unbounded burst.
Slow GPU submission can still delay both clocks; the scheduler separates timing
ownership on one host thread, rather than running simulation on another thread.

Cached packets are invalidated before every update, on checkpoint restore and
before explicit synchronous VSync calls. State changes, synchronous loading and
scrapbook use the existing retail wait path. Internal recording, armed recording
and playback also use that path to preserve version-2 VSync packet/input ordering.
Their frame presentation is capped but is not repeated between updates. The PSX
path remains unchanged. Scheduler deadlines are host-only and are not serialized.

Profiler `presentations.csv` contains one row per actual swap, associated with its
prepared simulation frame. `frame_times.csv` adds `simulation_elapsed_s` to
measure update cadence without using the latest swap timestamp. Tick CPU scopes
exclude the scheduler wait and cached presentations between updates. Boot/VRAM
presentations precede the first simulation frame and have frame index zero.
Internal `--perf-frames N` stops cleanly after N captured simulation frames.

For example, use a configuration containing `{"video":{"max_fps":120}}`:

```sh
build-macos-arm64-game/ctr_native --config /absolute/path/fps-120.json --perf-dir /absolute/path/perf-120 --perf-frames 450
```

This checks live scheduler timing. It does not certify replay parity or motion
interpolation.

## Experimental visual snapshots and graphical regression

Keep `video.interpolation` set to `false` for the stable renderer, including at
60/75/120 FPS. The configurable presentation ceiling and independent simulation
scheduler continue to work with interpolation disabled. The supplied config and
built-in default both explicitly disable it. Alt/Option+F3 toggles it for the
current session without rewriting the config.

When explicitly enabled above 30 FPS, `NativeSceneGameSnapshot_Begin` copies the
scene before preparation: world camera position/basis, instance pose including
model scale, flags, animation/frame/header and generation IDs. Model command
offsets identify unprojected source triangles. Packed animation vertices and
materials are copied into two host-owned frames; historical asset/game pointers
are never dereferenced. Births use current geometry, deleted objects disappear,
and reused pool addresses cannot inherit the old object's pose. Model/animation
changes and large object jumps do not blend incompatible poses.

Terrain capture traverses the complete reachable BSP, independently of the
canonical camera/PVS list, and copies animated terrain vertices. Allocated but
unreachable nodes are excluded. Model sources use header 0; terrain uses the
highest valid decoded texture LOD. Distance-dependent LOD selection is pending.

`NativeSceneSnapshot_Render` samples camera and object poses using quaternion
rotation with signed scale, including reflected camera bases. Animation corners
blend only across the same primitive and vertex topology. It composes one eye
transform per object, rejects triangles outside the six frustum planes, clips
surviving geometry, projects with subpixel XY and rebuilds depth buckets. Terrain
winding follows camera handedness. Thus visibility, clipping, projection and
ordering for regenerated geometry all use the same sampled camera, including
triangles absent from the previous prepared draw list.

Presentation builds a private host GPU chain: regenerated terrain/model packets
replace their canonical counterparts, while retained commands and UI preserve
ordering. Generated primitive groups are sorted by eye depth within each outer
bucket, preserving their draw-mode/geometry pairing. Attached wheel geometry
shares its kart's ordering bias, instead of reusing a nested retail OT index as
a world depth offset. The original GPU arena remains untouched. Bounded packet/link/vertex
budgets or capture errors select the entire canonical chain; partially generated
frames are never published. Reset epochs invalidate scene history immediately
on loads/checkpoint restore/toggles. Scene/menu/mode changes, camera cuts, respawn
and stalls also reset history. Input, gameplay, audio, events and preparation do
not run on extra presentations. The smoothing adds roughly one simulation tick
of visual latency.

Original packet colors/UVs can update a source material only for one intact
primary triangle whose three captured local corners match the source. Clipped,
split or reflected packet corners cannot overwrite an uncut source's material.
The exact GTE SXY-field bindings remain available for this check; runtime 3D
screen-coordinate interpolation is disabled. Rebuilt packet fields instead
receive presentation-local float XY overrides, cleared at the next presentation.
Retail packet bytes, game structures and checkpoint layouts are unchanged.

Current integration covers standard 3D terrain, kart bodies and ordinary model
instances in one-player gameplay. Menus, cutscenes, loading, adventure arenas and
split-screen use canonical packets. Pixel LOD, billboard, custom-matrix, huge and
split-state instances also retain canonical geometry. Verified wheels, shadows, reflections, particles, weather, confetti and sky now
enter the scene source path described below. Primitives without complete source
provenance keep their entire canonical geometry and can still misalign during
motion. Water/material animation
and texture/light transitions are not fully smoothed. Migrating those effects,
expanding layouts, tuning LOD/performance and whole-game validation remain
unfinished; this is not complete interpolation for the entire game.

`presentations.csv` includes scene object/source/triangle/match/unsupported counts
and regeneration/fallback flags. Unit tests cover copied source lifetime,
ID/signature resets, intermediate visibility, reflected rotation and winding,
handedness cuts, material corner validation, clipping, fresh depth ordering and
presentation-local float-field lifetime, alongside the earlier prototype tests.

### Specialized effects

`native_scene_effect.h` describes copied line/triangle/quad sources without game,
asset or GPU pointers. The adapter converts captured GTE eye coordinates into
world units using the effect's explicit scale (wheel/shadow/particle coordinates
use quarter units), then imports them into the same scene history as the camera
and kart. Wheels copy the kart pose and ordering bias; their local corners and
sprite topology remain distinct across changes of corner order. Shadow patches
sample ground-space geometry. Ordinary particles sample their world centre and
colours; camera-facing particles retain eye-space axes instead of inheriting the
camera's previous orientation. Sky sources use camera rotation without parallax.

Exact projection bindings follow wheel corner permutations, shadow scratch
copies, particle COP2 stores and saved/restored reflection registers. Missing
provenance explicitly invalidates the destination even when its XY happens to
match a previous vertex. There is no screen-coordinate source matching.

Sky and weather candidates are copied before screen bounds rejection; confetti
is staged after its valid four-corner projection and before bounds rejection.
Beaker rain stages separate gradient passes with stable RNG-slot/pass identities.
These candidates can become visible under the sampled camera. Wheel, shadow,
particle and reflected-model sources currently come from emitted preparation
primitives, so their broader spawn/cull/LOD decisions still follow the canonical
tick. Complete source visibility for every special variant remains part of the
remaining case coverage and validation work.

Lines are clipped against all six eye-space planes with endpoint colour
interpolation and emitted as GPU lines, including their own draw mode. Quads are
split using the original corner/UV ordering and clipped as triangles. Blend mode,
page, CLUT and raw-texture semantics are preserved; atlas/frame selection remains
discrete. Unsupported primitives remain whole canonical packets; capture or
capacity failure falls back to the complete canonical frame. No effect generator,
RNG, emitter, sound, gameplay update or event is rerun during presentation.

`presentations.csv` additionally reports `scene_effects` and
`scene_effect_fallback`. Tests cover attached-wheel rotation and inherited depth
bias, subpixel quads, sky parallax, billboard camera changes, clipped gradient
lines, compound draw-mode/line packets, staged invisible geometry, stale binding
invalidation and a real weather-generator capture. The normal and sanitizer
suites retain 45 test executables. Short manual comparisons exercised Crash Cove
and Cortex Castle with motion, steering, jump, pause, level change and the
interpolation toggle; they do not establish whole-game parity or a 120 FPS
performance guarantee. Logs are under ignored `debug/effects`.

### Verification scope after the graphical regression

Normal and AddressSanitizer/UndefinedBehaviorSanitizer builds pass all 45 tests.
The earlier corrected projection-binding prototype was manually exercised in the menus
and Crash Cove, including acceleration, steering, jump/fall/respawn and pause,
with interpolation toggled on/off. That one-track check does not establish
whole-game visual parity; interpolation therefore remains opt-in.

The final stable path was also run sequentially at ceilings 10, 30, 60, 75 and
120, for 150 simulation frames each, with interpolation explicitly disabled.
Every run exits cleanly and captures zero visual vertices. These are boot/loading
smoke measurements, not race benchmarks: observed update rates are approximately
29.70/s and median swap intervals are respectively 101.46, 36.27, 18.90, 16.62
and 10.42 ms. The 120 ceiling does not establish an 8.33 ms frame-time guarantee.
Recorded outputs live under ignored `debug/fps-interpolation/final-caps`.

The source-regeneration path was manually exercised in one-player Crash Cove and
Roo's Tubes with interpolation enabled, acceleration, steering, jump and on/off
comparison, including pause and a level change. Both races captured all eight
kart bodies. Crash Cove copied about 16,000 source triangles; Roo's Tubes copied
about 14,200. Profiling confirmed regenerated frames without canonical fallback
in the sampled race segments. The terrain winding regression found during
implementation was corrected using camera handedness and covered by a test.
These are short visual checks, not all-track parity validation. At a 120 ceiling,
observed race presentation intervals were roughly 16–20 ms on the development
machine; this implementation does not yet guarantee 120 rendered FPS. Outputs
are under ignored `debug/scene-snapshot/final`.

## Gameplay baseline

Internal `--perf` / `--perf-dir` captures now also write `drivers.csv`, observing
the race's canonical player/bot driver table after end-of-frame updates. Columns
cover positions, velocity, speed, turbo reserves, fire speed, turbo count, jump
height/cooldown, lap index/time and action/collision flags. Ghosts and non-race
cutscene objects are excluded. No trace is produced while the level is loading.
The existing frame CSV supplies timers, modes, VBlank counts and presentation
intervals; audio queue/underrun counters remain diagnostic measurements rather
than deterministic comparison fields.

For a before/after comparison, keep a copy of the instrumented pre-refactor
executable, record a race segment with `--record --toggle`, and press F9 after
loading. Exercise acceleration, turning, jumps, turbo, wall contact and pause,
then finalize with F10. Replay the same input file with both executables:

```sh
path/to/reference/ctr_native --replay path/to/input.ctrreplay --perf-dir debug/fps-baseline/before
build-macos-arm64-game/ctr_native --replay path/to/input.ctrreplay --replay-bypass-header --perf-dir debug/fps-baseline/after
python3 tools/compare_gameplay_baseline.py debug/fps-baseline/before debug/fps-baseline/after
```

Use header bypass only for intentional comparisons across build identities; it
does not bypass checkpoint ABI checks. Inspect both logs for replay divergence or
checkpoint errors as well. The comparator requires equal nonempty frame traces
and driver observations, compares simulation values exactly, and reports timing
separately. A truncated/missing capture fails rather than matching an empty run.
Use replay-generated reference traces, not the original recording's profiler
output: profiler capture can continue after F10 and include extra frames.

Snapshots still use the existing file format. Native state regions are now copied
through aligned temporary storage on capture/restore, because four-byte file
alignment is insufficient for ARM64 audio snapshot fields. No payload layout or
format version changes are required.

## Automated checks

- `ctr_native_frame_scheduler`: identical 598 VBlanks and 299 updates over ten
  simulated seconds at caps of 1, 10, 20, 30, 60, 75, 120, 144 and 240 FPS;
  fractional deadlines, short catch-up, long-stall rebasing and clock rollback.
- `ctr_native_frame_loop`: production wait/present code and VBlank callback,
  with substituted host clock/GPU boundaries. At 10/30/60/120 FPS, 150 ticks keep
  identical root counters, input/audio callbacks and fixture simulation state
  while presentation counts differ. Also checks invalidation and replay fallback.
- `ctr_native_frame_present`: submits the same prepared frame 120 times and
  verifies that tracker state (except the draw flag), global game data, pads and
  OT packets remain byte-for-byte unchanged, using the production presentation
  entry points with a substituted GPU boundary.
- `ctr_native_state_alignment`: captures/restores all eight possible byte
  alignments and checks region alignment, round-trip bytes and failure paths.
- `ctr_native_gameplay_baseline`: checks exact comparison, differing frame timing,
  simulation mismatches, missing rows, truncated CSVs and empty race captures.

These checks do not certify all tracks, split screen, complete laps, every turbo
mechanic or audio parity. Actual replay validation results are recorded separately.

## Scheduler validation on 2026-10-14

Both complete-game presets pass **43/43 CTest entries**, including ASan/UBSan.
Four separate live intro runs capture 450 simulation frames each with copied
assets and isolated save storage. Excluding the first 100 frames and boot swaps:

| Configured ceiling | Simulation updates/s | Actual presentations/s | Minimum swap interval |
| --- | --- | --- | --- |
| 10 FPS | 29.909 | 9.779 | 101.147 ms |
| 30 FPS | 29.908 | 28.275 | 34.029 ms |
| 60 FPS | 29.919 | 53.182 | 17.290 ms |
| 120 FPS | 29.910 | 98.858 | 8.838 ms |

Every steady-state update records `elapsed_time_ms=32` and exactly two VBlank
callbacks, including the 10 FPS run. No swap exceeds its configured ceiling.
These are short local intro smoke measurements, not race gameplay equivalence
or guaranteed throughput on other hardware. The production-loop fixture covers
input/audio callback counts; no complete-track or replay parity claim follows.
Captures are retained locally in `debug/fps-scheduler/fps-{10,30,60,120}`.
An additional 450-frame intro run at a 120 FPS ceiling with ASan/UBSan exits
cleanly without sanitizer diagnostics (`debug/fps-scheduler/sanitized-120`).

## Validation on 2026-10-13

Both complete-game presets pass **41/41 CTest entries**, including ASan/UBSan.
The reference executable uses the pre-refactor frame loop from `4f33c368fbec`,
with only the driver observations and aligned snapshot transport added. The local
capture is `debug/fps-baseline/race-replay/input.ctrreplay` (781 frames, finalized),
and the associated profiler capture is in `debug/fps-baseline/recording`.
The captured Crash Cove segment includes acceleration, steering, a jump, contact
and pause. Driver observations include speed up to 15219 in retail units and jump
height up to 4655. The recording's 1,872 loaded-track presentation intervals have
a median of 34.072 ms, about 29.35 FPS. This is a short local reference, not a
complete lap or a performance benchmark; turbo activation and lap completion were
not exercised.

**Exact before/after replay equivalence is not validated.** Playing back the
reference recording with the pre-refactor executable itself crashes immediately
after the bootstrap checkpoint restore, before any driver trace is written. The
sanitized post-refactor executable fails at the same access in
`MainFrame_GameLogic`, reading a driver from the restored player thread list.
The failure is an existing runtime pointer restoration issue, separate from
the fixed snapshot alignment. Local reproductions and logs are retained in
`debug/fps-baseline/reference-playback.log` and `debug/fps-baseline/after.log`.
Do not interpret the passing comparator unit tests as gameplay replay parity.

Direct interactive checks use a temporary application and copied assets/save
storage, leaving the normal save files untouched. The native camera skybox tilt
also uses an explicit MIPS-style wrapping shift after UBSan exposed a signed
negative shift in that path; the PSX expression remains unchanged.

The final pipeline's interactive ASan/UBSan run exercised the intro, menus,
Crash Cove, acceleration, jumping, steering, pause and resume across 4,954 game
frames. Its 2,286 loaded-track presentation intervals have a median of 33.702 ms
(about 29.67 FPS). These timings are separate smoke measurements, not matched
replays, so their difference is not evidence of a performance improvement.
Results are retained in `debug/fps-baseline/smoke-final`.

That run exposed a second existing ARM64 issue on audio resume: retail's
eight-word channel backup truncated the native 40-byte `ChannelStats`, losing
`soundID` and `startFrame` and causing invalid song indices. Native pause/resume
now copies the full structure and preserves the destination channel's list links
and channel ID. `ctr_native_runtime_globals_tests` exercises the production pause
and resume routines with a poisoned free-channel tail and verifies the restored
music ID, start frame, volume, timing and links. The PSX copy remains unchanged.
The interactive run preceded this final audio correction; the final correction
is covered by both normal and sanitized automated suites.
