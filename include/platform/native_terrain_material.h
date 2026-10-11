#ifndef PLATFORM_NATIVE_TERRAIN_MATERIAL_H
#define PLATFORM_NATIVE_TERRAIN_MATERIAL_H
#include <platform/native_mesh_geometry.h>
#include <platform/native_model_commands.h>
struct NativeTerrainTriangle {
    struct NativeMeshTriangle geometry;
    struct NativeModelTexture texture;
    u32 selectorWord;
    int textured, doubleSided;
    s16 orderingBias;
};
// Immutable decoded map/mesh, caller-owned disjoint value output. Face 0..3,
// triangle 0..1, texture LOD 0=far/1=middle/2=near. Reopens all relocation slots;
// no host pointer or active animation pointer is written to the asset.
// NOT_FOUND means a degenerate face triangle. Errors clear output.
enum NativeAssetResult NativeTerrain_GetTriangle(const struct NativeLevelView *level,
    const struct NativeMeshView *mesh,u32 quadIndex,u32 face,u32 triangle,u32 lod,u32 tick,
    struct NativeTerrainTriangle *out);
// Native direct-face NCLIP sign folded with selector sign/double-sided flag.
int NativeTerrain_FrontFacing(const struct NativeTerrainTriangle *triangle,s64 screenArea);
#endif
