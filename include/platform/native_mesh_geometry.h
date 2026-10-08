#ifndef PLATFORM_NATIVE_MESH_GEOMETRY_H
#define PLATFORM_NATIVE_MESH_GEOMETRY_H
#include <platform/native_asset_readers.h>

struct NativeMeshVertex { s16 position[3]; u16 flags; u32 colorHigh, colorLow; };
struct NativeMeshQuad { u16 indices[9], flags; u32 drawOrderLow, drawOrderHigh; };
struct NativeMeshTriangle { u16 indices[3]; struct NativeMeshVertex vertices[3]; };
// Use unchanged validated mesh spans returned by NativeLevel_GetMesh. These
// value outputs borrow no host pointers; asset bytes are never patched. Outputs
// must be disjoint from inputs and asset storage. Errors
// clear output. No allocation, global state, texture or visibility traversal.
enum NativeAssetResult NativeMesh_GetVertex(const struct NativeMeshView *mesh,u32 index,struct NativeMeshVertex *out);
// Checks all nine indices, including midpoints unused by the coarse triangles.
enum NativeAssetResult NativeMesh_GetQuad(const struct NativeMeshView *mesh,u32 index,struct NativeMeshQuad *out);
// Two coarse low-LOD triangles, slots 2/0/3 and 0/1/3, from the resident
// sDrawLevelOvr1PLowLodIndices. Ignores draw-order/face masks and culling.
// Not the high-LOD curved grid, native subdivision or terrain material path.
enum NativeAssetResult NativeMesh_GetLowTriangle(const struct NativeMeshView *mesh,u32 quadIndex,u32 triangleIndex,struct NativeMeshTriangle *out);
#endif
