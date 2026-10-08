#ifndef PLATFORM_NATIVE_MODEL_VERTICES_H
#define PLATFORM_NATIVE_MODEL_VERTICES_H

#include <platform/native_model_animation.h>

// Raw unsigned byte coordinates, matching RenderBucketVertex before origin,
// scale, packed-coordinate transforms or interpolation are applied.
struct NativeModelVertex { u8 x, y, z; };

struct NativeVertexDecoder
{
	const u8 *data;
	size_t bytes, bitPosition, vertexIndex;
	s32 x, y, z;
	int compressed;
};

// Compressed words are LE, bits MSB-first within each word. Component order
// is X,Z,Y; 8-bit fields reset, shorter fields accumulate temporal base+delta
// and wrap through a signed byte. A failed Next leaves state unchanged and
// clears its output. The source and initialized state must remain unchanged
// except through Next. Raw input consists of XYZ triples.
enum NativeAssetResult NativeVertexDecoder_Init(const void *data, size_t bytes,
    int compressed, struct NativeVertexDecoder *out);
enum NativeAssetResult NativeVertexDecoder_Next(struct NativeVertexDecoder *decoder,
    u32 temporalWord, struct NativeModelVertex *out);

// Bounded command scan: prefix is color-cache count, sentinel is 0xffffffff.
// Color-only commands and flag-4 cached vertices consume no new vertex data.
// This reports fresh vertices, not faces or unique rendered positions.
enum NativeAssetResult NativeModel_GetVertexCount(const struct NativeModelView *model,
    u32 headerIndex, u32 *out);
// Caller owns output storage. On failure count=0; output records are scratch
// until success and may contain a partially decoded prefix. Counts come from
// the header's commands. Delta words and vertex streams are independently
// bounded; static compressed lengths are calculated from the declared widths.
enum NativeAssetResult NativeModel_DecodeStaticVertices(const struct NativeModelView *model,
    u32 headerIndex, struct NativeModelVertex *vertices, size_t capacity, u32 *count);
enum NativeAssetResult NativeModel_DecodeAnimationVertices(const struct NativeModelView *model,
    u32 headerIndex, u32 animationIndex, u32 storedFrameIndex,
    struct NativeModelVertex *vertices, size_t capacity, u32 *count);

#endif
