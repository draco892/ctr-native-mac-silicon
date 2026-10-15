# Race interpolation graphics validation — 2026-10-15

This targeted pass covers kart height during acceleration, race backgrounds and
retained canonical geometry. It is not a whole-game visual certification.

## Changes and deterministic checks

- Camera aspect correction is decomposed by row; model scale remains by column.
  A 121-sample pitch fixture preserves the 9/16 aspect and exact pose endpoints.
- A ramp/follow-camera fixture checks body contact and independently captured
  ground-patch height against the expected projection within 0.6 pixels at 121
  samples. Animation-change tests keep body/wheel poses continuous while selecting
  the current animation's local geometry, and reset on a different model asset.
- The production sky fixture copies eight sectors, covers all four emitted
  packets and verifies byte-identical canonical packets with capture on/off.
- A 0.25-pixel triangle survives presentation even when its integer corners are
  collinear; the opposite winding is rejected. The canonical integer API keeps
  its original rejection policy.
- Coherent-current-pose tests compare geometry, camera and material output at
  121 requested alphas against the same alpha=1 reference. Existing generation,
  clipping, wheel corner-order, weather and packet-lifetime tests also pass.

Both complete-game builds pass **45/45 CTest executables**, including ASan/UBSan.
The sanitizer suite covers unit/integration fixtures; this pass did not run a
sanitized interactive race or establish matched gameplay/audio replay parity.

## Interactive scope

One-player 800×600, 60 FPS ceiling, interpolation initially enabled, isolated save
storage and copied configuration. The graphics verification build was exercised in Crash
Cove, Roo’s Tubes and Cortex Castle, including acceleration, steering, pause,
level changes and interpolation on/off comparison. Crash Cove also included a
jump; Cortex Castle showed rain. These were short segments, not complete laps.
The comparisons are qualitative, not synchronized pixel-diff recordings.
A final guard for rejected staged candidates was added after the interactive
run; its regression passes both test builds. Failed staging now retains the
canonical packet and requests a coherent pose, rather than marking an absent
source as covered.

The targeted comparisons did not establish an additional reproducible background
failure requiring a new fix. Thin seams visible in the canonical terrain path
remain outside a claim of complete raster/LOD parity. Full-track and long-session
validation is still pending.

## Observed presentation modes

Only presentations where the experimental scene path was requested are counted.
Menus, loading and intervals explicitly toggled off are excluded. Counts include
race introductions and pauses; they are coverage observations, not FPS benchmarks.

| Track | Interpolated regeneration | Coherent current pose | Full canonical fallback |
| --- | ---: | ---: | ---: |
| Crash Cove | 2495 | 104 | 19 |
| Roo’s Tubes | 1549 | 200 | 171 |
| Cortex Castle | 1977 | 178 | 722 |

The CSV audit found zero legacy packet-sampled fields and zero cases where a
retained world model/effect was mixed with a delayed sampled pose. It also found
zero re-entry into regeneration during a prepared tick after full fallback.
Those checks concern annotated packets and the explicit sky-glow gate; they do
not prove that every uninstrumented special effect in the game is covered.

## Canonical cases and limits

Unverified world model packets and special effects select current-pose sampling
for the whole regenerated scene. Sky glow does the same because it is still
computed from current pitch/roll. Reason bits are 1=models, 2=effects, 4=glow.
The observed model/effect cases exercised the guard; the glow-specific decision
is covered by code inspection and the shared coherent-pose fixture, not a
separate live glow-track measurement.

Invalid wheel provenance, unsupported wheel anchors, asset/packet validation or
capacity failures retain the complete canonical chain. A presentation-time
failure now latches that choice until the next simulation tick and clears
history. Retained commands receive no legacy XY/RGB/rectangle smoothing.

Current-pose and full-fallback cases can still show about 30 distinct poses per
second. Migrating remaining special geometry, distance-dependent LOD, all layouts,
all tracks and performance work remain open. This pass does not guarantee stable
60 or 120 rendered FPS or make interpolation the default stable renderer.

## Local evidence

Ignored `debug/graphics-audit/verified/` contains `tests.log`, `tests-asan.log`,
`manual.log`, `audit.json`, `frame_times.csv`, `presentations.csv` and `drivers.csv`.
Earlier iterations under `runtime/` and `final/` are separate development runs;
they are not included in the table above. Repository `config.json` is unchanged.
