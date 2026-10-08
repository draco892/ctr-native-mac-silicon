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
`ctr_native_model_transform_tests` and `ctr_native_model_matrix_tests` under
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
rotation/view/instance inputs. Translation, perspective, VRAM pixel sampling
and rendering/gameplay remain unverified in this path. Inputs are opened read-only; no asset extraction is
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
VRAM pixel sampling, translation/projection and renderer migration remain
required before visual playback or an ARM64 game build.

## Bounded model vertex decoding

`native_model_vertices.c` decodes raw XYZ byte triples and compressed model
streams without patching assets or casting bytes to host structures. Output
coordinates retain the encoded byte representation, before frame origin/scale,
packed-coordinate transforms or interpolation. Packing promotes compressed
bytes as signed and raw bytes as unsigned; these are distinct contracts. Stream words are
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
With the supplied disc present, both presets run 14 tests; without it, 13 run.

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
the vertex decoder: compressed axes are sign-extended; raw axes are unsigned.
This distinction matters both for negative coordinates and packed OR carries.

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
halves, negative origins, signed compressed versus unsigned raw coordinates,
and 10000 randomized comparisons with a wide-integer reference calculation.
Synthetic assets exercise static/direct/interpolated bulk packing, odd/even
logical endpoints, clamping, missing next scratch, capacity, unaligned buffers,
immutability, Rebind, compressed stream integration and a malformed next frame.
Both complete preset suites pass 14 tests with the supplied disc present.

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
ASan/UBSan preset suites pass 14 tests with the supplied disc present.

```sh
cmake --preset macos-arm64-memory-sanitized
cmake --build --preset macos-arm64-memory-sanitized
ctest --preset macos-arm64-memory-sanitized
# Focused matrix and real-disc checks:
ctest --preset macos-arm64-memory-sanitized -R '^ctr_native_model_matrix$' -V
ctest --preset macos-arm64-memory-sanitized -L retail -V
```

## Remaining game work

The complete game still has a CMake pointer-width guard and a corresponding
assertion in `game_layouts.h`. Retail layout assertions remain enabled. The
memory-only preset is a migration/test target, not a switch that permits an
unsafe 64-bit game build.

Next, separate binary asset layouts (four-byte addresses and offsets) from
runtime objects (host pointers). In particular:

1. Replace persistent MPK/LEV callback publications and direct host-pointer
   consumers with decoded wire views and explicit ownership. The native DRAM
   callback now retains actual payload lengths and validates embedded maps.
2. Port camera translation/perspective and decode VRAM pixels; connect the new matrix, triangle,
   vertex, library, animation and instance-definition readers to resident
   gameplay and rendering consumers. Validate these and the LNG integration with retail assets.
3. Audit resident globals, callbacks carried in integers and fixed scratchpad
   offsets; host structures must not overlap retail-sized scratchpad slots.
4. Port checkpoint pointer slots and address tables before enabling 64-bit
   replay/savestate support.
5. Enable the full macOS ARM64 target only after those contracts are satisfied,
   then validate OpenGL context/shaders, audio and gameplay with retail assets.

The PS1 allocator path retains its original four-byte alignment and pointer
arithmetic. The existing native 32-bit game remains the baseline for behavior;
this milestone does not establish full game or PS1 binary parity.
