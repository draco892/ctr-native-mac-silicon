#ifndef PLATFORM_NATIVE_SCENE_VISIBILITY_H
#define PLATFORM_NATIVE_SCENE_VISIBILITY_H
#include <platform/native_scene_camera.h>
struct NativeSceneBsp { u16 flags; s16 id,minimum[3],maximum[3]; u16 children[2]; u32 firstQuad,quadCount; };
struct NativeVisibilityWorkspace { u32 *stack; u8 *states,*quads; size_t stackCapacity,stateCapacity,quadCapacity; };
struct NativeVisibilityResult { const u8 *quadMask; u32 visibleNodes,visibleQuads; };
enum NativeAssetResult NativeSceneBsp_Get(const struct NativeLevelView *level,const struct NativeMeshView *mesh,u32 index,struct NativeSceneBsp *out);
// Walk root 0, branch child-ID mask 0x3fff, signed-negative IDs skipped, MSB-first native PVS
// words. NULL PVS means all nodes eligible. A frustum rejects whole node boxes.
// Scratch capacities: stack >= 2*bspCount+1, states >= bspCount, quads >= quadCount.
// Scratch is unpublished until success; result borrows its mask until next call.
// Bounded DFS rejects cycles; shared already-completed subtrees are allowed.
// Zero-node meshes use per-quad bounds. No PVS decompression is inferred here.
enum NativeAssetResult NativeSceneVisibility_Select(const struct NativeLevelView *level,const struct NativeMeshView *mesh,
    const struct NativeSceneCamera *camera,const void *pvs,size_t pvsBytes,const struct NativeVisibilityWorkspace *workspace,
    struct NativeVisibilityResult *out);
#endif
