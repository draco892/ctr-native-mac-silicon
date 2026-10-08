#ifndef PLATFORM_NATIVE_ASSET_READERS_H
#define PLATFORM_NATIVE_ASSET_READERS_H

#include <platform/native_ptrmap.h>

// Retail byte sizes, independent of host structure packing or pointer width.
enum
{
	NATIVE_MODEL_BYTES = 0x18,
	NATIVE_MODEL_HEADER_BYTES = 0x40,
	NATIVE_LEVEL_BYTES = 0x1f4,
	NATIVE_INSTANCE_DEF_BYTES = 0x40,
	NATIVE_MESH_BYTES = 0x20,
	NATIVE_QUAD_BYTES = 0x5c,
	NATIVE_VERTEX_BYTES = 0x10,
	NATIVE_BSP_BYTES = 0x20,
};

enum NativeAssetResult
{
	NATIVE_ASSET_OK,
	NATIVE_ASSET_INVALID_ARGUMENT,
	NATIVE_ASSET_INVALID_DATA,
	NATIVE_ASSET_INDEX_OUT_OF_RANGE,
	NATIVE_ASSET_NOT_FOUND,
	NATIVE_ASSET_OUTPUT_TOO_SMALL,
};

struct NativeModelView
{
	const struct NativePtrMapView *map;
	u32 offset;
	u32 headersOffset;
	u32 headerCount;
	s16 id;
	char name[17];
};

struct NativeMpkView
{
	const struct NativePtrMapView *map;
	u32 modelCount;
};

struct NativeLevelView
{
	const struct NativePtrMapView *map;
	u32 modelCount;
	u32 modelsOffset;
	u32 instanceCount;
	u32 instancesOffset;
};

struct NativeMeshView
{
	u32 quadCount, vertexCount, bspCount;
	// Validated wire spans, NOT arrays of pointer-bearing runtime structures.
	const u8 *quads, *vertices, *bsp;
};

struct NativeModelHeaderView
{
	char name[17];
	s16 maxDistanceLOD;
	u16 flags;
	s16 scale[3];
	u32 animationCount;
	// Complete 64-byte wire record; nested animation/texture data is not decoded.
	const u8 *wire;
};

struct NativeInstanceDefView
{
	char name[17];
	struct NativeModelView model;
	s16 scale[3], position[3], rotation[3];
	u32 colorRGBA, flags;
	s32 unk24, unk28, modelID;
	// The on-disk ptrInstance field is runtime scratch, never a host pointer.
};

// DRAM file envelope used by LOAD_DramFileCallback: four-byte signed map offset,
// then asset bytes, then PTR. Positive/zero offsets are relative to the asset
// start. Negative offsets denote the separate-PTR path and are rejected here;
// that path must call NativePtrMap_Decode with the known asset and PTR lengths.
// The returned origin excludes both the envelope header and embedded PTR.
enum NativePtrMapResult NativeAsset_DecodeDram(void *file, size_t fileSize,
    struct NativePtrMapEntry *entries, size_t capacity, struct NativePtrMapView *out);

// All APIs require an unchanged successful NativePtrMap_Decode view, with the
// asset length excluding any appended PTR data or CD padding. No allocations
// or in-place patching. Outputs are cleared on error. Open checks root/table
// spans; individual models are checked by GetModel. MPK scanning also checks
// each model pointer, but not its nested header data.
// Views borrow map/asset/entries. After reload, release or Rebind, reopen views
// before use (in particular mesh/header outputs contain transient pointers).
enum NativeAssetResult NativeMpk_Open(const struct NativePtrMapView *map, struct NativeMpkView *out);
enum NativeAssetResult NativeMpk_GetModel(const struct NativeMpkView *mpk, u32 index, struct NativeModelView *out);
enum NativeAssetResult NativeLevel_Open(const struct NativePtrMapView *map, struct NativeLevelView *out);
enum NativeAssetResult NativeLevel_GetModel(const struct NativeLevelView *level, u32 index, struct NativeModelView *out);
enum NativeAssetResult NativeLevel_GetMesh(const struct NativeLevelView *level, struct NativeMeshView *out);
enum NativeAssetResult NativeLevel_GetInstance(const struct NativeLevelView *level, u32 index, struct NativeInstanceDefView *out);
// Checked reopening by an asset-relative offset, for persistent runtime refs.
enum NativeAssetResult NativeModel_Open(const struct NativePtrMapView *map, u32 offset, struct NativeModelView *out);
enum NativeAssetResult NativeModel_GetHeader(const struct NativeModelView *model, u32 index, struct NativeModelHeaderView *out);

#endif
