#ifndef PLATFORM_NATIVE_VERTEX_ANIMATION_H
#define PLATFORM_NATIVE_VERTEX_ANIMATION_H
#include <platform/native_mesh_geometry.h>
struct NativeVertexAnimationView {
    struct NativeMeshView mesh;
    const struct NativePtrMapView *map;
    const u8 *records,*visibility,*environment;
    u32 count; int scenery;
};
// Decode SCVert (16 bytes) or WaterVert (8 bytes) according to Level.configFlags.
// All nested slots/vertex membership/28 water samples are checked. Borrowed view.
enum NativeAssetResult NativeVertexAnimation_Open(const struct NativeLevelView *level,
    const struct NativeMeshView *mesh,struct NativeVertexAnimationView *out);
// Output is a separate wire-sized vertex buffer; quads/BSP borrow the mesh.
// No asset writes, global GTE or heap. Output capacity in bytes. Empty animation
// returns the original mesh. Optional masks (0..4) are ORed, LSB-first; zero masks
// uses the level default, or all vertices if no default was supplied by the asset.
// tick is the game timer; scenery uses tick<<7, water tick/8 modulo 28.
enum NativeAssetResult NativeVertexAnimation_Apply(const struct NativeVertexAnimationView *animation,u32 tick,
    const void *const *masks,const size_t *maskBytes,size_t maskCount,
    void *vertexBuffer,size_t capacity,struct NativeMeshView *out);
#endif
