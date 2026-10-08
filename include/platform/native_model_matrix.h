#ifndef PLATFORM_NATIVE_MODEL_MATRIX_H
#define PLATFORM_NATIVE_MODEL_MATRIX_H
#include <platform/native_model_transform.h>

// Pointer-free runtime values, not a MATRIX wire overlay. Q12 one = 4096.
struct NativeModelMatrix { s16 m[3][3]; };
struct NativeMatrixVector { s32 mac[3]; s16 ir[3]; u8 saturated; };
// Caller supplies rotation, scale and view matrices in the renderer's axis
// order. Inputs and outputs must not overlap. Clears outputs on errors.
// Build follows the normal RenderBucket_BuildM3x3 scale path, including the
// viewDepth<4096 threshold and optional PIXEL_LOD low-word multiplication.
// Rotation and compositions floor Q12 dot products and saturate signed IR.
// No angles/trig, translation, reflection/split path, perspective or GTE flags.
enum NativeAssetResult NativeModelMatrix_Build(const struct NativeModelMatrix *rotation,
    const s16 modelScale[3], const s16 instanceScale[3], s32 viewDepth,
    int pixelLod, struct NativeModelMatrix *out);
enum NativeAssetResult NativeModelMatrix_Compose(const struct NativeModelMatrix *left,
    const struct NativeModelMatrix *right, struct NativeModelMatrix *out);
// Applies a Q12 linear matrix to the signed GTE input halves of a packed vertex.
// mac retains the shifted dot product; ir clamps to [-32768,32767]; saturated
// bits 0..2 identify clamped axes. This diagnostic is not the hardware FLAG.
enum NativeAssetResult NativeModelMatrix_Apply(const struct NativeModelMatrix *matrix,
    const struct NativePackedModelVertex *vertex, struct NativeMatrixVector *out);
#endif
