#ifndef PLATFORM_NATIVE_SCENE_CAMERA_H
#define PLATFORM_NATIVE_SCENE_CAMERA_H
#include <platform/native_instance_transform.h>
#include <platform/native_terrain_material.h>
#include <platform/native_model_draw.h>
struct NativeSceneCamera { struct NativeInstanceCamera transform; u32 width,height; s32 nearDepth,farDepth; };
// Proper runtime camera orientation: Y*X*Z authored rotation, transposed view.
// No fitting, heap or game-layout casts. Caller supplies world position/angles.
enum NativeAssetResult NativeSceneCamera_Init(const s32 position[3],const s16 rotation[3],
    u32 width,u32 height,u16 h,s32 nearDepth,s32 farDepth,struct NativeSceneCamera *out);
int NativeSceneCamera_IsValid(const struct NativeSceneCamera *camera);
// Conservative homogeneous frustum test of all 8 corners. Optional orthogonal
// reflected views are allowed for diagnostic consumers. Invalid cameras reject.
int NativeSceneCamera_BoxVisible(const struct NativeSceneCamera *camera,const s16 minimum[3],const s16 maximum[3]);
enum NativeAssetResult NativeSceneCamera_ProjectTerrain(const struct NativeSceneCamera *camera,
    const struct NativeTerrainTriangle *source,struct NativeDrawTriangle *out,int *frontFacing);
#endif
