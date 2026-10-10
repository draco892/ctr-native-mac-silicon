# Replays

Use this for bug reports in internal builds.

Current checkpoints use payload version 9 and replay headers use version 2.
Version 9 accounts for the expanded RenderBucket/terrain host workspaces and
asset-owner snapshot version 2, which embeds resident graph checkpoints.
Version 8 accounts for the new resident Camera/PushBuffer/Driver/Instance layouts,
full-width instance peer links and the shadow OT pointer. Version 7 added
Torch/skid/shadow workspaces to the collision/camera storage from version 6.
Previous versions are rejected. Pointer width, endian marker and platform/CPU
identity are checked; bypassing build identity does not bypass format/ABI checks.
The outer CTST container remains version 1. Checkpoints now include immutable
asset owners and host workspaces; the fixed asset-owner region adds 6 MiB to each
payload. Full ARM64 gameplay/replay validation remains blocked by resident layout
migration; the portable relocation/container tests run independently on ARM64.
See [ARM64 progress](ARM64_PORT.md) for supported tests and remaining work.

## Quick State

- `F5`: save `debug/states/quick.ctrstates`
- `F8`: load `debug/states/quick.ctrstates`

## Record

```sh
build/ctr_native --record
```

Windows: use `build\ctr_native.exe` instead.

Normal saves live in `memcards/slot0`.

When recording starts, the CTR save files from `memcards/slot0` and `slot1` are copied to `memcard.seed`. The game records with a writable copy named `memcard.recording`, so saves and ghosts made while recording stay in the report.

To choose when recording starts:

```sh
build/ctr_native --record --toggle
```

- Press `F9` to start.
- Press `F10` to stop.

For more detailed reports:

```sh
build/ctr_native --record --detailed
```

You can combine both:

```sh
build/ctr_native --record --toggle --detailed
```

## Play Back

Use the command written in that folder's `metadata.txt`.

It looks like:

```sh
build/ctr_native --replay "debug/reports/20260605/ctr-123456/input.ctrreplay"
```

Playback creates a fresh writable `memcard.playback` from `memcard.seed` every run and does not touch your real saves.

If a developer asks you to bypass header identity checks:

```sh
build/ctr_native --replay "debug/reports/20260605/ctr-123456/input.ctrreplay" --replay-bypass-header
```
