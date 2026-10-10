#ifndef PLATFORM_NATIVE_SCENE_RENDER_H
#define PLATFORM_NATIVE_SCENE_RENDER_H
#include <platform/native_scene_visibility.h>
typedef enum NativeAssetResult (*NativeSceneTriangleSink)(void *user,const struct NativeDrawTriangle *triangle);
struct NativeSceneRenderStats { u32 visibleNodes,visibleQuads; size_t triangles,culled,degenerate,flagged,clipped,subdivided; };
struct NativeRuntimeInstance {
    struct NativeModelView model;
    struct NativeModelMatrix rotation;
    s32 position[3]; s16 scale[3]; u32 flags;
    u32 header,animation,frame;
};
enum NativeAssetResult NativeRuntimeInstance_Init(const struct NativeInstanceDefView *definition,struct NativeRuntimeInstance *out);
// Caller supplies runtime pose/header/animation/frame. Asset model refs borrow
// their owner; callbacks only receive values. No callback integer or scratchpad.
enum NativeAssetResult NativeSceneRender_Model(const struct NativeRuntimeInstance *instance,
    const struct NativeSceneCamera *camera,const struct NativeModelDrawWorkspace *workspace,
    const struct NativeVramView *vram,NativeSceneTriangleSink sink,void *user,struct NativeSceneRenderStats *out);
// lod=UINT32_MAX selects near/middle/far at H*12/H*24, else explicit 0..2.
// Water/scenery records animate into a temporary vertex copy when present.
// Camera subdivisionDepth optionally subdivides both models and terrain.
// Native clipping interpolates UV/RGB at near/far and viewport boundaries.
// Fully inside triangles retain their existing GTE projection.
// NULL sink preflights all selected records/projection and counts primitives.
// A sink failure may leave already-emitted primitives; output stats clear.
// Face masks refer to a native MSB-first bitset indexed by wire blockID (retail
// reverses IDs within 32-block groups). Masks must cover every accessed ID.
enum NativeAssetResult NativeSceneRender_Terrain(const struct NativeLevelView *level,const struct NativeMeshView *mesh,
    const struct NativeSceneCamera *camera,const void *leafPvs,size_t leafPvsBytes,const void *facePvs,size_t facePvsBytes,
    u32 tick,u32 lod,const struct NativeVisibilityWorkspace *workspace,NativeSceneTriangleSink sink,void *user,struct NativeSceneRenderStats *out);
#endif
