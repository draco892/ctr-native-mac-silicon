# ARM64 port progress

The port remains C17. The first milestone builds and tests production memory
modules natively on macOS ARM64. It does **not** build a playable game yet.

## Build and test the memory milestone

With Xcode Command Line Tools, CMake 3.20+, Ninja and Python 3 available:

```sh
cmake --preset macos-arm64-memory
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory
```

The executable is `build-macos-arm64-memory/ctr_native_memory_tests`. SDL and
retail assets are not needed for these tests.

To run the same modules with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
```

For other hosts, the equivalent configuration is:

```sh
cmake -S . -B build-memory -DCTR_BUILD_GAME=OFF -DBUILD_TESTING=ON
cmake --build build-memory
ctest --test-dir build-memory --output-on-failure
```

## Implemented contracts

- Base types and endian helpers can be used by independent 32-bit and 64-bit
  native modules. `u32` remains four bytes.
- `Mempack` is runtime allocator state, with explicit layout assertions for
  both pointer widths. Its state binding is provided by the platform boundary;
  the game continues to use the resident `sdata` pools.
- Native MEMPACK initialization preserves full host addresses. Free space is
  calculated using byte-pointer differences instead of truncated addresses.
- On 64-bit hosts, allocations use `_Alignof(max_align_t)`. Native subpack
  bounds are aligned inward, never extending beyond the supplied buffer.
  Negative sizes, rounding overflow and allocations beyond capacity trap.
- The backing arena is independently compilable and explicitly aligned. It
  retains the retail `0xba9f0` start, `0x144e10` capacity and end guard. Larger
  runtime structures consume more of this capacity; playable 64-bit builds may
  need a separate arena-size policy.
- The existing GPU bridge is tested with real host pointers and 24-bit tokens.
  No host pointer is stored in a GPU packet link.

The tests link the actual allocator, arena and GPU bridge as separate C17
translation units. They substitute only the resident pool binding, the error
screen and the checkpoint-reset notification. Coverage includes addresses above
4 GiB on macOS ARM64, typed host allocations, low/high allocation, resizing,
bookmarks, pool switching, arena reset and token round trips. Invalid allocation
cases run in child processes and must terminate with a trap.

## Remaining game work

The complete game still has a CMake pointer-width guard and a corresponding
assertion in `game_layouts.h`. Retail layout assertions remain enabled. The
memory-only preset is a migration/test target, not a switch that permits an
unsafe 64-bit game build.

Next, separate binary asset layouts (four-byte addresses and offsets) from
runtime objects (host pointers). In particular:

1. Replace in-place host-address patching in `LOAD_RunPtrMap` with explicit
   decoding or address resolution, then update consumers of the affected assets.
2. Decode LNG/MPK/LEV/model tables without interpreting four-byte entries as
   host-pointer arrays. Keep binary sizes and strides explicitly verified.
3. Audit resident globals, callbacks carried in integers and fixed scratchpad
   offsets; host structures must not overlap retail-sized scratchpad slots.
4. Port checkpoint pointer slots and address tables before enabling 64-bit
   replay/savestate support.
5. Enable the full macOS ARM64 target only after those contracts are satisfied,
   then validate OpenGL context/shaders, audio and gameplay with retail assets.

The PS1 allocator path retains its original four-byte alignment and pointer
arithmetic. The existing native 32-bit game remains the baseline for behavior;
this milestone does not establish full game or PS1 binary parity.
