#include <platform/native_asset_readers.h>

#include <string.h>

enum NativePtrMapResult NativeAsset_DecodeDram(void *file, size_t fileSize,
    struct NativePtrMapEntry *entries, size_t capacity, struct NativePtrMapView *out)
{
	u32 mapOffset;
	u8 *asset;
	if (out == NULL)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (file == NULL || (uintptr_t)file > UINTPTR_MAX - fileSize)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	if (fileSize < 8)
		return NATIVE_PTRMAP_INVALID_MAP;
	mapOffset = CTR_ReadU32LE(file);
	if (mapOffset > INT32_MAX || mapOffset > fileSize - 8)
		return NATIVE_PTRMAP_INVALID_MAP;
	asset = (u8 *)file + 4;
	return NativePtrMap_Decode(asset, mapOffset, asset + mapOffset,
	    fileSize - 4 - mapOffset, entries, capacity, out);
}

static int NativeAsset_HasBytes(const struct NativePtrMapView *map, u32 offset, size_t bytes)
{
	return map != NULL && map->origin != NULL && map->originSize <= UINT32_MAX &&
	    offset <= map->originSize && bytes <= map->originSize - offset;
}

static u16 NativeAsset_ReadU16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static s16 NativeAsset_ReadS16(const u8 *p)
{
	u16 value = NativeAsset_ReadU16(p);
	// Avoid implementation-defined unsigned-to-signed narrowing.
	return (s16)(value <= INT16_MAX ? (s32)value : (s32)value - 65536);
}

// An unlisted zero is an optional NULL. A listed zero means asset origin,
// exactly as in the retail relocation map; never conflate the two cases.
static int NativeAsset_Resolve(const struct NativePtrMapView *map, u32 slot,
    size_t bytes, int optional, const u8 **target)
{
	void *resolved = NULL;
	enum NativePtrMapResult result;
	*target = NULL;
	if (!NativeAsset_HasBytes(map, slot, 4))
		return 0;
	result = NativePtrMap_Resolve(map, slot, bytes, &resolved);
	if (result == NATIVE_PTRMAP_OK)
	{
		*target = resolved;
		return 1;
	}
	return optional && result == NATIVE_PTRMAP_SLOT_NOT_FOUND &&
	    CTR_ReadU32LE(map->origin + slot) == 0;
}

static int NativeAsset_Array(const struct NativePtrMapView *map, u32 slot,
    u32 count, size_t stride, const u8 **target)
{
	if (count > SIZE_MAX / stride)
		return 0;
	return NativeAsset_Resolve(map, slot, (size_t)count * stride, count == 0, target);
}

static enum NativeAssetResult NativeAsset_ModelAtSlot(const struct NativePtrMapView *map,
    u32 slot, struct NativeModelView *out)
{
	const u8 *model;
	const u8 *headers;
	s16 count;
	if (!NativeAsset_Resolve(map, slot, NATIVE_MODEL_BYTES, 0, &model))
		return NATIVE_ASSET_INVALID_DATA;
	count = NativeAsset_ReadS16(model + 0x12);
	if (count < 0 || !NativeAsset_Array(map, (u32)(model - map->origin) + 0x14,
	    (u32)count, NATIVE_MODEL_HEADER_BYTES, &headers))
		return NATIVE_ASSET_INVALID_DATA;
	out->map = map;
	out->offset = (u32)(model - map->origin);
	out->headersOffset = headers == NULL ? 0 : (u32)(headers - map->origin);
	out->headerCount = (u32)count;
	out->id = NativeAsset_ReadS16(model + 0x10);
	memcpy(out->name, model, 16);
	out->name[16] = '\0';
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeMpk_Open(const struct NativePtrMapView *map, struct NativeMpkView *out)
{
	const u8 *icons;
	u32 count = 0;
	if (out == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (map == NULL || map->origin == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	if (!NativeAsset_Resolve(map, 0, 0x10, 1, &icons))
		return NATIVE_ASSET_INVALID_DATA;
	// Slot zero is icon metadata; model list starts at four, ends at an
	// unrelocated zero. Bound scanning by the true file size, never the arena.
	for (size_t slot = 4; slot <= map->originSize && map->originSize - slot >= 4; slot += 4)
	{
		const u8 *model;
		if (!NativeAsset_Resolve(map, (u32)slot, NATIVE_MODEL_BYTES, 1, &model))
			return NATIVE_ASSET_INVALID_DATA;
		if (model == NULL)
		{
			out->map = map;
			out->modelCount = count;
			return NATIVE_ASSET_OK;
		}
		count++;
	}
	return NATIVE_ASSET_INVALID_DATA;
}

enum NativeAssetResult NativeMpk_GetModel(const struct NativeMpkView *mpk, u32 index, struct NativeModelView *out)
{
	if (out == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (mpk == NULL || mpk->map == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	if (index >= mpk->modelCount)
		return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
	return NativeAsset_ModelAtSlot(mpk->map, 4 + index * 4, out);
}

enum NativeAssetResult NativeLevel_Open(const struct NativePtrMapView *map, struct NativeLevelView *out)
{
	struct NativeLevelView result = {0};
	const u8 *models, *instances;
	if (out == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (map == NULL || map->origin == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	if (!NativeAsset_HasBytes(map, 0, NATIVE_LEVEL_BYTES))
		return NATIVE_ASSET_INVALID_DATA;
	result.modelCount = CTR_ReadU32LE(map->origin + 0x14);
	result.instanceCount = CTR_ReadU32LE(map->origin + 0xc);
	if (!NativeAsset_Array(map, 0x18, result.modelCount, 4, &models) ||
	    !NativeAsset_Array(map, 0x10, result.instanceCount, NATIVE_INSTANCE_DEF_BYTES, &instances))
		return NATIVE_ASSET_INVALID_DATA;
	result.map = map;
	result.modelsOffset = models == NULL ? 0 : (u32)(models - map->origin);
	result.instancesOffset = instances == NULL ? 0 : (u32)(instances - map->origin);
	*out = result;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeLevel_GetModel(const struct NativeLevelView *level, u32 index, struct NativeModelView *out)
{
	if (out == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (level == NULL || level->map == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	if (index >= level->modelCount)
		return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
	return NativeAsset_ModelAtSlot(level->map, level->modelsOffset + index * 4, out);
}

enum NativeAssetResult NativeLevel_GetMesh(const struct NativeLevelView *level, struct NativeMeshView *out)
{
	struct NativeMeshView result = {0};
	const struct NativePtrMapView *map;
	const u8 *mesh;
	u32 base;
	if (out == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (level == NULL || level->map == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	map = level->map;
	if (!NativeAsset_Resolve(map, 0, NATIVE_MESH_BYTES, 0, &mesh))
		return NATIVE_ASSET_INVALID_DATA;
	base = (u32)(mesh - map->origin);
	result.quadCount = CTR_ReadU32LE(mesh);
	result.vertexCount = CTR_ReadU32LE(mesh + 4);
	result.bspCount = CTR_ReadU32LE(mesh + 0x1c);
	if (result.quadCount > INT32_MAX || result.vertexCount > INT32_MAX || result.bspCount > INT32_MAX ||
	    !NativeAsset_Array(map, base + 0xc, result.quadCount, NATIVE_QUAD_BYTES, &result.quads) ||
	    !NativeAsset_Array(map, base + 0x10, result.vertexCount, NATIVE_VERTEX_BYTES, &result.vertices) ||
	    !NativeAsset_Array(map, base + 0x18, result.bspCount, NATIVE_BSP_BYTES, &result.bsp))
		return NATIVE_ASSET_INVALID_DATA;
	*out = result;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeModel_GetHeader(const struct NativeModelView *model, u32 index, struct NativeModelHeaderView *out)
{
	const u8 *header;
	u32 offset;
	if (out == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (model == NULL || model->map == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	if (index >= model->headerCount)
		return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
	offset = model->headersOffset + index * NATIVE_MODEL_HEADER_BYTES;
	if (!NativeAsset_HasBytes(model->map, offset, NATIVE_MODEL_HEADER_BYTES))
		return NATIVE_ASSET_INVALID_DATA;
	header = model->map->origin + offset;
	memcpy(out->name, header, 16);
	out->name[16] = '\0';
	out->maxDistanceLOD = NativeAsset_ReadS16(header + 0x14);
	out->flags = NativeAsset_ReadU16(header + 0x16);
	for (int axis = 0; axis < 3; axis++)
		out->scale[axis] = NativeAsset_ReadS16(header + 0x18 + axis * 2);
	out->animationCount = CTR_ReadU32LE(header + 0x34);
	out->wire = header;
	return NATIVE_ASSET_OK;
}
