# ARM64 port progress

The port remains C17. Production memory modules, LNG decoding and bounded
pointer-map resolution and MPK/LEV readers build and run in tests natively on macOS ARM64. It does
**not** build a playable game yet.

## Build and test the memory milestone

With Xcode Command Line Tools, CMake 3.20+, Ninja and Python 3 available:

```sh
cmake --preset macos-arm64-memory
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory
```

The executables are `ctr_native_memory_tests`, `ctr_native_lng_tests`,
`ctr_native_ptrmap_tests`, `ctr_native_assets_tests`,
`ctr_native_model_library_tests`, `ctr_native_model_animation_tests` and
`ctr_native_model_vertices_tests`, `ctr_native_model_commands_tests` and
`ctr_native_model_transform_tests`, `ctr_native_model_matrix_tests` and
`ctr_native_model_projection_tests`, `ctr_native_vram_tests` and
`ctr_native_model_draw_tests` and `ctr_native_raster_tests` and `ctr_native_instance_transform_tests` under
`build-macos-arm64-memory/`. SDL and retail
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
fixtures establish the relative-address contract; the real MPK/LEV map checks
below supplement them. Full game loading still requires runtime validation.

## MPK/LEV wire readers

`platform/native_asset_readers.c` uses the pointer-map resolver to read assets without
casting on-disk data to `struct Level`, `struct Model` or host pointer arrays.
All scalar reads are little-endian and tolerate unaligned input. Retail byte
sizes are explicit: Level `0x1f4`, Model `0x18`, ModelHeader `0x40`, InstDef
`0x40`, mesh info `0x20`, QuadBlock `0x5c`, vertex `0x10` and BSP `0x20`.

`NativeAsset_DecodeDram` reads the four-byte envelope used by
`LOAD_DramFileCallback`. It checks the embedded PTR offset and passes the exact
payload length, excluding the prefix and PTR, to `NativePtrMap_Decode`.
Negative offsets select the legacy separate-PTR path and are rejected by this
helper; that path needs separately known asset and PTR lengths.

`NativeMpk_Open` checks optional icon metadata and scans the inline four-byte
model-offset table from byte four to its unrelocated zero terminator. Each
listed model must fit completely. A relocated zero references the asset start,
as in retail; only an unlisted zero means NULL or terminates the MPK list.
Nonzero pointers absent from PTR are rejected.

`NativeLevel_Open` checks the complete root and the counted model-pointer and
instance-definition spans. Indexed MPK/LEV model access checks the model and
all its header records, preserving signed IDs (including `-1`) and copying
16-byte names into terminated host strings. Header access exposes signed LOD
distance, flags, scale and animation count. `NativeLevel_GetMesh` validates
nonnegative counts and complete quad, vertex and BSP spans using retail strides.
Geometry is returned as wire bytes, never pointer-bearing runtime structs.

Validation is staged: opening a level does not validate each model or its mesh;
call the corresponding accessors. Texture/command pointers, icon contents and
the remaining Level fields are not decoded yet. Animation/frame records and
instance definitions are decoded by the later readers below.
Bounds checks do not establish semantic validity or prohibit
overlapping spans. Keep the decoded map, entries and asset alive and unchanged;
reopen views after reload, release or Rebind. No global state or allocation is
introduced. These readers are linked into the native unity chain, but loading
callbacks and renderer consumers still use the guarded 32-bit compatibility path.

The new `ctr_native_assets` test covers the DRAM-to-PTR-to-MPK pipeline,
shared models, LEV tables, signed fields, unterminated fixed-width names,
unaligned buffers, pointers above 4 GiB on Apple Silicon, file immutability,
exact-end geometry spans, rebind, 100 reloads and 2000 mutated fixtures.
Rejection cases include missing relocations/terminators, truncated roots,
headers and envelopes, references into appended PTR, negative/oversized counts,
insufficient output capacity and spans extending past the asset. These are
synthetic fixtures, supplemented by the retail checks below. Complete game
loading remains untested.

To run only the new reader tests with full output and sanitizers:

```sh
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_assets$' -V
```

## Loading completion and real-asset validation

`native_asset_loading.c` accepts exact file lengths at the loading boundary.
Embedded DRAM completion publishes a decoded map only after all relocation
records validate. Negative prefixes enter `WAITING_PTR`, with the four-byte
prefix removed. Raw payload completion is also available for already unwrapped
LEV data. A separate PTR completion requires a pending payload, preserves it
for retry on error, and publishes `READY` only on success. Repeated PTR
completion is rejected. Reset/new DRAM completion clears old published state.
The payload and decoded records are borrowed; PTR bytes can be freed immediately
after successful decoding. No runtime structure or queue-layout changes are made.

The actual native `LOAD_DramFileCallback` now preflights the exact BIGFILE entry
length and pointer map before legacy relocation or MEMPACK shrinking. Scratch
relocation records are temporarily heap-allocated and freed before patching.
Downstream callbacks receive `size_UNUSED` as the payload length, excluding the
DRAM prefix and embedded PTR. `LOAD_ReadFile_ex` also rejects invalid entry
indices, nonpositive/overflowing sizes and invalid sector arithmetic before
issuing reads. The PS1 branch retains its original source behavior.

The 32-bit game still patches pointers and uses its existing callbacks and
consumers. The new completion adapters execute the unpatched ARM64 path in
tests and the standalone validator. Persistent wire-view publication from the
game's LEV/PTR callbacks, resident state and checkpoint ownership still needs
conversion; this step does not enable an ARM64 game build.

`ctr_native_asset_validate` reads extracted files, individual BIGFILE entries,
or the supplied `assets/ctr-u.bin` directly using the production MODE2/2352
disc reader. It checks root/table spans, every listed model/header, and LEV
geometry spans. It also checks animation/frame records and decompresses model
vertex streams as described below. Triangle/color/texture metadata and local vertex packing/interpolation are
decoded too. Q12 matrix probes use real model scales with synthetic identity
rotation/view/instance inputs. Projection probes use a declared synthetic
camera. Optional VRAM modes load retail rectangles and sample triangle-corner
texels. Actual game camera/rendering integration and gameplay remain unverified. Inputs are opened read-only; no asset extraction is
needed and CD padding is excluded from the decoder's payload size.

Build with either preset above. From the project root, validate real NTSC-U
assets with sanitizers:

```sh
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-mpk assets 259
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-mpk assets 260
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-lev assets 1
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-lev-ptr assets 201 202
```

Validated with the supplied disc on Apple Silicon:

| Entry/path | Models | Instances | Quads | Vertices | BSP nodes | Payload bytes | Relocations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Shared MPK, 259 | 52 | — | — | — | — | 295032 | 3562 |
| 1P Crash MPK, 260 | 44 | — | — | — | — | 277892 | 4389 |
| LEV, 1 | 15 | 66 | 1250 | 7652 | 515 | 487924 | 15900 |
| Hub LEV/PTR, 201/202 | 14 | 13 | 1583 | 7179 | 648 | 421164 | 12007 |

The hub entry includes a negative DRAM prefix: apply its separate PTR relative
to the payload **after** that prefix, not to the BIGFILE entry start.

Other validator forms (indices are decimal):

```sh
ctr_native_asset_validate mpk pack.mpk
ctr_native_asset_validate lev-dram track.dram
ctr_native_asset_validate lev track.payload track.ptr
ctr_native_asset_validate lev-external track.dram track.ptr
ctr_native_asset_validate big-mpk BIGFILE.BIG 259
ctr_native_asset_validate big-lev BIGFILE.BIG 1
ctr_native_asset_validate big-lev-ptr BIGFILE.BIG 201 202
```

The full presets run nine asset-independent CTest tests. If
`assets/ctr-u.bin` exists **when configuring**, CMake adds a tenth,
`ctr_native_asset_retail`, which validates the four entries above. Disc contents
remain local and ignored by Git. To rerun just loading/file validation or retail:

```sh
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_asset' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

Synthetic tests also cover two-stage completion, malformed PTR retry,
insufficient record capacity, stale-view clearing, reset, duplicate completion,
release of PTR storage after decoding, archive bounds and exact entry lengths
when sector padding would otherwise hide truncation.

## ARM64 model library and instance definitions

`native_model_library.c` provides a runtime library of 227 model references.
Each reference stores its borrowed decoded-map owner and a four-byte relative
model offset, not a cached wire pointer. Lookup by ID uses `NativeModel_Open`
to reopen and validate the model and header span against the current host base.
Rebind of unchanged asset bytes therefore preserves references. Before
re-decoding, overwriting or releasing an asset, call `DropOwner` while its map
identity is still available. The map, entries and payload remain caller-owned;
the library does not allocate or serialize pointers.

Store accepts MPK, LEV or a single model. IDs `0..226` are valid; `-1` is skipped
as in retail; other negative/oversized IDs are rejected before array indexing.
Later entries/sources replace earlier IDs. MPK/LEV stores are transactional:
if any model is invalid, no partial update becomes visible. Removing an owner
only removes its current references and does not resurrect overridden models.
`Clear` mirrors the retail function by clearing 226 entries and preserving slot
226; `Reset` and owner removal also clear that final slot.

`NativeLevel_GetInstance` decodes the 64-byte InstDef wire record into host
values: name, validated direct model reference, signed scale/position/rotation,
color, flags, the two unknown signed fields and the serialized model ID. It
never copies the on-disk runtime `ptrInstance` field into a host pointer, and
does not create a gameplay Instance or modify the asset. The model ID field is
preserved independently of the model's ID; lookup ownership and renderer
selection must not silently replace the direct model reference with another
source's same-ID override.

The validator now stores each MPK/LEV in the new library, looks up all registered
IDs and decodes all LEV instance definitions. The supplied disc passes these
checks for both MPKs and both levels above (66 and 13 instance definitions).
`ctr_native_model_library` covers overrides, ignored/invalid IDs, duplicate IDs,
transactional failure, clear/reset, owner release, rebind and 100 reloads.
Asset tests cover signed instance fields, stale runtime-pointer bytes, missing
model relocations, invalid model spans and cleared failure outputs. File tests
also exercise model-ID boundaries and instance references through the CLI.

To build and test this step with sanitizers:

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_model_library$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

The new library is linked into the native unity chain and used by the ARM64
validator. The game's existing `GameTracker.modelPtr`, `LibraryOfModels_Store`
and `INSTANCE_LevInitAll` still consume 32-bit patched structures; their switch
requires migration of resident ownership and gameplay/rendering consumers.
The existing native model-store and driver-extra paths now check the same ID
domain before indexing their legacy table; their PS1 branches are unchanged.
This step does not provide visual asset playback or enable the complete game.

## Animation and frame wire readers

`native_model_animation.c` reads animation tables through the decoded pointer
map, preserving full host addresses. ModelAnim has a `0x18`-byte wire header;
ModelFrame has a `0x1c`-byte prefix. Animation names, frame origins and scalar
metadata are decoded independently of host struct packing. The frame stride
is unsigned, matching the render bucket's access even though the old C field
was signed.

The high bit of `numFrames` selects interpolation, while the low 15 bits are
the logical frame count. Direct animations store that many frames; interpolated
animations require `floor(logicalCount/2)+1` stored records, including the next
frame needed at an even-count endpoint. Opening checks the complete animation
pointer table, animation header, nonzero logical count, stride of at least
28 bytes, complete stored frame span and optional first delta-table word.
Optional unlisted-zero pointers return `NOT_FOUND`; a relocated zero still
references asset origin. Nonzero pointers absent from PTR are rejected.

Frame access validates its relative vertex offset against the frame stride,
and exposes only the remaining frame bytes. The offset need not be 28. For
static frames the length is not serialized: the caller specifies how many
vertex bytes it needs, and the reader validates only that requested span.
The vertex decoder below derives the required static span from commands and
delta metadata. The supplied LEV contains a valid offset-34 frame. Delta
metadata is read as checked little-endian words, with each requested index
separately bounded against the asset.

`NativeAnimation_SelectFrame` clamps a logical request before selecting the
stored record(s). Odd interpolated requests return current and next frames;
direct/even requests return only current. It does not advance time, wrap an
animation or interpolate/decompress positions or vertices. Views borrow the
unchanged map, entries and asset; reopen them after reload or Rebind, and
discard any transient frame pointers before releasing the asset.

The validator checks every stored frame and the final clamped selection in
all listed animations. Real-disc results on Apple Silicon:

| Entry | Animations | Interpolated | Stored frames | Static frames |
| --- | ---: | ---: | ---: | ---: |
| Shared MPK 259 | 82 | 2 | 1029 | 50 |
| Crash 1P MPK 260 | 43 | 18 | 638 | 42 |
| LEV 1 | 7 | 0 | 186 | 23 |
| Hub LEV/PTR 201/202 | 1 | 1 | 16 | 14 |

These are record visits per listed model/header/animation, not deduplicated
counts of unique byte ranges. They establish bounded data access rather than
rendered-animation parity.

`ctr_native_model_animation` covers unaligned input, unsigned strides, direct
and interpolated frames, odd/even endpoints, signed origins, clamping, static
offsets, delta reads, rebind, file immutability, malformed tables/counts/strides/
offsets, missing relocations and 2000 mutated fixtures. CLI file tests include
valid/interpolated animation fixtures and malformed frame/table/delta targets.
To repeat just the new decoder and real-disc checks with sanitizers:

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_model_animation$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

The module is included in the native unity chain and standalone validator.
Gameplay and render bucket consumers still use their legacy structures;
Camera/renderer integration and resident migration remain
required before visual playback or an ARM64 game build.

## Bounded model vertex decoding

`native_model_vertices.c` decodes raw XYZ byte triples and compressed model
streams without patching assets or casting bytes to host structures. Output
coordinates retain the encoded byte representation, before frame origin/scale,
packed-coordinate transforms or interpolation. Packing promotes both raw and compressed stored bytes as unsigned; signed
delta accumulators have already been wrapped into the stored byte representation. Stream words are
little-endian; fields are consumed most-significant bit first in X,Z,Y order.
Eight-bit fields reset an axis; shorter signed fields accumulate the temporal
base and delta, wrapping through a signed byte. Cross-word reads require both
complete words. Failed incremental reads clear output and preserve decoder state.

A bounded scan of each header's command list derives the number of fresh
vertices. It skips the color-cache prefix, color-only commands and cached
vertex references, and requires the `0xffffffff` terminator within the asset.
This scan does not reconstruct faces, colors, textures or cached draw vertices.
Each temporal word is separately checked through the pointer map. For static
compressed frames, declared widths determine the stream size rounded to whole
32-bit words; raw frames require three bytes per fresh vertex. Animated streams
remain bounded by their frame stride.

Bulk decoding uses caller-owned output storage and reports `OUTPUT_TOO_SMALL`
for insufficient capacity. On any failure the returned count is zero; output
storage is scratch until success and may contain a decoded prefix. Sources,
map entries and views must stay valid and unchanged during decoding. The module
is included in the native unity chain and standalone validator; legacy render
bucket consumers still require conversion.

The validator decodes all stored animation frames, or the static frame for
headers without animations, when a command list is present. Real-disc results:

| Entry | Decoded model vertices |
| --- | ---: |
| Shared MPK 259 | 105473 |
| Crash 1P MPK 260 | 85735 |
| LEV 1 | 7404 |
| Hub LEV/PTR 201/202 | 2316 |

These total 200928 vertex visits, including repeated model/frame data; they are
not unique positions or LEV terrain vertices. Successful bounded decompression
does not establish rendered parity.

`ctr_native_model_vertices` checks golden raw/compressed coordinates, signed
bases and byte wrap, unaligned input, cross-word truncation, transactional
state, and 2000 streams against an independent bit-by-bit oracle. Synthetic
asset tests cover fresh/cache/color command counts, raw and compressed static
and animated decoding, output capacity, truncated frames/delta tables and a
missing command terminator. The retail test requires vertex decoding output.
With the supplied disc present, both presets run 19 tests; without it, 18 run.

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
# Focused vertex and real-disc checks:
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_model_vertices$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

## Model triangles, source colors and texture metadata

`native_model_commands.c` reconstructs triangle topology before projection or
culling. It follows `RenderBucket_DrawFunc_Normal`: fresh vertices populate a
256-slot cache, cached references retain their original fresh-vertex indices,
and each vertex advances a four-entry rolling FIFO. Bit 31 restarts a strip;
its first triangle uses the restart command's material/flags. Later triangles
use the current command. Bit 30 continuations replace the triangle's first
vertex with the preceding FIFO entry. Color-only commands update the rolling
colors without consuming vertex data. No winding swap is introduced; original
command bits remain available for the renderer's culling and material rules.

Open requires a command terminator within the asset and validates the color
cache prefix/span (at most 128 indexed words). Each direct source RGB word is
checked against the asset; cached reads also require an index below the copied
prefix count. Referencing an unwritten vertex cache slot fails. Palette and
texture-table lengths are not serialized: only the accessed ranges can be
validated, not their semantic ownership within the asset.

Triangle indices refer to the array produced by the previous vertex decoder.
One-based texture indices resolve four-byte table slots through PTR, followed
by a complete 12-byte TextureLayout. UV bytes, CLUT and texture-page words are
decoded explicitly, independent of host packing. Index zero and unlisted-zero
table entries give untextured triangles; listed zero still means asset origin.
Nonzero pointers missing from PTR are invalid. UV metadata does not include
VRAM pixels, animated-texture updates or material/lighting execution.

The iterator allocates nothing and borrows the unchanged asset/map/entries.
Reopen after reload or Rebind. Successful `Next` returns one triangle; exhaustion
returns `NOT_FOUND` and consumes trailing commands. Errors clear output and
preserve iterator state. The validator runs one command-list pass per listed
model/header, independently of repeated frame vertex decoding:

| Entry | Triangles | Textured |
| --- | ---: | ---: |
| Shared MPK 259 | 4396 | 3320 |
| Crash 1P MPK 260 | 6376 | 4354 |
| LEV 1 | 1402 | 685 |
| Hub LEV/PTR 201/202 | 1620 | 668 |

These are 13794 triangle visits (9027 textured), not unique faces or terrain
quads. All referenced source colors and texture metadata pass bounds checks;
this does not confirm their rendered appearance. Legacy render consumers still
use their resident structures, and the complete ARM64 game remains guarded.

`ctr_native_model_commands` checks golden triangle indices, restart-material
selection, bit-30 continuation, cache reuse, color-only commands, UV/CLUT/page
endianness, immutable unaligned buffers, full host addresses and Rebind. It also
checks missing terminators, unwritten cache slots, cached color bounds, truncated
color/texture spans, absent relocations, null versus origin pointers, exhaustion
and 2000 mutated command lists with transactional error-state checks.

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
# Focused command and real-disc checks:
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_model_commands$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

## Local vertex packing and halfway interpolation

`native_model_transform.c` prepares the coordinate words used by the normal
render bucket before GTE matrix/projection operations. It follows the existing
`PackModelVertexXY`, `PackInterpolatedModelVertexXY`, `ModelVertexZ` and
`InterpolatedModelVertexZ` functions. Input uses the byte representation from
the vertex decoder: raw and compressed axes are unsigned stored bytes. Signed
delta accumulators wrap into u8 before packing, as in RenderBucketVertex.
Signed frame origins and packed carries remain separate from byte signedness.

For direct frames the first origin component is masked with `0x7fff`. The
X/Z vertex pair is combined with the first two origin components in a single
32-bit packed addition, shifted left by two and masked with `0xfff8ffff`.
The vertical Y byte plus the third origin component is shifted by two.
Halfway frames sum both packed vertices and the two frame origins, shifting
by one instead. This intentionally preserves cross-half carries, wrap and
masking; independent floating-point XYZ averaging would change the result.
Unsigned operations avoid shifting negative signed integers. Position access
returns the signed low/high halves consumed by the GTE (X/Z/Y axis order).

Bulk static and animation APIs use caller-owned current/next scratch arrays
and packed output, with no allocations. Arrays must not overlap. Logical
animation requests use SelectFrame clamping; only odd half-rate requests
decode and combine the next stored frame. Direct/even requests need no next
scratch. Each source stream/delta table keeps the previous reader's bounds
checks. Failed calls return count zero; scratch/output are unpublished until
success. Source buffers and maps remain unchanged and views must be reopened
after Rebind or reload.

This is local packing, not model/instance scale, rotation, view matrices,
projection, special split/reflection paths or timing advancement. The new
module is included in the native unity chain and standalone validator;
legacy game render consumers remain unchanged. Full ARM64 compilation is
still guarded, with the same pre-existing layout and global-initializer errors.

The validator now prepares static models and every logical animation frame,
including halfway frames, separately from its stored-frame decode pass:

| Entry | Packed vertex visits |
| --- | ---: |
| Shared MPK 259 | 106151 |
| Crash 1P MPK 260 | 109283 |
| LEV 1 | 7404 |
| Hub LEV/PTR 201/202 | 2841 |

These total 225679 vertex visits, not unique geometry. Successful calculation
and bounds checks establish no visual parity until matrix/render consumers run.

`ctr_native_model_transform` checks golden packed words and signed position
halves, negative origins, unsigned raw/compressed stored coordinates,
and 10000 randomized comparisons with a wide-integer reference calculation.
Synthetic assets exercise static/direct/interpolated bulk packing, odd/even
logical endpoints, clamping, missing next scratch, capacity, unaligned buffers,
immutability, Rebind, compressed stream integration and a malformed next frame.
Both complete preset suites pass 19 tests with the supplied disc present.

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
# Focused local-transform and real-disc checks:
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_model_transform$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

## Q12 linear model matrices

`native_model_matrix.c` stores runtime 3x3 matrices as signed halfwords,
independent of resident MATRIX layouts. Q12 one is 4096. Compose multiplies
left and right matrices in that order, floors each signed dot product after
12 fractional bits and clamps to signed IR range. Build follows the normal
RenderBucket_BuildM3x3 two-stage path: first model/instance scale, then rotation
multiplied by the resulting saturated diagonal matrix. View rotation is supplied
separately through Compose; angles and trigonometric tables are not generated.

The model-scale shift changes from zero to two at view depth 4096, using the
signed result of the wrapping MIPS subtraction. Model coefficients are first
unsigned halfwords, shifted logically, then interpreted as signed GTE matrix
coefficients. PIXEL_LOD computes floor(depth/2)+4096 with 32-bit wrap, multiplies
instance scales retaining only the low 32 bits, floors by 4096 and narrows to
signed halfwords. These rules preserve negative inputs and extreme depth wrap;
ordinary floating-point scaling or signed overflow would not match the path.

Apply takes the signed input halves of a prepared packed vertex. It returns
the shifted linear dot products and saturated IR coordinates, with a simple
three-bit saturation diagnostic. Wide intermediates avoid overflow; negative
rounding uses defined division/remainder instead of implementation-dependent
signed right shifts. This diagnostic is not GTE FLAG and this module does not
implement translation, perspective division, screen/depth FIFOs, clipping,
lighting, split/reflection paths or the hardware's projection accumulator rules.

Inputs are caller-owned and outputs must not overlap inputs. Outputs clear on
argument errors. Operations allocate nothing, access no globals and retain no
pointers. The native unity chain and standalone validator include the module;
legacy game rendering still requires conversion and the full ARM64 guard stays.

For each prepared retail vertex, the validator applies two matrix probes:
actual header scale with identity rotation/view/instance scale at synthetic
depths 0 and 4096. This tests scale-data access and linear computation, not a
real gameplay camera or resident instance transform. Counts are twice the
local-packing visits: shared MPK 212302, Crash MPK 218566, LEV 14808 and hub
5682, totaling 451358 linear applications.

`ctr_native_model_matrix` checks identity and quarter-turn matrices, unequal
axis scales, composition order, the near/far threshold, PIXEL_LOD, negative
fractional rounding, signed-halfword narrowing, saturation and extreme depth/
product wrap. It also compares 10000 randomized matrix compositions, packed
vertex applications and scale builds with independent wide-integer reference
calculations, checking that inputs remain unchanged. Complete normal and
ASan/UBSan preset suites pass 19 tests with the supplied disc present.

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
# Focused matrix and real-disc checks:
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_model_matrix$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

## Camera translation and explicit GTE projection state

`native_model_projection.c` prepares camera-relative translations and implements
the current native GTE RTPS/RTPT path with SF=1, LM=0. Configuration contains a
Q12 rotation, signed integer translation, Q16 screen offsets, H, DQA and DQB.
All screen/depth FIFOs are caller-owned; no operation touches global gteRegs.
Project advances one vertex; Project3 advances three vertices, accumulates flags
and leaves the last vertex's MAC/IR/depth-cue registers in its result.

ViewTranslation follows GetViewPosition and AdjustViewPositionForMvp: wrapping
camera subtraction, signed low16 camera-relative inputs, view rotation and IR
clamping. Screenspace instances bypass camera subtraction/rotation. Raw depth
is captured before the wrapping near-depth <<2 and optional arithmetic
DRAW_HUGE >>2 adjustments, allowing callers to drive the scale/LOD path.
No camera matrix or rotation angles are inferred from LEV instance definitions.

Projection uses the native GTE reciprocal lookup/refinement, including the
H>=2*SZ divide-overflow path. It returns shifted/truncated MAC coordinates,
saturated IR and unsigned depth, screen coordinates limited to [-1024,1023],
MAC0, IR0, ratio and flags. Behind-camera or saturated vertices return OK with
flags; callers must apply the renderer's visibility rules. Invalid arguments
clear output and preserve FIFO state. Inputs/config/state/output must not
overlap. There is no allocation, rasterization, clipping or triangle culling.

The compatibility target is the existing native core, not independent PS1
hardware verification. In particular, this step preserves its asymmetric
accumulator thresholds, truncation behavior and flag-summary quirks. Changes
to those contracts require separate hardware/reference validation. The source
retains the PsyCross/REDRIVER2 MIT provenance notice. In native_gte_core.c,
bit-31 flag shifts now use unsigned operands and RTPS/RTPT translation shifts
use wide multiplication; this removes UB exercised by the sanitizer reference
without changing these routines' intended values. Other GTE opcodes remain
outside this validation scope.

The asset validator projects the same 451358 near/far matrix vertex visits
with a synthetic camera: identity view, camera-relative position (0,0,4096),
H=256, offsets (160,120), DQA/DQB=0, and actual model scale. The FIFO carries
probe state across vertices. These are numerical asset probes, not gameplay
camera state, visible-face counts or visual parity. Legacy resident camera and
render-bucket consumers still need integration; full ARM64 compilation remains
guarded.

`ctr_native_model_projection` compares 10000 random RTPS/RTPT cases directly
with the production native GTE core, checking MAC/IR, flags and all screen/depth
FIFO entries. Another 10000 camera-translation cases compare view rotation with
the core and independently check adjustment/wrap. Golden tests cover screen
coordinates, three-vertex FIFO order, divide threshold/zero/negative depth,
screen saturation, screenspace bypass, near/far and DRAW_HUGE, full signed depth
extremes and transactional argument failures. The new module leaves global GTE
state unchanged. Complete normal and ASan/UBSan suites pass 19 tests with the
supplied disc present.

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
# Focused projection and real-disc checks:
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_model_projection$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

## VRAM rectangle loading and integer texel sampling

`native_vram.c` binds caller-owned 1 MiB storage containing 1024x512 little-endian
16-bit words. No host alignment or resident renderer structures are required.
Bind preserves storage; the standalone validator explicitly zero-initializes it.
Loads cannot alias destination storage and must use immutable source bytes.

The reader follows LOAD_VramFileCallback: a single 20-byte VramHeader has its
rectangle at byte 12; a first word of `0x20` selects concatenated chunks with
four-byte lengths and a final zero length. Chunk size uses `size & ~3`. Header
metadata in the first 12 bytes is opaque, matching the loader. Positive width/
height, the complete word span and the physical 1024x512 rectangle are checked.
A full preflight precedes every write, so malformed later chunks leave VRAM
unchanged. Ordered overlapping rectangles overwrite earlier pixels. Unused
trailing bytes are allowed; CD padding cannot complete a truncated input span.

Sample interprets byte UV coordinates and TPAGE/CLUT: mode 0 extracts one of
four nibbles per word, mode 1 one of two bytes, and both resolve a palette word.
Mode 2 is direct 16-bit, while reserved mode 3 preserves the native renderer's
16-bit fallback. The sampler bounds physical addresses; it deliberately does
not emulate the renderer's edge clamping, UV filtering, texture windows,
blending, modulation or texture animation. Out-of-range source/palette addresses
return invalid data. Only color word zero is transparent; `0x8000` is visible
black with STP set. RGB8 uses five-bit replication, alpha is 0/255, and STP is
reported separately rather than folded into alpha.

New validator modes load raw VRAM assets, or combine one VRAM entry with model
validation. Shared model probes use VRAM entry 258; LEV probes use their VRAM
entry. They sample three integer UV corners per textured triangle, not every
texel in each triangle. Unloaded VRAM stays zero; a successful sample confirms
an address and decoding, not that all runtime texture residency is reproduced.

| Model entry | VRAM entry | Rectangles | Uploaded words | Sampled corners |
| --- | ---: | ---: | ---: | ---: |
| Shared MPK 259 | 258 | 2 | 57344 | 9960 |
| Crash MPK 260 | 258 | 2 | 57344 | 13062 |
| LEV 1 | 0 | 2 | 229376 | 2055 |
| Hub LEV/PTR 201/202 | 200 | 2 | 114688 | 2004 |

The four passes total 27081 corner samples; VRAM 258 is loaded twice into
independent buffers. No asset extraction, export or changes to the image occur.
The new module is in the native unity chain and validator. Resident loading,
GPU upload and render consumers still require ARM64 integration.

`ctr_native_vram` checks single/list formats, low-bit chunk rounding, ordered
overlap, complete preflight, unaligned storage/input, truncated/sentinel/rectangle
failures, source alias rejection, palette indices, nibble/byte order, direct and
reserved modes, RGB/STP/transparent-black semantics and physical boundaries.
10000 randomized samples compare with an independent byte-address/index oracle.
CLI tests check valid and truncated rectangle files and source immutability.
The optional retail test runs all four VRAM/model combinations. Complete normal
and ASan/UBSan suites pass 19 tests with the supplied disc present.

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_vram$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
# From the project root:
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-vram assets 258
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-mpk-vram assets 259 258
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-mpk-vram assets 260 258
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-lev-vram assets 1 0
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-lev-ptr-vram assets 201 202 200
# Extracted raw VRAM input:
build-macos-arm64-memory-sanitized/ctr_native_asset_validate vram path/to/asset.vrm
```

## Connected frame-to-triangle preparation

`native_model_draw.c` joins frame decoding/packing, command topology, projection
and optional VRAM sampling behind Open/Next. Open takes a model/header, static
selection (`animationIndex=UINT32_MAX`) or a logical animation frame, a caller
projection configuration and disjoint caller-owned current/next/packed arrays.
Animation clamping/interpolation and unsigned stored-byte packing retain the earlier
modules' contracts. No allocations or resident game layouts enter the module.

Next returns one NativeDrawTriangle with source fresh-vertex indices, RGB words,
material command bits, UV/CLUT/page metadata, three projected screen/depth corners,
projection flags and optional integer corner texels. Source indices are checked
against the prepared packed array before access. A projection/texture failure
clears output and preserves the entire command iterator; trailing command
exhaustion commits normally and returns NOT_FOUND. Projection config is copied;
assets/maps/entries, packed storage and VRAM remain borrowed and immutable during
iteration. Reopen after reload or Rebind.

Each triangle deliberately uses a fresh RTPT projection of all three vertices.
Screen/depth geometry can therefore feed a preview consumer, but flags aggregate
all corners and differ from the legacy continuation RTPS optimization. Original
command bits and corner order remain intact. Signed area and arithmetic average
depth are diagnostics, not native NCLIP/AVSZ3/ordering-table policy. This module
does not cull, clip, light, blend, sample triangle interiors or rasterize. It is
not yet a renderer or a playable ARM64 game.

The validator runs the connected path for each listed header: static models or
the first present animation, choosing logical frame 1 when halfway interpolation
is available and otherwise frame 0. It uses actual model scale with synthetic
identity rotation/instance scale, depth 4096, H=256 and offsets (160,120). The
existing separate readers still validate all frames. Results for one pass (updated after the unsigned stored-byte packing correction):

| Entry | Prepared/projected triangles | Corner pixels with VRAM | Zero screen area |
| --- | ---: | ---: | ---: |
| Shared MPK 259 | 4396 | 9960 | 1986 |
| Crash MPK 260 | 6376 | 13062 | 3477 |
| LEV 1 | 1402 | 2055 | 684 |
| Hub LEV/PTR 201/202 | 1620 | 2004 | 316 |

All 13794 topology visits now pass the connected path; VRAM modes additionally
sample 27081 corners through it. Screen degeneracy is reported, not an asset
failure: the synthetic camera and integer rounding can collapse small faces.
No visibility or rendered appearance is established. Existing no-VRAM CLI modes
exercise the same triangle path and omit pixel sampling.

`ctr_native_model_draw` checks a golden two-triangle strip with exact screen
coordinates, depths, signed areas, source colors and texels; static, halfway and
clamped frames; continuation topology; optional VRAM; end state; capacity and
missing-scratch errors; immutable unaligned assets and Rebind. Texture failure
after projection and an unwritten vertex-cache reference roll back iterator
state. CLI synthetic fixtures require connected output and reject invalid cache
use; retail tests require the pipeline output in all eight validation modes.
Complete normal and ASan/UBSan preset suites pass 19 tests with the supplied disc.

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_model_draw$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

## Diagnostic software preview

`native_raster.c` consumes projected triangles in caller-owned RGB and integer
Z buffers. It uses pixel-center/top-left coverage, two-sided triangles, viewport
scissoring, affine UV/color/depth interpolation and nearest-depth replacement.
Equal depth keeps the existing pixel. Texture RGB is modulated by vertex RGB/128
and clamped; word-zero texels write neither RGB nor depth, while STP is opaque.
Degenerate triangles, zero depth and GTE depth/divide overflow are skipped.
Surviving texture reads are preflighted before a triangle writes any pixels;
an invalid read clears output statistics and preserves the target. Buffers must
be disjoint; views must remain unchanged after successful Bind. No heap allocation
or resident game structures are required by the rasterizer.

This is a diagnostic renderer: it does not implement near-plane clipping,
lighting, material blending, texture windows, dithering, ordering tables, runtime
visibility or framebuffer feedback. It is independent of OpenGL and SDL.

The validator can list model array ordinals (distinct from model IDs) and export
a single chosen header and frame as a binary PPM, 512x512 RGB. Header indices select
model LOD/variants. `auto` chooses the first present animation, or a static frame
when none are present; `static` explicitly requests static data. The camera is
synthetic: it fits the prepared model bounds and stays above the GTE H/2 divide
threshold. Empty/non-renderable selections fail without opening the output file.
A successful render opens the requested file for replacement; export I/O failure
may leave a partial output. Inputs, including the supplied disc, are read-only.

```sh
build-macos-arm64-memory/ctr_native_asset_validate disc-models assets 260
build-macos-arm64-memory/ctr_native_asset_validate disc-preview assets 260 258,0 36 0 auto 0 build-macos-arm64-memory/crash-preview.ppm iso
# Extracted files use: preview MPK_FILE VRAM_FILE MODEL_INDEX HEADER_INDEX ANIMATION_INDEX|auto|static FRAME OUTPUT.ppm [front|side|top|iso]
# Optional conversion for viewing with macOS tools:
sips -s format png build-macos-arm64-memory/crash-preview.ppm --out build-macos-arm64-memory/crash-preview.png
```

**Visual regression found and corrected:** the initial Crash preview exposed a
packing mismatch. The bounded C17 path sign-extended compressed stored bytes,
but the original renderer's RenderBucketVertex uses u8 fields. Negative X after
promotion contaminated the whole upper packed Z half; Y values >=128 also became
negative. Packing now uses unsigned stored bytes for both raw and compressed
streams, with signed frame origins and the original 32-bit carries unchanged.
The earlier signed-coordinate golden/oracle expectations were wrong and have
been corrected, with explicit 127/128/255 boundary regressions. Decoder temporal
bases, signed delta arithmetic, byte wrapping and command topology are unchanged.

The preview uses the near-model matrix branch (depth probe 0) before fitting
its synthetic camera. Crash ordinal 36, MPK 260/header 0/auto animation/frame 0,
still yields 272 triangles. The earlier flattened quadrilateral now has structured
racer geometry. Full retail render parity remains unverified.

## Preview camera axes and ordered VRAM uploads

The initial preview incorrectly swapped the second and third GTE inputs a second
time. The packing path already produces GTE/model XYZ from stored X,Z,Y;
RenderBucket_BuildM3x3 applies the instance/model matrix directly to those inputs.
The preview now preserves this model basis and flips image Y for its downward
screen convention. Front is the default; optional `side`, `top` and `iso` views
rotate this basis before fitting bounds. Iso uses a fixed Q12 yaw/pitch matrix.
These are inspection cameras, not the live game's camera or driver rotation.

Disc modes with VRAM accept an ordered comma-separated list of up to 16 unsigned
indices, without spaces. Each file loads into the same zero-initialized VRAM;
later rectangles replace overlapping words and other uploaded regions survive.
Each individual file remains atomic on invalid data. The private CLI VRAM may
contain successful earlier files when a later upload fails, but no preview file
is opened. Existing single-index commands remain valid. Extracted-file preview
still takes one VRAM file; a packed multi-rectangle file remains supported.

For the NTSC-U Dingo Canyon 1P probe, use `258,0`: MainMain loads shared VRAM 0x102
(258) at boot; LOAD_TenStages queues the chosen level VRAM later, and
LOAD_GetBigfileIndex selects entry 0 for this track/LOD. MPK 260 is the corresponding
Crash arcade pack selected by LOAD_DriverMPK. This reproduces those texture uploads
in source order; it does not reconstruct framebuffer writes, runtime material
changes or a complete scene. The iso preview now shows orange racer geometry and
kart details; recognizability alone is not proof of exact rendering behavior.

```sh
# Same model/frame/textures, different inspection cameras:
for view in front side top iso; do
  build-macos-arm64-memory/ctr_native_asset_validate disc-preview assets 260 258,0 36 0 auto 0 "build-macos-arm64-memory/crash-$view.ppm" "$view"
  sips -s format png "build-macos-arm64-memory/crash-$view.ppm" --out "build-macos-arm64-memory/crash-$view.png"
done
```

`ctr_native_raster` tests exact small-image coverage, shared-edge ownership,
reversed winding, scissoring, depth ties/occlusion, interpolated colors/depth,
transparent and STP texels, affine UV sampling, buffer guards and rollback after
an invalid texture read. CLI fixtures test complete file-to-image production,
RGB dimensions/content, source immutability and rejection of invalid headers or
vertex-cache use without creating an image. An asymmetric synthetic fixture
asserts an exact pixel that would be missing under the old axis swap. CLI tests
reject malformed/overflowing/overlong upload lists and unknown camera names.
VRAM tests check cross-file overlaps, preservation and upload-order effects. The
optional retail test renders all four Crash views with uploads 258 then 0, checks
valid distinct images and upload order, and rejects a failed later upload without
creating an output. These checks establish image production, not retail parity.
Normal and ASan/UBSan suites pass 19 tests with the supplied disc, 18 without it.

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_raster$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

## Animation sequence preview

The validator now lists animation names and exports bounded logical frame ranges:
`disc-animations ASSETS_DIR MPK_INDEX MODEL_INDEX HEADER_INDEX` (or
`animations MPK_FILE MODEL_INDEX HEADER_INDEX`) and `disc-sequence`/`sequence`.
The sequence first prepares every requested frame and fits one shared camera to
the union of their bounds. It then reuses its workspace, VRAM and framebuffer to
render every frame with that same projection. This avoids per-frame centering
that could hide translation or create apparent camera motion. Odd half-rate
logical frames use the existing halfway packing path; the decoder is unchanged.

Sequence COUNT must be 1..256 and the entire FIRST/COUNT range must lie inside
the chosen animation's logical range. Static data is rejected. Unlike the single
preview's clamping behavior, sequences reject out-of-range requests. Files use
`OUTPUT_PREFIX-000000.ppm` with the actual logical index. Parent directories must
already exist. Existing sequence files are rejected before rendering, and files
are opened with C17 exclusive creation to avoid replacement. Malformed frame
geometry is rejected during the initial bounds pass; later texture or I/O failure
may leave previously exported frames or a partial current file. This is not an
atomic multi-file transaction. Single-image preview keeps its existing behavior.

For MPK 260/model 36/header 0, supplied NTSC-U data lists:

| Index | Name | Logical frames | Stored frames | Half-rate interpolation |
| --- | --- | ---: | ---: | --- |
| 0 | turn | 21 | 21 | No |
| 1 | reverse | 7 | 4 | Yes |
| 2 | bump | 15 | 8 | Yes |
| 3 | jump | 4 | 4 | No |

```sh
cmake --build --preset macos-arm64-memory
build-macos-arm64-memory/ctr_native_asset_validate disc-animations assets 260 36 0
build-macos-arm64-memory/ctr_native_asset_validate disc-sequence assets 260 258,0 36 0 1 0 7 build-macos-arm64-memory/crash-reverse side
python3 tools/preview_sequence_html.py build-macos-arm64-memory/crash-reverse build-macos-arm64-memory/crash-reverse.html
open build-macos-arm64-memory/crash-reverse.html
```

Use a new output prefix to repeat export while preserving existing frames.
`preview_sequence_html.py` validates consecutive native PPM frames, converts
copies to PNG using macOS `sips` in a temporary directory and embeds them in one
standalone HTML file. Source PPMs stay unchanged. The viewer has play/pause and a
logical-frame slider; its default 15 fps (`--fps 1..60`) is an inspection speed,
not verified game timing. This does not implement gameplay playback or audio.

CLI synthetic tests cover a translating five-frame half-rate animation with an
exact fixed camera and five distinct rendered frames, including halfway frames;
metadata listing, bounds/count rejection, preserved existing output and a corrupt
later frame rejected before any image is created. The retail test exports all
seven reverse frames with a shared camera and checks valid images and motion.
Normal and ASan/UBSan suites still pass 19 tests with the supplied disc.

```sh
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_asset_validate$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

## Authored LEV instance transforms

`native_instance_transform.c` bridges NativeInstanceDefView to the normal model
projection configuration using the decoded position, scale and rotation. It
retains pointer-free runtime camera and matrix values; no 64-bit overlay is cast
onto retail instance records and no runtime ptrInstance is published.

Rotation follows INSTANCE_LevInit -> ConvertRotToMatrix: 4096 angle units per
turn, quarter-turn lookup with quadrant sign rules, and Y * X * Z composition.
The existing 1024 sine/cosine pairs from game data.trigApprox were moved unchanged
to native_trig_table.inc, which both the original game initializer and this
module include. All pairs were compared with the original source. It deliberately
does not use floating-point trigonometry or the different native_libgte rsin LUT.

The normal projection bridge first computes raw camera-relative depth, with the
existing wrapping/low16/IR/near/DRAW_HUGE rules. It builds the world model matrix
from authored rotation and actual header/instance scale (including PIXEL_LOD),
then composes the camera view. This order preserves intermediate Q12 rounding
and saturation. SCREENSPACE_INSTANCE bypasses camera subtraction/rotation for
translation only; the normal matrix still receives the view transform. Camera
screen offsets, H, DQA and DQB are copied. Errors clear both outputs, inputs must
be disjoint and immutable, and no heap or global GTE registers are used.

This is explicitly the normal matrix path: custom/always-north billboard
matrices, reflection/splits, instance colors/materials, visibility/LOD selection,
runtime tick callbacks and animation timing remain outside this bridge. The
validator probes every listed header, not the gameplay-selected visible LOD.
GTE overflow flags and screen degeneracy remain diagnostic results, not asset
validation failures. The camera is supplied explicitly; retail probes currently
use identity view at (0,0,-4096), H=256, offset (160,120).

Existing LEV CLI modes now run the connected draw iterator for every referenced
instance model/header, using the authored transform and a static frame or the
first present animation (halfway frame 1 when available). Results with supplied
NTSC-U assets:

| Entry | Instance definitions | Triangle visits | GTE-flagged triangles | Zero screen area |
| --- | ---: | ---: | ---: | ---: |
| LEV 1, Dingo Canyon | 66 | 5108 | 2464 | 3303 |
| Hub LEV/PTR 201/202 | 13 | 1150 | 0 | 656 |

These are transformed triangle probes, not a rendered level scene. The model
preview/animation viewer retains its fitted inspection camera and is unchanged.

`ctr_native_instance_transform` checks cardinal/mixed/wrapped angles, 10000
compositions against the resident GTE matrix-column operations, authored position
and unequal scale, near/far, SCREENSPACE, DRAW_HUGE and PIXEL_LOD behavior,
projection fields and cleared error outputs. A non-identity view confirms scale
is applied before the view multiplication. CLI fixtures pass a rotated/scaled
instance through decoded LEV -> matrix -> projected triangle and preserve source
bytes; retail modes require the new Instance draw OK output.
Both preset suites pass 19 tests with the disc (18 without it). The full-game
syntax baseline remains 668 layout assertions plus its existing global initializer
error; the playable target is still guarded.

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized --output-on-failure
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_instance_transform$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-lev-vram assets 1 258,0
build-macos-arm64-memory-sanitized/ctr_native_asset_validate disc-lev-ptr-vram assets 201 202 258,200
```

## Diagnostic multi-instance LEV scenes

The validator now renders selected authored LEV instances into one 512x512 RGB
and depth target. Each object retains its decoded position, rotation and scale;
the target is cleared once and the nearest opaque fragment wins across objects.
This connects LEV definitions to the existing native projection, draw iterator,
VRAM sampler and software rasterizer without publishing runtime host pointers
inside binary records. Source assets remain immutable.

```text
disc-scene ASSETS_DIR LEV_INDEX VRAM_INDICES FIRST COUNT OUTPUT.ppm [VIEW]
disc-scene-ptr ASSETS_DIR LEV_INDEX PTR_INDEX VRAM_INDICES FIRST COUNT OUTPUT.ppm [VIEW]
disc-scene-near ASSETS_DIR LEV_INDEX VRAM_INDICES ANCHOR COUNT OUTPUT.ppm [VIEW]
disc-scene-ptr-near ASSETS_DIR LEV_INDEX PTR_INDEX VRAM_INDICES ANCHOR COUNT OUTPUT.ppm [VIEW]
scene LEV_FILE PTR_FILE VRAM_FILE FIRST COUNT OUTPUT.ppm [VIEW]
scene-dram LEV_FILE VRAM_FILE FIRST COUNT OUTPUT.ppm [VIEW]
```

VIEW defaults to front and accepts front, side, top or iso. VRAM_INDICES is the
existing ordered comma-separated upload list. COUNT is 1..256 and FIRST/ANCHOR
is a zero-based instance ordinal. Consecutive selection must fit the remaining
instance range. Nearest selection includes the anchor, then ranks all definitions
by squared distance between authored positions, before filtering unsupported
paths; ties retain anchor first and then source order. COUNT cannot exceed the
level's instance count. Logs identify the actual selected ordinals and positions.

Each supported instance uses its first available normal header and static frame
or frame 0 of the first present animation. The diagnostic camera fits aggregate
world bounds using the far matrix scale, then each instance goes through the
normal adapter's actual near/far matrix and translation rules. This is a synthetic
inspection camera, with no gameplay visibility or LOD selection. Instances with
PIXEL_LOD, SCREENSPACE, CUSTOM_MATRIX or DRAW_HUGE and always-north headers are
skipped and counted; missing usable frames/headers are counted separately.
Malformed selected data and zero fragment writes fail before opening the output.
The PPM replaces an existing output on success; an I/O failure may leave a partial
file, as with the single-model preview.

To inspect six nearby Dingo Canyon objects with the supplied NTSC-U disc:

```sh
cmake --build --preset macos-arm64-memory
build-macos-arm64-memory/ctr_native_asset_validate disc-scene-near assets 1 258,0 0 6 build-macos-arm64-memory/dingo-near.ppm iso
sips -s format png build-macos-arm64-memory/dingo-near.ppm --out build-macos-arm64-memory/dingo-near.png
open build-macos-arm64-memory/dingo-near.png
```

This selects instances 0, 22, 3, 14, 15 and 27: the C collectible, a cow skull,
three cacti and a question crate. The diagnostic visits 320 triangles with
25 skipped triangles and 4015 opaque fragment writes. Consecutive records can
be far apart; fitting the entire level can yield tiny objects and GTE clipping
or wrapping. Nearest selection provides a more useful local inspection.

Terrain/track quads, instance materials/colors, transparency, lighting,
billboards, native visibility, camera behavior and gameplay remain unverified
and outside this scene mode. This image is a group of level objects, not a
playable level or evidence of renderer parity.

CLI fixtures verify two separate silhouettes survive in one target, embedded
and external PTR routes agree, shared-depth occlusion is independent of instance
order, unsupported paths are counted, and bad ranges/pointers publish no image.
Retail smoke tests cover both LEV/PTR forms, nearest selection and nonempty RGB.
Run the complete checks with:

```sh
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory --output-on-failure
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized --output-on-failure
```

## Coarse terrain geometry in diagnostic scenes

`native_mesh_geometry.c` reads LEV vertex and quad-block values through validated
wire spans. It decodes signed XYZ/flags/high and low colors from each 16-byte
vertex and nine vertex indices/flags/draw-order words from each 92-byte quad.
All nine indices are checked against the vertex count, including midpoints not
used by coarse rendering. Outputs contain values, not pointer-bearing game
structures; unaligned little-endian source bytes remain immutable.

`NativeMesh_GetLowTriangle` exposes the two low-LOD triangles in quad index slots
2/0/3 and 0/1/3, following `sDrawLevelOvr1PLowLodIndices` in the resident level
renderer. This approximates curved blocks with their four corner vertices.
Draw-order masks, special face modes, high-LOD midpoints and subdivision are
not interpreted. Degenerate triangles are permitted and skipped by the rasterizer.

New scene modes accept the same arguments as the corresponding earlier modes:

```text
disc-scene-terrain ASSETS_DIR LEV_INDEX VRAM_INDICES ANCHOR COUNT OUTPUT.ppm [VIEW]
disc-scene-ptr-terrain ASSETS_DIR LEV_INDEX PTR_INDEX VRAM_INDICES ANCHOR COUNT OUTPUT.ppm [VIEW]
scene-terrain LEV_FILE PTR_FILE VRAM_FILE ANCHOR COUNT OUTPUT.ppm [VIEW]
scene-dram-terrain LEV_FILE VRAM_FILE ANCHOR COUNT OUTPUT.ppm [VIEW]
```

These modes choose COUNT nearest authored instances, plus quad blocks whose
axis-aligned bounds (computed from all nine indexed vertices) lie within 2048
world units of the anchor position. They validate all quad indices while scanning
the mesh, even outside the chosen neighborhood. The radius is currently fixed;
intersecting blocks are included whole, so the resulting view is not a clipped
sphere. An excluded instance can still serve as the positional anchor.

The inspection camera fits both coarse terrain and supported models. Terrain
uses world XYZ directly, the common Q12 view, GTE-compatible projection and
interpolated `color_hi` RGB, without terrain textures. Both geometry types share
the cleared RGB/depth target. Near model projection produces four times world
depth: this mode divides its corner depths by four before the rasterizer compares
them with terrain/far-model depth. This is an integer diagnostic approximation
with affine depth and no near clipping, not ordering-table or renderer parity.
The previous object-only scene commands retain their behavior.

With the supplied NTSC-U assets, Dingo Canyon anchor 0 with six nearby objects
selects 62 of 1250 quad blocks, draws 124 coarse terrain triangles and visits
444 triangles total. The iso view produces 19684 terrain fragment writes,
20040 total writes, and 126 skipped triangles. These are write counts, not final
visible pixel counts. Road and walls are visible with vertex shading; terrain
textures, special face handling, native visibility and gameplay remain pending.

```sh
cmake --build --preset macos-arm64-memory
build-macos-arm64-memory/ctr_native_asset_validate disc-scene-terrain assets 1 258,0 0 6 build-macos-arm64-memory/dingo-terrain.ppm iso
sips -s format png build-macos-arm64-memory/dingo-terrain.ppm --out build-macos-arm64-memory/dingo-terrain.png
open build-macos-arm64-memory/dingo-terrain.png
```

`ctr_native_mesh_geometry` checks unaligned spans, signed extrema, RGB words,
coarse topology, source immutability, invalid indices and cleared errors.
CLI tests put blue terrain between red/green models, exercise both near and far
model depths, reverse traversal, compare external/embedded PTR routes, and reject
invalid midpoint indices and truncated mesh spans without publishing an image.
Retail tests cover terrain through both disc LEV/PTR routes with nonempty output.
The suite now contains 20 tests with the disc, 19 without it.

```sh
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory --output-on-failure
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized --output-on-failure
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_mesh_geometry$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

## Textured terrain and runtime scene integration (current milestone)

The C17 ARM64 suite now contains **22 tests with the retail disc, 21 without it**.
Both the normal and AddressSanitizer/UndefinedBehaviorSanitizer suites pass.
Earlier counts and coarse-scene measurements above describe previous milestones.

`native_terrain_material.c` decodes the four face selectors, all 24 resident
scratch-init modes, UV flip/degenerate assignments, NCLIP winding and double-sided
flags, signed ordering bias, and the three TextureLayout LOD records. Tagged odd
texture targets resolve AnimTex frame arrays through the bounded pointer map.
Integer ticks include signed frame offsets and frame-skip divisors; no active
texture pointer or source byte is changed. These implement direct face triangles,
not the original subdivision or clipping pipeline.

`native_scene_camera.c` creates a Q12 view from authored angles and world position.
`native_scene_visibility.c` traverses BSP branches/leaves with bounded caller
workspace, detects visible cycles and checks leaf quad ranges. Signed-negative
child IDs disable branches, including retail 0xc000 leaves. Optional decompressed
PVS masks are MSB-first: leaf masks use node ordinals, face masks use wire blockIDs,
which differ from quad ordinals within retail groups of 32. Frustum tests are
conservative AABB tests; this module does not decompress or reproduce the game's
visibility selection algorithm.

`native_scene_render.c` consumes runtime camera and model pose/frame values and
emits triangles through an explicit callback. Terrain LOD defaults to the resident
non-cutscene thresholds H*12 and H*24. Model near depths are normalized to world
units before sharing a software depth target with terrain. `native_scene_gpu.c`
packs G3/GT3 primitives into fixed 40-byte slots and links them to the existing OT
through registered GPU tokens. Host addresses are never truncated into wire words.

The game loader captures immutable wire snapshots before legacy relocation; raw
LEV snapshots are completed when the external PTR arrives. The game model library,
driver extras and normal instance/vehicle animation frame-count queries can use
these readers. MEMPACK release, shrink, bookmark rollback and arena reuse drop
owner maps and borrowed model library references. Snapshot storage is additional
host heap memory. Borrowed views expire on release/reset/replacement; this is not
a checkpoint restore integration at that milestone (see the later migration below).

`NativeSceneConsumer_Terrain` is an actual one-player game adapter: it reads the
PushBuffer camera and decompressed PVS, preflights capacity, emits native GPU
packets and advances PrimMem. The unit target compiles this production adapter
and the production model-library consumer against minimal host aggregates, and
also executes the production MEMPACK lifecycle. It does not execute the full game.
`CTR_NATIVE_DECODED_TERRAIN` is **OFF by default**. Enabling it opts the existing
32-bit game into the experimental terrain consumer, with legacy fallback before
publication if the snapshot or output capacity is unavailable. The default
renderer continues to provide the behavior baseline.

New static diagnostic commands:

```text
disc-scene-textured ASSETS LEV VRAM_CSV ANCHOR COUNT OUTPUT.ppm [VIEW]
disc-scene-ptr-textured ASSETS LEV PTR VRAM_CSV ANCHOR COUNT OUTPUT.ppm [VIEW]
scene-textured LEV_FILE PTR_FILE VRAM_FILE ANCHOR COUNT OUTPUT.ppm [VIEW]
scene-dram-textured LEV_FILE VRAM_FILE ANCHOR COUNT OUTPUT.ppm [VIEW]
```

New runtime sequence commands insert a TICKS argument before the output prefix:

```text
disc-scene-runtime ASSETS LEV VRAM_CSV ANCHOR COUNT TICKS PREFIX [VIEW]
disc-scene-ptr-runtime ASSETS LEV PTR VRAM_CSV ANCHOR COUNT TICKS PREFIX [VIEW]
scene-runtime LEV_FILE PTR_FILE VRAM_FILE ANCHOR COUNT TICKS PREFIX [VIEW]
scene-dram-runtime LEV_FILE VRAM_FILE ANCHOR COUNT TICKS PREFIX [VIEW]
```

TICKS is 1..256. The camera fits the selected local neighborhood and then pans
32 world X units per tick. Models advance one logical frame per tick with wrap;
terrain textures use that tick. This is an inspection sequence, not gameplay
timing or controller input. All frames share the same projection and one freshly
cleared color/depth target per tick. Existing sequence files are never replaced;
choose a new prefix to rerun. A later rendering or I/O failure can leave earlier
frames on disk. Rendering remains opaque, affine, without near clipping.

```sh
cmake --preset macos-arm64-memory
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R 'terrain_material|scene_runtime' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V

build-macos-arm64-memory/ctr_native_asset_validate disc-scene-textured assets 1 258,0 0 6 build-macos-arm64-memory/dingo-textured.ppm iso
sips -s format png build-macos-arm64-memory/dingo-textured.ppm --out build-macos-arm64-memory/dingo-textured.png
build-macos-arm64-memory/ctr_native_asset_validate disc-scene-runtime assets 1 258,0 0 6 8 build-macos-arm64-memory/dingo-runtime-new iso
python3 tools/preview_sequence_html.py build-macos-arm64-memory/dingo-runtime-new build-macos-arm64-memory/dingo-runtime-new.html --fps 15
open build-macos-arm64-memory/dingo-runtime-new.html
```

Dingo's static iso test selects 62/1250 quad blocks and emits 411 terrain
triangles after 57 face culls, with 18675 terrain fragment writes. Runtime tick 0
visits 252 BSP nodes and 577 eligible quads before the local face mask, then emits
411 terrain and 200 model triangles with 19028 writes. These are diagnostics,
not parity baselines. Retail tests cover both Dingo and the external-PTR hub;
CLI tests compare embedded/external PTR images and sequence immutability,
advancing frames and refusal to overwrite. Material tests cover all selector
modes, UVs, tagged animation and malformed data; runtime tests cover camera,
PVS/blockID masks, disabled children, cycles, packet links and MEMPACK ownership.

## Host-width audit and checkpoint migration (previous milestone)

The suite now contains **23 tests with the disc, 22 without it**. Normal ARM64
and AddressSanitizer/UndefinedBehaviorSanitizer runs pass, including the new
`ctr_native_checkpoint_relocation` and the extended scene ownership tests.

The native loader uses `CtrCallbackArg` (intptr_t) when transporting callback
addresses or -1/-2 flags; the PS1 ABI remains int. Native VSync registration and
reset return the previous typed function pointer. UI creation carries tick/name
addresses at host width. Resident MPK, UI push-buffer, fruit-display and load/save
object slots now use native host pointers. The audio voice-set table contains
native pointers rather than cast int initializers. SPU addresses, retail dispatch
IDs, wire offsets and GPU tokens remain narrow integers.

Scratchpad storage is max_align_t-aligned. Native typed access checks byte range
and alignment; one-past-end traversal has a separate end helper. Host-only
workspaces hold RenderBucket's temporary object pointers and the particle
render-list state, preventing their wider fields from colliding with retail byte
slots. Their pointer slots are registered with explicit widths, and the host
workspace region participates in checkpoints. Other scratch overlays still need
semantic migration: especially collision/camera unions, Torch, skid/shadow and
recursive level-renderer overlays. Bounds checks do not prove that overlapping
views have compatible lifetimes or layouts.

`native_checkpoint_relocation.c` is the portable production relocation core.
Address ranges use fixed 16-byte records (kind/size u32, start u64). It validates
non-overlap, unique IDs and arithmetic overflow. Relocation preserves interior
and one-past-end pointers; an actual owner takes priority at an adjacent boundary.
Slots explicitly declare 4 or 8 bytes and use memcpy for unaligned access; writing
a high address into a four-byte slot fails. Image rebasing handles positive and
negative ASLR deltas, null/-1/-2 sentinels and overflow.

The game checkpoint consumer now uses this core. Typed fields derive their width
from sizeof(field), while legacy LOAD_RunPtrMap registrations remain four bytes.
Registered slot records store width and are checked for out-of-range/overlapping
locations before restore. Registration overflow makes capture fail rather than
silently dropping pointers. Address tables no longer reject addresses above 4 GB.

Checkpoint payloads are **version 5** and record host pointer width and an endian
marker. Replay headers are **version 2**, with pointer-width/endian fields in the
identity checksum and architecture-specific platform IDs. Older versions and
incompatible ABIs are rejected even when bypassing replay build identity. The
outer CTST checkpoint container stays version 1 because it stores opaque payloads.
These are same-build, same-ABI snapshots, not interchangeable PS1/native saves.

Scene owners now have a separate bounded, versioned serialization: immutable
wire bytes, PTR slot offsets, source identities, pending-map state and model
library references. Restore rebuilds maps against owned copies, rebases source
identities and validates every library reference before replacing old owners.
The game consumer stages this before writing game regions. A fixed **6 MiB**
scene region keeps replay allocation/header size stable as owners change inside
the existing 2 MiB arena; capture fails if snapshots exceed it. Each checkpoint
therefore gains that storage plus expanded pointer-slot records. Overall game
restore still has the existing partial-write behavior if a later subsystem restore
fails; scene-owner reconstruction itself is transactional.

Tests exercise high host addresses, narrowing failures, unaligned slots, ASLR
in both directions, one-past-end/adjacent ranges, malformed/overlapping metadata,
actual VSync callback return values, separate aligned workspaces, and the actual
CTST writer/reader with checksum corruption. Scene tests restore ready and pending
owners at rebased identities, restore library references and preserve existing
state on malformed snapshots. The full game checkpoint traversal and replay loop
cannot yet run on ARM64 because resident and wire structs still share 32-bit
layout contracts.

The full-game syntax audit now reports **667 active layout assertions and no
other syntax errors**. The previous voice-set initializer error is fixed; one
particle scratch assertion now checks its explicit host-width layout. It also
reports **545 pointer/integer cast warnings** requiring review. These counts are
a compiler inventory, not a count of independent bugs: some casts represent
retail IDs or tokens. The source inventory lists 101 scratchpad call sites,
including PS1 fallback branches. Neither areas 3 nor 4 are fully verified for a
playable 64-bit game; the remaining consumers and scratch overlays must be
migrated before removing the full-game guard.

Run the suites and inspect the inventory:

```sh
cmake --preset macos-arm64-memory
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R 'checkpoint_relocation|scene_runtime' -V

# Expected to fail on the remaining layout guards; preserve diagnostics.
/usr/bin/clang -fsyntax-only -ferror-limit=0 -std=c17 -DCTR_NATIVE -DCTR_INTERNAL -DCTR_NATIVE_GAME_SCENE -DCTR_NATIVE_DECODED_TERRAIN -DCTR_NATIVE_BUILD_ID='"port-check"' -DCTR_NATIVE_VERSION='"port-check"' -Iinclude -I/opt/homebrew/include main.c > build-macos-arm64-memory/stage34-syntax.log 2>&1
python3 tools/arm64_contract_audit.py --syntax-log build-macos-arm64-memory/stage34-syntax.log --output build-macos-arm64-memory/arm64-contract-audit.json
```

The syntax command uses the local Homebrew SDL include directory; adjust that
include path if SDL is installed elsewhere. The audit tool reads diagnostics and
inventories source call sites; it does not disable assertions or certify a build.

## Collision/camera workspaces and native geometry (previous milestone)

The ARM64 C17 suite now has **26 tests with the disc, 25 without it**. Normal
and ASan/UBSan builds pass. This advances the host-contract and renderer areas;
**neither area is complete for a playable ARM64 game**. The full-game pointer-width
guard stays enabled.

On 64-bit native hosts, collision contexts retain full-width object pointers and
callbacks. Quad and thread search fields have distinct storage, so scalar quad
coordinates cannot alias a callback address. Every context owns its 15-entry scrub
history; `COLL_MOVED_FindScrub` no longer assumes that any collision pointer can be
cast to a larger retail scratchpad overlay. History capacity is checked.

The collision and camera workspaces have separate keys in the aligned host
storage. Camera collision work contains a real typed collision context, followed
by camera fields. Terrain height refers explicitly to the collision hit-position
Y field; follow-camera consumers use typed accessors rather than a cast of the
retail 0x20c-byte prefix. Angle-axis access uses union members with an identical
common initial sequence. Default-instance and adventure-sign probes use their
own stack contexts. Bot, vehicle, weapon and garage callers use the new collision
workspace. PS1 and native 32-bit layouts retain their original union, offsets and
retail assertions; the ARM64 runtime types use alignment and capacity assertions.

The actual BSP search implementation now uses per-call bounded traversal storage
on native hosts. It retains child-1-before-child-0 traversal, validates child IDs
against the mesh node count, rejects cycles/shared-node trees, and supports nested
search callbacks without sharing the retail pending-child stack. It requires a
valid tree rooted at the context's mesh BSP; invalid runtime contracts trap.
This path allocates traversal storage per search; reuse/performance work remains.

Checkpoint payloads advance to **version 6** because host workspace and resident
collision layouts changed. Collision pointer visitors cover the two host contexts
and the resident collision context, rebasing object slots separately from image
callbacks. Replay headers remain version 2 and CTST containers remain version 1.
Version-5 payloads are rejected. This does not certify full game replay traversal.

The native scene renderer now clips triangles **before perspective division** at
near/far depth and all four viewport boundaries. Generated vertices interpolate
UV and RGB in Q16 and retain texture page, CLUT and ordering bias. A triangle can
produce at most seven triangles. Fully inside triangles retain their existing GTE
projection when subdivision is disabled. Model view coordinates are normalized
for the near path's factor of four before clipping. Front-face tests run on the
generated terrain triangles; preflight and GPU/raster publication share this path.

`NativeSceneCamera.subdivisionDepth` accepts **0..3**. Zero keeps direct geometry;
1..3 perform bounded uniform view-space midpoint subdivision, then clipping, up
to 64 leaves per source triangle. This is an explicit diagnostic option, **not the
retail subdivision dispatch/threshold implementation**. The CLI accepts a trailing
`subdiv=0..3` for runtime scene modes. The experimental game adapter defaults to zero.

`native_vertex_animation.c` decodes SCVert/WaterVert records through bounded PTR
resolution. It validates vertex membership, full 28-sample water tables, default
visibility masks and the environment layout. Water color interpolation and
scenery position/color animation run on a separate vertex copy, without writing
to immutable assets or global GTE state. Up to four LSB-first masks can be ORed;
scene rendering currently uses the level default. Scenery uses game timer<<7;
water uses timer/8 modulo 28 with interpolation within each eight-tick interval.
The scene terrain wrapper allocates a temporary vertex buffer only when an active
animation list exists. Water environment/reflection rendering remains separate.

Validation includes:

- Production BSP traversal with nested callbacks and original leaf order;
  collision/camera isolation, full-width fields, scrub-history saturation and
  checkpoint pointer rebasing, including camera state preservation.
- Near/far/viewport intersections, winding, UV/RGB interpolation, material metadata,
  outside/degenerate triangles, scaled model transforms and invalid bounds.
- Subdivision depths 0..3, conserved projected area on a planar fixture, preflight
  agreement, failed sinks and invalid depth rejection.
- Differential comparison against the existing `AnimateWater.c` and production
  GTE core: 224 water ticks and 4096 scenery ticks, with immutable source/GTE
  checks, output bounds and visibility mask union.
- Retail validation of Dingo Canyon's **165 water records** and Crash Cove's
  **3291 water records**, runtime images of both tracks and the adventure hub,
  plus subdivided Dingo sequences and overwrite rejection.

The complete-game syntax audit reports **611 remaining layout assertions, 545
pointer/integer-cast warnings, 86 scratchpad call sites, and no other syntax errors**.
The change from 667 assertions reflects explicit migration of collision/camera
runtime storage; binary asset layouts elsewhere remain guarded. These counts are
an inventory, not a percentage or a count of independent defects.

Reproduce the checks from the repository root:

```sh
cmake --preset macos-arm64-memory
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R 'collision_work|scene_geometry|vertex_animation|scene_runtime|asset_retail' -V

# Use a fresh prefix: existing sequence frames are rejected.
build-macos-arm64-memory/ctr_native_asset_validate disc-scene-runtime assets 1 258,0 0 6 8 build-macos-arm64-memory/dingo-subdivision-check iso subdiv=1
python3 tools/preview_sequence_html.py build-macos-arm64-memory/dingo-subdivision-check build-macos-arm64-memory/dingo-subdivision-check.html --fps 15
sips -s format png build-macos-arm64-memory/dingo-subdivision-check-000000.ppm --out build-macos-arm64-memory/dingo-subdivision-check-000000.png

# Crash Cove has a larger animated water list; this is a diagnostic scene.
build-macos-arm64-memory/ctr_native_asset_validate disc-lev assets 25
build-macos-arm64-memory/ctr_native_asset_validate disc-scene-runtime assets 25 258,24 0 6 8 build-macos-arm64-memory/cove-water-check iso

# Expected to fail on the remaining layout guards, not a full-game build.
/usr/bin/clang -fsyntax-only -ferror-limit=0 -std=c17 -DCTR_NATIVE -DCTR_INTERNAL -DCTR_NATIVE_GAME_SCENE -DCTR_NATIVE_DECODED_TERRAIN -DCTR_NATIVE_BUILD_ID='"port-check"' -DCTR_NATIVE_VERSION='"port-check"' -Iinclude -I/opt/homebrew/include main.c > build-macos-arm64-memory/stage12-syntax.log 2>&1
python3 tools/arm64_contract_audit.py --syntax-log build-macos-arm64-memory/stage12-syntax.log --output build-macos-arm64-memory/arm64-contract-audit.json
```

Remaining in the first two requested areas: other scratch overlays and recursive rendering, resident camera/vehicle/level
contracts, direct legacy asset consumers, complete model/gameplay integration,
retail subdivision, environment/reflection/special materials, multiplayer/cutscene
camera paths and complete-game behavioral/visual validation. Passing module tests
does not close these remaining parts.

## Effect workspaces and material blending (current milestone)

The C17 ARM64 suite has **27 tests with the disc, 26 without it**, passing both
normal and ASan/UBSan builds. CMake now uses the correct `CTR_ENABLE_SANITIZERS`
option for collision, scene geometry, vertex animation and effect workspace
executables. The first three targets in the previous milestone had accidentally
used an undefined option, so that milestone's sanitized preset did not instrument
those three targets. Compile-command inspection now confirms instrumentation,
and all 27 checks pass after rebuilding.

On ARM64, Torch, skid and shadow work use separate aligned host slots. Torch's
retail saved-particle address slot is unused on native; particle positions,
radii, colors and OT bias come from typed resident fields. Scalar Torch layout
assertions remain active. Skid origin loads address `scratch->origin`, whose
location changes after the full-width PushBuffer pointer. The fourth skid segment
passes its actual coordinate pointer instead of round-tripping it through s32.
The production skid projection helper is compiled independently for wraparound
regression checks. Native 32-bit and PS1 retain retail workspace layouts.

Shadow keeps its scalar 1 KiB payload but moves object addresses to nine typed
driver and instance slots, including the sentinel entry. Checked helpers map each
retail slot address to its typed index; no host pointer is written into four-byte
scalar gaps. Tests cover all nine entries, scalar isolation, the original retail
scratchpad remaining untouched, storage capture/reset/restore and pointer rebasing.
They do not execute the complete Torch or vehicle-shadow render loops. Their
resident PushBuffer/Driver/Instance contracts still require the remaining migration.

Checkpoint payloads advance to **version 7** for the expanded host storage.
The effect visitor traverses the skid PushBuffer and both shadow pointer arrays;
Torch contains no saved native pointer. Version-6 payloads are rejected. Replay
headers remain version 2 and CTST containers remain version 1. Complete-game
checkpoint execution is still blocked by the resident-layout guard.

`native_material.h` shares the ordinary GT3 material policy between GPU packets
and the software rasterizer: page blend bits 3 mean opaque by default, matching
the existing RenderBucket writers. `NativeModelTriangle.textureBlend` can explicitly
force opaque or semi-transparent behavior, including quarter-source blending.
Clipping and subdivision retain the policy. The rasterizer blends sampled STP
texels only, keeps non-STP texels opaque, and discards word-zero texels. Average,
add, subtract and quarter-source factors use RGB8 integer truncation/saturation.
Blended fragments test against opaque depth but do not replace it; primitive
submission order matters. This diagnostic path does not implement RGB5 GPU
quantization, mask bits, dithering, texture windows, OT sorting or reflections.

Tests exercise all four factors, overrides and GPU codes, mixed STP/non-STP
samples in one primitive, repeated blends, foreground occlusion, saturation,
transparent zero and transactional texture errors after an early blend candidate.
The CLI reports blended fragment counts. An eight-frame Dingo sequence with
`subdiv=1` remains valid, but the selected inspection region produces **zero
blended fragments**; real translucent material parity is not established by it.

The stage-13 syntax inventory was **602 layout assertions, 544 pointer/integer-cast
warnings and 86 scratchpad call sites**, with no other syntax errors. The scratch
count includes PS1/native32 fallback branches. These are diagnostic counts, not
a completion percentage; the full-game build remains guarded.

Run from the repository root:

```sh
cmake --preset macos-arm64-memory
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R 'effect_work|raster|scene_geometry|scene_runtime|checkpoint_relocation' -V

# Use a fresh prefix; existing frames are rejected.
build-macos-arm64-memory/ctr_native_asset_validate disc-scene-runtime assets 1 258,0 0 6 8 build-macos-arm64-memory/effects-materials-check iso subdiv=1
python3 tools/preview_sequence_html.py build-macos-arm64-memory/effects-materials-check build-macos-arm64-memory/effects-materials-check.html --fps 15
sips -s format png build-macos-arm64-memory/effects-materials-check-000000.ppm --out build-macos-arm64-memory/effects-materials-check-000000.png

# Expected layout-guard failure; this is an inventory, not a successful game build.
/usr/bin/clang -fsyntax-only -ferror-limit=0 -std=c17 -DCTR_NATIVE -DCTR_INTERNAL -DCTR_NATIVE_GAME_SCENE -DCTR_NATIVE_DECODED_TERRAIN -DCTR_NATIVE_BUILD_ID='"port-check"' -DCTR_NATIVE_VERSION='"port-check"' -Iinclude -I/opt/homebrew/include main.c > build-macos-arm64-memory/stage13-syntax.log 2>&1
python3 tools/arm64_contract_audit.py --syntax-log build-macos-arm64-memory/stage13-syntax.log --output build-macos-arm64-memory/arm64-contract-audit.json
```

Still open in the first two areas: resident Camera/Vehicle/Level and direct asset
consumers, recursive rendering workspaces, retail subdivision dispatch,
reflection/environment passes, special model consumers, multiplayer/cutscene
cameras and complete-game behavioral/visual checks. The two areas are not closed
by this milestone.

## Remaining game work

The complete game still has a CMake pointer-width guard and a corresponding
assertion in `game_layouts.h`. Retail layout assertions remain enabled. The
memory-only preset is a migration/test target, not a switch that permits an
unsafe 64-bit game build.

The requested terrain/material and reader-to-consumer areas now have an executable
ARM64 path, but are **not fully closed for the game**. Remaining in these areas:

- Retail subdivision dispatch/threshold parity, reflection/environment materials,
  special model paths and complete-game vertex animation/visibility parity.
  Native clipping, diagnostic midpoint subdivision and water/scenery animation
  now execute through the scene renderer.
- Replace the remaining legacy model RenderBucket/gameplay consumers and add
  multiplayer/cutscene camera paths, then validate in the complete game.

These depend on continued separation of binary asset layouts (four-byte addresses
and offsets) from runtime objects (host pointers). In particular:

1. Continue replacing legacy MPK/LEV callback publications and direct pointer
   consumers. Immutable loader ownership is connected, but legacy publications
   coexist until their callers are migrated.
2. Validate material/visibility and camera/renderer parity in the complete game,
   including remaining model and gameplay consumers and the LNG integration.
3. Resolve the remaining pointer/integer audit findings and migrate other scratch
   overlays. Collision/camera workspaces are migrated, but their resident game
   and legacy asset contracts still need integration and behavioral validation.
4. Verify the migrated checkpoint traversal against all remaining host objects
   and scratch overlays before enabling full ARM64 replay/savestate support.
5. Enable the full macOS ARM64 target only after those contracts are satisfied,
   then validate OpenGL context/shaders, audio and gameplay with retail assets.

The PS1 allocator path retains its original four-byte alignment and pointer
arithmetic. The existing native 32-bit game remains the baseline for behavior;
this milestone does not establish full game or PS1 binary parity.

The current full-game ARM64 syntax audit encounters **382 active binary-layout
assertions, 411 pointer/integer-cast warnings and no other syntax errors**.
The source inventory retains 86 scratchpad call sites, including fallback paths.
This is not a successful full-game build. The CMake pointer-width guard and
remaining retail layout assertions stay enabled.


## Stage 14: resident camera, vehicle and asset record contracts

`CameraDC` and `PushBuffer` now compile as actual host64 game declarations.
Pointer-free camera/viewport/matrix prefix checks remain active; pointer-dependent
retail offsets remain checked on the 32-bit path, with host pointer width and
alignment checks on ARM64. `PushBuffer_Core.c` contains the production init,
matrix and frustum routines, included by `PushBuffer.c` and exercised with the
production native GTE register bridge. Matrix stores use byte-safe helpers;
negative normal normalization avoids signed-left-shift UB and the corner loop
no longer forms a pointer before its array. Native initialization writes the
whole camera ID. CAM fly-in, blasted-height and end-of-race path accesses use
typed fields/byte arithmetic. Draw-environment cursor increments retain host
width. Shadow viewport selection, matrices and per-player flags use typed fields;
its OT pointer lives beside the scalar scratch payload and participates in
checkpoint rebasing.

`Driver` and `BotData` retain fixed-width scalar state and full-width callbacks,
object references and list links. Player/bot initialization clears through the
actual `ghostTape` offset, preserving the separately initialized ghost extension.
The host large-stack pool includes the whole Driver and intrusive prefix,
rounds to host alignment and leaves room for PROC's strict capacity check.
`Instance` uses a C17 flexible per-player array. IDPP command/color/delta and OT
address transports use `CtrRuntimeAddress`; direct RenderBucket range parameters
and returns retain that width while draw-handler IDs stay 32-bit tokens.
SpawnType1 and IconGroup trailing pointer arrays are aligned flexible members.

Resident `Instance` and `InstDef` have a shared full-width peer slot after the
intrusive list prefix. Linking initializes both typed references and peer slots;
pooled birth clears the instance peer. `LevInstDef` traversal reads the peer by
bytes, preserving the existing toggle-on-each-reference behavior of shared PVS
lists rather than deduplicating them. Native level instance initialization copies
named fields instead of copying a wire prefix across changed host offsets.

`native_resident.h/.c` defines explicit fixed-size wire records and bounded,
transactional decoders for Level, Model, ModelHeader, ModelAnim, InstDef, mesh,
QuadBlock, BSP branches/leaves and hitboxes, WaterVert, SCVert, PVS, NavHeader,
Skybox and both SpawnType2 interpretations. Scalars are read little-endian.
Caller-owned typed bindings translate interior array references by element index
with distinct wire/host strides, reject unaligned/ambiguous bindings and never
patch the asset. Listed target zero still refers to origin; an unlisted zero slot
is NULL. Tagged terrain textures resolve to resident IconGroup4/AnimTex objects
and retain their tag. Only opaque byte spans and word streams may borrow asset
storage; mixed pointer-bearing records require resident bindings.

These are record/header decoders, **not a complete resident graph loader**.
Bindings must describe real allocations. The caller must materialize pointer
arrays, inline animation/navigation/texture payloads and nested objects, validate
complete streams/terminated lists and publish only a finished graph. The retail
validator now compares resident Model/InstDef conversion against the existing
bounded views for every level definition, including Dingo Canyon, Coco Park and
the separate-PTR menu. Its model header allocations are placeholders for this
comparison and are never published or rendered as resident objects.

Checkpoint payload version is **8** because the resident ABI and shadow pointer
storage changed. Instance peer and shadow OT slots are visited. Full game
checkpoint traversal, including future resident asset owners/definition peers,
remains unverified and must be completed with graph-loader integration.

Validation: **29/29 CTest entries pass normally and with ASan/UBSan** (28 without
the optional disc). The two new tests check 1–4 viewport layouts, 2,000 rotations
through production matrix/frustum/GTE routines, intrusive/peer/PVS links,
per-player data, Driver base initialization and pool capacity, aligned trailing
pointer arrays, little-endian record conversion, distinct array strides,
geometry references, tagged textures, immutable bytes and atomic rejection.
The optional retail test requires the resident conversion diagnostic as well as
the existing rendering checks. The syntax inventory above remains a guard-failure
inventory, not evidence of a successful game build.

```sh
cmake --preset macos-arm64-memory
cmake --build --preset macos-arm64-memory
ctest --preset macos-arm64-memory
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized -R 'resident|effect_work|asset_retail' -V

# Real definitions, without generating or overwriting images.
build-macos-arm64-memory/ctr_native_asset_validate disc-lev assets 1
build-macos-arm64-memory/ctr_native_asset_validate disc-lev assets 25
build-macos-arm64-memory/ctr_native_asset_validate disc-lev-ptr assets 201 202

# Expected full-game layout failure; other_error must remain absent/zero.
/usr/bin/clang -fsyntax-only -ferror-limit=0 -std=c17 -DCTR_NATIVE -DCTR_INTERNAL -DCTR_NATIVE_GAME_SCENE -DCTR_NATIVE_DECODED_TERRAIN -DCTR_NATIVE_BUILD_ID='"port-check"' -DCTR_NATIVE_VERSION='"port-check"' -Iinclude -I/opt/homebrew/include main.c > build-macos-arm64-memory/stage14-syntax.log 2>&1
python3 tools/arm64_contract_audit.py --syntax-log build-macos-arm64-memory/stage14-syntax.log --output build-macos-arm64-memory/arm64-contract-audit.json
```

Next work, in dependency order:

1. Migrate recursive render scratch, retail subdivision/reflection and special
   model/material consumers, including texture-animation slot transports.
2. Finish the resident asset graph loader/publications, remaining globals,
   overlays and direct offset consumers (including BOTS), plus checkpoint owner
   and pointer traversal. Existing raw asset callbacks must not cast wire records
   to the enlarged resident declarations.
3. Enable/build the full ARM64 game once those contracts and pointer transport
   checks are satisfied; the CMake and game-layout guards remain active until then.
4. Validate real menu/race behavior, multiplayer/cutscene cameras, GPU rendering,
   input/controllers, audio, replay/save-state round trips and memory lifetimes.
