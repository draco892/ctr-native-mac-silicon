#include <platform/native_model_transform.h>
#include <string.h>

static s32 NativeTransform_Byte(u8 value, int compressed)
{
	return compressed && value > 127 ? (s32)value - 256 : (s32)value;
}
static u32 NativeTransform_XZ(const struct NativeModelVertex *vertex, int compressed)
{
	return (u32)NativeTransform_Byte(vertex->x, compressed) |
	    ((u32)NativeTransform_Byte(vertex->z, compressed) << 16);
}
enum NativeAssetResult NativeModel_PackVertex(const struct NativeFrameView *frame,
    const struct NativeModelVertex *vertex, int compressed,
    const struct NativeFrameView *nextFrame, const struct NativeModelVertex *nextVertex,
    struct NativePackedModelVertex *out)
{
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (frame == NULL || vertex == NULL || (compressed != 0 && compressed != 1) ||
	    ((nextFrame == NULL) != (nextVertex == NULL))) return NATIVE_ASSET_INVALID_ARGUMENT;
	u32 origin, xz = NativeTransform_XZ(vertex, compressed);
	s32 y = frame->position[2] + NativeTransform_Byte(vertex->y, compressed);
	if (nextFrame == NULL)
	{
		origin = ((u16)frame->position[0] & 0x7fffu) | ((u32)(u16)frame->position[1] << 16);
		out->xy = ((xz + origin) << 2) & 0xfff8ffffu;
		out->z = (u32)y << 2;
	}
	else
	{
		origin = (u16)(frame->position[0] + nextFrame->position[0]) |
		    ((u32)(u16)(frame->position[1] + nextFrame->position[1]) << 16);
		xz += NativeTransform_XZ(nextVertex, compressed);
		y += nextFrame->position[2] + NativeTransform_Byte(nextVertex->y, compressed);
		out->xy = ((xz + origin) << 1) & 0xfff8ffffu;
		out->z = (u32)y << 1;
	}
	return NATIVE_ASSET_OK;
}
static s16 NativeTransform_S16(u32 value)
{
	u32 low = value & 0xffffu;
	return (s16)(low <= INT16_MAX ? (s32)low : (s32)low - 65536);
}
enum NativeAssetResult NativeModel_UnpackPosition(const struct NativePackedModelVertex *vertex,
    s16 out[3])
{
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, 3 * sizeof(*out));
	if (vertex == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	out[0] = NativeTransform_S16(vertex->xy);
	out[1] = NativeTransform_S16(vertex->xy >> 16);
	out[2] = NativeTransform_S16(vertex->z);
	return NATIVE_ASSET_OK;
}
static enum NativeAssetResult NativeTransform_Capacity(const struct NativeModelView *model,
    u32 headerIndex, const struct NativeModelVertex *scratch,
    const struct NativePackedModelVertex *out, size_t capacity, u32 *needed)
{
	enum NativeAssetResult status = NativeModel_GetVertexCount(model, headerIndex, needed);
	if (status != NATIVE_ASSET_OK) return status;
	if (*needed > capacity) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
	if (*needed != 0 && (scratch == NULL || out == NULL)) return NATIVE_ASSET_INVALID_ARGUMENT;
	return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeModel_PackStaticVertices(const struct NativeModelView *model,
    u32 headerIndex, struct NativeModelVertex *scratch, struct NativePackedModelVertex *out,
    size_t capacity, u32 *count)
{
	u32 needed, decoded, delta;
	struct NativeFrameView frame;
	if (count == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	*count = 0;
	enum NativeAssetResult status = NativeTransform_Capacity(model, headerIndex, scratch, out, capacity, &needed);
	if (status != NATIVE_ASSET_OK) return status;
	status = NativeModel_GetStaticFrame(model, headerIndex, 0, &frame);
	if (status != NATIVE_ASSET_OK) return status;
	int compressed = 0;
	if (needed != 0)
	{
		status = NativeModel_ReadStaticDeltaWord(model, headerIndex, 0, &delta);
		if (status != NATIVE_ASSET_OK && status != NATIVE_ASSET_NOT_FOUND) return status;
		compressed = status == NATIVE_ASSET_OK;
	}
	status = NativeModel_DecodeStaticVertices(model, headerIndex, scratch, capacity, &decoded);
	if (status != NATIVE_ASSET_OK) return status;
	for (u32 i = 0; i < decoded; i++)
	{
		status = NativeModel_PackVertex(&frame, &scratch[i], compressed, NULL, NULL, &out[i]);
		if (status != NATIVE_ASSET_OK) return status;
	}
	*count = decoded;
	return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeModel_PackAnimationVertices(const struct NativeModelView *model,
    u32 headerIndex, u32 animationIndex, u32 logicalIndex,
    struct NativeModelVertex *currentScratch, struct NativeModelVertex *nextScratch,
    struct NativePackedModelVertex *out, size_t capacity, u32 *count)
{
	struct NativeAnimationView animation;
	struct NativeFrameSelection selection;
	u32 needed, decoded, nextCount;
	if (count == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	*count = 0;
	enum NativeAssetResult status = NativeTransform_Capacity(model, headerIndex, currentScratch, out, capacity, &needed);
	if (status != NATIVE_ASSET_OK) return status;
	status = NativeModel_GetAnimation(model, headerIndex, animationIndex, &animation);
	if (status != NATIVE_ASSET_OK) return status;
	status = NativeAnimation_SelectFrame(&animation, logicalIndex, &selection);
	if (status != NATIVE_ASSET_OK) return status;
	if (selection.hasNext && needed != 0 && nextScratch == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	status = NativeModel_DecodeAnimationVertices(model, headerIndex, animationIndex,
	    selection.storedIndex, currentScratch, capacity, &decoded);
	if (status != NATIVE_ASSET_OK) return status;
	if (selection.hasNext)
	{
		status = NativeModel_DecodeAnimationVertices(model, headerIndex, animationIndex,
		    selection.storedIndex + 1, nextScratch, capacity, &nextCount);
		if (status != NATIVE_ASSET_OK) return status;
		if (nextCount != decoded) return NATIVE_ASSET_INVALID_DATA;
	}
	for (u32 i = 0; i < decoded; i++)
	{
		status = NativeModel_PackVertex(&selection.current, &currentScratch[i], animation.hasDelta,
		    selection.hasNext ? &selection.next : NULL, selection.hasNext ? &nextScratch[i] : NULL, &out[i]);
		if (status != NATIVE_ASSET_OK) return status;
	}
	*count = decoded;
	return NATIVE_ASSET_OK;
}
