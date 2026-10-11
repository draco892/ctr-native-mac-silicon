#ifndef PLATFORM_NATIVE_RASTER_H
#define PLATFORM_NATIVE_RASTER_H
#include <platform/native_model_draw.h>
struct NativeRasterView { u8 *rgb; u32 *depth; u32 width,height; };
struct NativeRasterStats { size_t covered,written,transparent,blended; int skipped; };
// Caller-owned disjoint RGB/depth arrays, up to 4096x4096. Depth is an aligned
// native u32 array. Use only unchanged views returned by successful Bind. Bind preserves pixels. Clear sets depth to UINT32_MAX.
enum NativeAssetResult NativeRaster_Bind(void *rgb,size_t rgbBytes,u32 *depth,size_t depthCount,
    u32 width,u32 height,struct NativeRasterView *out);
enum NativeAssetResult NativeRaster_Clear(const struct NativeRasterView *target,const u8 background[3]);
// Diagnostic two-sided rasterizer, pixel-center/top-left coverage,
// affine UV/RGB/integer depth, nearest depth wins (equal depth keeps old pixel).
// Textured fragments modulate by RGB/128, clamp; word-zero texels write neither
// color nor depth. Semi-transparent materials blend only sampled STP texels;
// non-STP texels and opaque materials write color/depth normally. Blended texels
// obey the opaque depth test and preserve depth. RGB8 average/add/subtract/
// quarter-source factors match the native renderer, with integer truncation.
// Primitive submission order therefore matters. Rejects zero depth or GTE divide/depth
// overflow; does not near-clip crossing triangles. Scissors to the target.
// Preflights surviving texture reads before any write. Error clears stats and
// preserves target. All target/input/VRAM buffers must be disjoint and immutable
// during a call (apart from successful target writes). No heap or global state.
// Not retail GPU parity: no lighting, texture windows, RGB5 quantization, dithering,
// ordering tables, native visibility policy or runtime framebuffer feedback.
enum NativeAssetResult NativeRaster_Draw(const struct NativeRasterView *target,
    const struct NativeDrawTriangle *triangle,const struct NativeVramView *vram,
    struct NativeRasterStats *out);
#endif
