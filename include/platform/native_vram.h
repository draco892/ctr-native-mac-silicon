#ifndef PLATFORM_NATIVE_VRAM_H
#define PLATFORM_NATIVE_VRAM_H
#include <platform/native_asset_readers.h>
enum { NATIVE_VRAM_WIDTH=1024, NATIVE_VRAM_HEIGHT=512, NATIVE_VRAM_BYTES=1024*512*2 };
struct NativeVramView { u8 *bytes; };
struct NativeVramLoadInfo { size_t rectangles, words; };
struct NativeTexturePixel { u16 word; u8 r,g,b,a,stp; };
// Caller owns at least 1 MiB, stored LE words, no alignment requirement. Bind
// clears output only, never pixel storage. Source load bytes must not overlap
// VRAM storage. All views require unchanged successful Bind, valid storage.
enum NativeAssetResult NativeVram_Bind(void *bytes,size_t size,struct NativeVramView *out);
// Retail VramHeader (20 bytes, rect at 12), single or 0x20 chunk list. Complete
// preflight before any write; ordered rectangles may overwrite prior pixels.
// Header's first 12 bytes are opaque, as in LOAD_VramFileCallback. Chunk lengths
// use size&~3; list requires a bounded zero sentinel. Ignores trailing bytes.
enum NativeAssetResult NativeVram_Load(const struct NativeVramView *vram,const void *asset,
    size_t bytes,struct NativeVramLoadInfo *out);
// Integer byte UVs; checks physical VRAM address rather than wrapping/clamping.
// TPAGE modes 0/1/2 = 4/8/16 bit; mode3 uses native renderer's 16-bit fallback.
// RGB8 uses bit replication. Only word==0 is transparent; STP remains separate,
// not alpha=128. No texture window, modulation, filtering or blending.
enum NativeAssetResult NativeVram_Sample(const struct NativeVramView *vram,u16 tpage,u16 clut,
    u8 u,u8 v,struct NativeTexturePixel *out);
#endif
