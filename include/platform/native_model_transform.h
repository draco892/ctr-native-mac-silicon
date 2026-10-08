#ifndef PLATFORM_NATIVE_MODEL_TRANSFORM_H
#define PLATFORM_NATIVE_MODEL_TRANSFORM_H

#include <platform/native_model_vertices.h>

// Bit patterns for the GTE input registers, before model/instance/view matrices.
// Position components are signed low16(XY), high16(XY), low16(Z), in that order.
struct NativePackedModelVertex { u32 xy, z; };

// Raw and compressed stored bytes are both unsigned, as in RenderBucketVertex.
// compressed must be 0/1; it describes the source, not packing signedness.
// Optional next frame AND vertex select the
// retail halfway pack. Uses unsigned shifts/adds, preserving packed carries.
// Frame scalar positions suffice; no source bytes are accessed by this helper.
enum NativeAssetResult NativeModel_PackVertex(const struct NativeFrameView *frame,
    const struct NativeModelVertex *vertex, int compressed,
    const struct NativeFrameView *nextFrame, const struct NativeModelVertex *nextVertex,
    struct NativePackedModelVertex *out);
enum NativeAssetResult NativeModel_UnpackPosition(const struct NativePackedModelVertex *vertex,
    s16 out[3]);

// Caller-owned, mutually non-overlapping scratch/output arrays, each capacity
// elements. Assets/maps stay immutable; views obey the animation reader lifetime.
// count=0 on error; scratch/output may contain a prefix until success. No heap.
// Animation requests clamp through SelectFrame; only odd half-rate frames need
// nextScratch. No playback timing, matrices, scale, lighting or projection.
enum NativeAssetResult NativeModel_PackStaticVertices(const struct NativeModelView *model,
    u32 headerIndex, struct NativeModelVertex *scratch, struct NativePackedModelVertex *out,
    size_t capacity, u32 *count);
enum NativeAssetResult NativeModel_PackAnimationVertices(const struct NativeModelView *model,
    u32 headerIndex, u32 animationIndex, u32 logicalIndex,
    struct NativeModelVertex *currentScratch, struct NativeModelVertex *nextScratch,
    struct NativePackedModelVertex *out, size_t capacity, u32 *count);

#endif
