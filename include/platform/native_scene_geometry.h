#ifndef PLATFORM_NATIVE_SCENE_GEOMETRY_H
#define PLATFORM_NATIVE_SCENE_GEOMETRY_H
#include <platform/native_scene_camera.h>
enum { NATIVE_SCENE_CLIP_MAX_TRIANGLES=7 };
struct NativeSceneClipVertex {
    s32 view[3];
    s32 uv[2], rgb[3]; // Q16 attributes, interpolated before integer GPU packing.
};
// Clips a convex triangle against near/far and viewport planes before dividing
// by depth. At most nine vertices/seven triangles. Pure value operation, no heap.
// Input view coordinates must be within +/- 2^24; UV/RGB within 0..255 Q16.
// Generated corners retain material/ordering metadata. NOT retail GTE parity.
typedef enum NativeAssetResult (*NativeSceneGeometrySink)(void *user,const struct NativeDrawTriangle *triangle);
// Bounded view-space midpoint subdivision (0..3 => 1..64 leaves), followed by
// clipping. NULL sink counts exactly the same triangles. Errors clear count;
// a sink failure may leave earlier triangles published.
enum NativeAssetResult NativeSceneGeometry_Subdivide(const struct NativeSceneCamera *camera,
    const struct NativeModelTriangle *material,const struct NativeSceneClipVertex vertices[3],s16 orderingBias,
    u32 depth,NativeSceneGeometrySink sink,void *user,size_t *count);
int NativeSceneGeometry_NeedsClip(const struct NativeSceneCamera *camera,const struct NativeSceneClipVertex vertices[3]);
enum NativeAssetResult NativeSceneGeometry_Clip(const struct NativeSceneCamera *camera,
    const struct NativeModelTriangle *material,const struct NativeSceneClipVertex vertices[3],s16 orderingBias,
    struct NativeDrawTriangle output[NATIVE_SCENE_CLIP_MAX_TRIANGLES],size_t *count);
enum NativeAssetResult NativeSceneGeometry_Transform(const struct NativeProjectionConfig *projection,
    const struct NativePackedModelVertex *vertex,unsigned depthScale,s32 view[3]);
void NativeSceneGeometry_Attributes(const struct NativeModelTriangle *material,unsigned corner,struct NativeSceneClipVertex *vertex);
#endif
