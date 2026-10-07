# ARM64 port progress

The port remains C17. Production memory modules, LNG decoding and bounded
pointer-map resolution build and run in tests natively on macOS ARM64. It does
**not** build a playable game yet.

## Build and test the memory milestone

With Xcode Command Line Tools, CMake 3.20+, Ninja and Python 3 available:

```sh
cmake --preset macos-arm64-memory
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory
```

The executables are `ctr_native_memory_tests`, `ctr_native_lng_tests` and
`ctr_native_ptrmap_tests` under `build-macos-arm64-memory/`. SDL and retail
assets are not needed for these tests.

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

## LNG asset decoding

The LNG wire header has two little-endian 32-bit fields: string count and offset
to an array of four-byte offsets relative to the file start. The native decoder
reads these fields byte by byte, including from unaligned buffers, and builds
a separate table of host `char *` pointers. It never patches the file in place.

Validation covers the header, count (at most 4096), complete offset table,
output capacity, each string offset and its null terminator. Text may precede
or follow the offset table, but cannot overlap the header or table. A null byte
inside the table or CD padding cannot terminate a string. Shared strings,
suffixes, empty strings and original byte encodings are preserved. A zero-count
file is valid for the decoder; the game loader rejects it because gameplay
requires language strings.

The native `LOAD_LangFile` path receives a typed BIGFILE pointer, validates the
entry before reading, and reserves a sector-rounded file buffer followed by
a pointer-aligned host table in one MEMPACK allocation. It reuses this allocation
on reload. The table is sized for the maximum valid count that can fit in the
configured file buffer, capped at 4096; this avoids leaking arena allocations
when languages have different string counts. `langBufferSize` remains fixed
for the lifetime of that allocation.

With the current `0x3f04` language limit, the raw buffer occupies `0x4000` bytes
and the table has capacity for 4094 pointers. This adds 32752 bytes on ARM64
(16376 on 32-bit native) compared with the sector buffer alone. The game loader
clears its published language state while reading and traps on invalid data.
Borrowed string pointers are valid until that buffer is overwritten or released;
callers of the standalone decoder own both its file and output storage.

Both file and table stay inside the existing MEMPACK checkpoint region. The
LNG relocation loop handles the separate table on the current 32-bit game.
Checkpoint payload version is now 4: version 3 snapshots held patched addresses
in the original LNG file and are rejected instead of being reused with the new
representation. This does not enable 64-bit checkpoints.

LNG tests use synthetic fixtures: offset tables before/after text, unaligned
buffers, maximum counts, truncated/corrupt files, 2000 malformed inputs, 100
reloads without further allocation, and MEMPACK release/reset. The native
decoder and arena are linked directly; the complete game's CD-loading path and
retail language files still need runtime validation once the game can build.

## Pointer-map decoding and resolution

`NativePtrMap_Decode` replaces in-place relocation for new ARM64 asset readers.
It accepts explicit asset and PTR byte lengths and decodes the PTR header's
little-endian byte count, followed by four-byte slot offsets. As in retail,
the low two bits of each slot offset are cleared. Each slot must contain a
complete four-byte relative target within the asset. A zero target resolves
to the asset start, preserving retail addition semantics; a target equal to
the asset size is an allowed end sentinel.

The caller supplies an array of `(slotOffset, targetOffset)` records. Decode
sorts it by slot and rejects duplicate normalized slots, incomplete maps,
out-of-bounds fields and invalid targets. On failure the view is cleared; the
record array is scratch until success. Neither asset nor PTR bytes change.
No global registry, heap allocation or packed host addresses are introduced.

`NativePtrMap_Resolve` looks up an exact slot by binary search and converts its
relative target to a full host pointer. The caller specifies the required object
size: a complete object must fit inside the buffer; zero bytes permits an end
sentinel. Resolution does not recursively traverse objects, so cycles and
shared targets are supported without extra ownership rules.

The view borrows its asset and record array. Keep both alive, clear/re-decode
the view after reload or release, and call `NativePtrMap_Rebind` after copying
or restoring the same asset at a new address. Records contain offsets only;
Rebind replaces the transient host base. It does not implement checkpoint
serialization or accept a file with different contents or length.

The existing `LOAD_RunPtrMap` remains a guarded 32-bit compatibility path:
native slot addressing now uses byte-pointer arithmetic and little-endian
unsigned writes, while the PS1 branch is retained. The ARM64 decoder is linked
into the platform unity chain and independently tested, but is deliberately
not substituted under the old MPK/LEV consumers. Those consumers directly
dereference pointer-bearing C structures and must first be converted into
explicit wire layouts plus runtime accessors. Putting offsets back into their
current pointer fields would break the existing game.

Tests cover unaligned buffers, unsorted maps, normalized flags, repeated loads,
origin/end references, cycles, shared targets, buffer relocation, duplicate
slots, invalid lengths/offsets/object sizes and 2000 malformed inputs. Synthetic
fixtures establish the relative-address contract; retail MPK/LEV maps and the
full game's loading callbacks remain to be validated.

## Remaining game work

The complete game still has a CMake pointer-width guard and a corresponding
assertion in `game_layouts.h`. Retail layout assertions remain enabled. The
memory-only preset is a migration/test target, not a switch that permits an
unsafe 64-bit game build.

Next, separate binary asset layouts (four-byte addresses and offsets) from
runtime objects (host pointers). In particular:

1. Connect the bounded pointer-map decoder to MPK/LEV loading callbacks with
   actual asset lengths, then replace their direct host-pointer consumers.
2. Apply the LNG separation to MPK/LEV/model tables without interpreting
   four-byte entries as host-pointer arrays. Keep binary sizes and strides
   explicitly verified; validate the LNG game integration with retail assets.
3. Audit resident globals, callbacks carried in integers and fixed scratchpad
   offsets; host structures must not overlap retail-sized scratchpad slots.
4. Port checkpoint pointer slots and address tables before enabling 64-bit
   replay/savestate support.
5. Enable the full macOS ARM64 target only after those contracts are satisfied,
   then validate OpenGL context/shaders, audio and gameplay with retail assets.

The PS1 allocator path retains its original four-byte alignment and pointer
arithmetic. The existing native 32-bit game remains the baseline for behavior;
this milestone does not establish full game or PS1 binary parity.
