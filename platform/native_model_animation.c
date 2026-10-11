#include <platform/native_model_animation.h>

#include <string.h>

static int NativeAnimation_Range(const struct NativePtrMapView *map, u32 offset, size_t bytes)
{
	return map != NULL && map->origin != NULL && offset <= map->originSize && bytes <= map->originSize - offset;
}

static enum NativeAssetResult NativeAnimation_Pointer(const struct NativePtrMapView *map,
    u32 slot, size_t bytes, const u8 **out)
{
	void *p;
	*out = NULL;
	if (!NativeAnimation_Range(map, slot, 4)) return NATIVE_ASSET_INVALID_DATA;
	enum NativePtrMapResult result = NativePtrMap_Resolve(map, slot, bytes, &p);
	if (result == NATIVE_PTRMAP_OK) { *out = p; return NATIVE_ASSET_OK; }
	if (result == NATIVE_PTRMAP_SLOT_NOT_FOUND && CTR_ReadU32LE(map->origin + slot) == 0)
		return NATIVE_ASSET_NOT_FOUND;
	return NATIVE_ASSET_INVALID_DATA;
}

static s16 NativeAnimation_S16(const u8 *p)
{
	u16 n = CTR_ReadU16LE(p);
	return (s16)(n <= INT16_MAX ? (s32)n : (s32)n - 65536);
}

static enum NativeAssetResult NativeAnimation_Header(const struct NativeModelView *model,
    u32 index, struct NativeModelHeaderView *header, u32 *offset)
{
	enum NativeAssetResult result = NativeModel_GetHeader(model, index, header);
	if (result == NATIVE_ASSET_OK) *offset = (u32)(header->wire - model->map->origin);
	return result;
}

enum NativeAssetResult NativeModel_GetAnimation(const struct NativeModelView *model,
    u32 headerIndex, u32 animationIndex, struct NativeAnimationView *out)
{
	struct NativeModelHeaderView header;
	struct NativeAnimationView result = {0};
	u32 h;
	const u8 *table, *anim, *delta;
	enum NativeAssetResult status;
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	status = NativeAnimation_Header(model, headerIndex, &header, &h);
	if (status != NATIVE_ASSET_OK) return status;
	if (animationIndex >= header.animationCount) return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
	size_t tableCount = header.animationCount;
	if (tableCount > SIZE_MAX / 4) return NATIVE_ASSET_INVALID_DATA;
	status = NativeAnimation_Pointer(model->map, h + 0x38, tableCount * 4, &table);
	if (status != NATIVE_ASSET_OK) return status;
	status = NativeAnimation_Pointer(model->map, (u32)(table - model->map->origin) + animationIndex * 4,
	    NATIVE_ANIMATION_BYTES, &anim);
	if (status != NATIVE_ASSET_OK) return status;
	u16 frames = CTR_ReadU16LE(anim + 0x10);
	result.map = model->map;
	result.offset = (u32)(anim - result.map->origin);
	result.framesOffset = result.offset + NATIVE_ANIMATION_BYTES;
	result.logicalFrameCount = frames & 0x7fff;
	result.frameStride = CTR_ReadU16LE(anim + 0x12); // Renderer uses unsigned stride.
	result.interpolated = (frames & 0x8000) != 0;
	result.storedFrameCount = result.interpolated ? result.logicalFrameCount / 2u + 1 : result.logicalFrameCount;
	if (result.logicalFrameCount == 0 || result.frameStride < NATIVE_FRAME_BYTES ||
	    result.storedFrameCount > SIZE_MAX / result.frameStride ||
	    !NativeAnimation_Range(result.map, result.framesOffset, (size_t)result.storedFrameCount * result.frameStride))
		return NATIVE_ASSET_INVALID_DATA;
	status = NativeAnimation_Pointer(result.map, result.offset + 0x14, 4, &delta);
	if (status != NATIVE_ASSET_OK && status != NATIVE_ASSET_NOT_FOUND) return status;
	result.hasDelta = status == NATIVE_ASSET_OK;
	memcpy(result.name, anim, 16); result.name[16] = '\0';
	*out = result;
	return NATIVE_ASSET_OK;
}

static enum NativeAssetResult NativeAnimation_Frame(const struct NativePtrMapView *map,
    u32 offset, size_t frameBytes, int exact, size_t requested, struct NativeFrameView *out)
{
	struct NativeFrameView result = {0};
	if (!NativeAnimation_Range(map, offset, NATIVE_FRAME_BYTES)) return NATIVE_ASSET_INVALID_DATA;
	const u8 *wire = map->origin + offset;
	u32 vertexOffset = CTR_ReadU32LE(wire + 0x18);
	if (vertexOffset < NATIVE_FRAME_BYTES || vertexOffset > frameBytes ||
	    !NativeAnimation_Range(map, offset, frameBytes)) return NATIVE_ASSET_INVALID_DATA;
	result.vertexBytes = exact ? frameBytes - vertexOffset : requested;
	if (result.vertexBytes > frameBytes - vertexOffset) return NATIVE_ASSET_INVALID_DATA;
	for (int axis = 0; axis < 3; axis++) result.position[axis] = NativeAnimation_S16(wire + axis * 2);
	result.wire = wire;
	result.vertices = wire + vertexOffset;
	*out = result;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeAnimation_GetStoredFrame(const struct NativeAnimationView *animation,
    u32 index, struct NativeFrameView *out)
{
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (animation == NULL || animation->map == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	if (index >= animation->storedFrameCount) return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
	return NativeAnimation_Frame(animation->map, animation->framesOffset + index * animation->frameStride,
	    animation->frameStride, 1, 0, out);
}

enum NativeAssetResult NativeAnimation_SelectFrame(const struct NativeAnimationView *animation,
    u32 logicalIndex, struct NativeFrameSelection *out)
{
	struct NativeFrameSelection result = {0};
	enum NativeAssetResult status;
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (animation == NULL || animation->map == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	if (animation->logicalFrameCount == 0) return NATIVE_ASSET_INVALID_DATA;
	result.logicalIndex = logicalIndex < animation->logicalFrameCount ? logicalIndex : animation->logicalFrameCount - 1u;
	result.storedIndex = animation->interpolated ? result.logicalIndex / 2 : result.logicalIndex;
	result.hasNext = animation->interpolated && (result.logicalIndex & 1u);
	status = NativeAnimation_GetStoredFrame(animation, result.storedIndex, &result.current);
	if (status != NATIVE_ASSET_OK) return status;
	if (result.hasNext)
	{
		status = NativeAnimation_GetStoredFrame(animation, result.storedIndex + 1, &result.next);
		if (status != NATIVE_ASSET_OK) return status;
	}
	*out = result;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeModel_GetStaticFrame(const struct NativeModelView *model,
    u32 headerIndex, size_t vertexBytes, struct NativeFrameView *out)
{
	struct NativeModelHeaderView header;
	u32 h;
	const u8 *wire;
	enum NativeAssetResult status;
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	status = NativeAnimation_Header(model, headerIndex, &header, &h);
	if (status != NATIVE_ASSET_OK) return status;
	status = NativeAnimation_Pointer(model->map, h + 0x24, NATIVE_FRAME_BYTES, &wire);
	if (status != NATIVE_ASSET_OK) return status;
	u32 offset = (u32)(wire - model->map->origin);
	return NativeAnimation_Frame(model->map, offset, model->map->originSize - offset, 0, vertexBytes, out);
}

static enum NativeAssetResult NativeAnimation_Delta(const struct NativePtrMapView *map,
    u32 slot, u32 index, u32 *out)
{
	const u8 *delta;
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	*out = 0;
	size_t wordIndex = index;
	if (wordIndex >= SIZE_MAX / 4) return NATIVE_ASSET_INVALID_DATA;
	enum NativeAssetResult status = NativeAnimation_Pointer(map, slot, (wordIndex + 1) * 4, &delta);
	if (status == NATIVE_ASSET_OK) *out = CTR_ReadU32LE(delta + (size_t)index * 4);
	return status;
}

enum NativeAssetResult NativeAnimation_ReadDeltaWord(const struct NativeAnimationView *animation,
    u32 vertexIndex, u32 *out)
{
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	*out = 0;
	if (animation == NULL || animation->map == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	return NativeAnimation_Delta(animation->map, animation->offset + 0x14, vertexIndex, out);
}

enum NativeAssetResult NativeModel_ReadStaticDeltaWord(const struct NativeModelView *model,
    u32 headerIndex, u32 vertexIndex, u32 *out)
{
	struct NativeModelHeaderView header;
	u32 h;
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	*out = 0;
	enum NativeAssetResult status = NativeAnimation_Header(model, headerIndex, &header, &h);
	return status == NATIVE_ASSET_OK ? NativeAnimation_Delta(model->map, h + 0x30, vertexIndex, out) : status;
}
