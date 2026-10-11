#include <platform/native_model_vertices.h>

#include <string.h>

static s32 NativeVertices_Sign(u32 n, unsigned bits)
{
	u32 mask = (1u << bits) - 1;
	n &= mask;
	return (n & (1u << (bits - 1))) ? (s32)n - (s32)(1u << bits) : (s32)n;
}

static enum NativeAssetResult NativeVertices_Bits(struct NativeVertexDecoder *state,
    unsigned bits, s32 *out)
{
	size_t word = state->bitPosition / 32;
	unsigned remain = (unsigned)(state->bitPosition % 32);
	int shift = 32 - (int)bits - (int)remain;
	if (word >= state->bytes / 4 || state->bitPosition > SIZE_MAX - bits)
		return NATIVE_ASSET_INVALID_DATA;
	u32 value = CTR_ReadU32LE(state->data + word * 4);
	if (shift < 0)
	{
		if (word + 1 >= state->bytes / 4) return NATIVE_ASSET_INVALID_DATA;
		value = (value << (unsigned)-shift) |
		    (CTR_ReadU32LE(state->data + (word + 1) * 4) >> (unsigned)(32 + shift));
	}
	else value >>= (unsigned)shift;
	*out = NativeVertices_Sign(value, bits);
	state->bitPosition += bits;
	return NATIVE_ASSET_OK;
}

static enum NativeAssetResult NativeVertices_Component(struct NativeVertexDecoder *state,
    unsigned bits, s32 base, s32 *accum)
{
	s32 delta;
	enum NativeAssetResult status = NativeVertices_Bits(state, bits + 1, &delta);
	if (status != NATIVE_ASSET_OK) return status;
	s32 value = bits == 7 ? delta : *accum + delta + base;
	*accum = NativeVertices_Sign((u8)value, 8);
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeVertexDecoder_Init(const void *data, size_t bytes,
    int compressed, struct NativeVertexDecoder *out)
{
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if ((data == NULL && bytes != 0) || (compressed != 0 && compressed != 1) ||
	    (data != NULL && (uintptr_t)data > UINTPTR_MAX - bytes)) return NATIVE_ASSET_INVALID_ARGUMENT;
	out->data = data; out->bytes = bytes; out->compressed = compressed;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeVertexDecoder_Next(struct NativeVertexDecoder *decoder,
    u32 temporalWord, struct NativeModelVertex *out)
{
	struct NativeVertexDecoder pending;
	struct NativeModelVertex vertex = {0};
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (decoder == NULL || (decoder->data == NULL && decoder->bytes != 0)) return NATIVE_ASSET_INVALID_ARGUMENT;
	if (decoder->vertexIndex == SIZE_MAX) return NATIVE_ASSET_INVALID_DATA;
	pending = *decoder;
	if (pending.compressed)
	{
		if (NativeVertices_Component(&pending, (temporalWord >> 6) & 7,
		        NativeVertices_Sign(temporalWord >> 25, 7) * 2, &pending.x) != NATIVE_ASSET_OK ||
		    NativeVertices_Component(&pending, (temporalWord >> 3) & 7,
		        NativeVertices_Sign(temporalWord >> 17, 8), &pending.z) != NATIVE_ASSET_OK ||
		    NativeVertices_Component(&pending, temporalWord & 7,
		        NativeVertices_Sign(temporalWord >> 9, 8), &pending.y) != NATIVE_ASSET_OK)
			return NATIVE_ASSET_INVALID_DATA;
		vertex.x = (u8)pending.x; vertex.y = (u8)pending.y; vertex.z = (u8)pending.z;
	}
	else
	{
		if (pending.vertexIndex > SIZE_MAX / 3) return NATIVE_ASSET_INVALID_DATA;
		size_t offset = pending.vertexIndex * 3;
		if (offset > pending.bytes || pending.bytes - offset < 3) return NATIVE_ASSET_INVALID_DATA;
		vertex.x = pending.data[offset]; vertex.y = pending.data[offset + 1]; vertex.z = pending.data[offset + 2];
	}
	pending.vertexIndex++;
	*decoder = pending; *out = vertex;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeModel_GetVertexCount(const struct NativeModelView *model,
    u32 headerIndex, u32 *out)
{
	struct NativeModelHeaderView header;
	void *commands;
	u32 count = 0;
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	*out = 0;
	enum NativeAssetResult status = NativeModel_GetHeader(model, headerIndex, &header);
	if (status != NATIVE_ASSET_OK) return status;
	u32 slot = (u32)(header.wire - model->map->origin) + 0x20;
	enum NativePtrMapResult result = NativePtrMap_Resolve(model->map, slot, 4, &commands);
	if (result != NATIVE_PTRMAP_OK)
		return result == NATIVE_PTRMAP_SLOT_NOT_FOUND && CTR_ReadU32LE(header.wire + 0x20) == 0 ?
		    NATIVE_ASSET_NOT_FOUND : NATIVE_ASSET_INVALID_DATA;
	size_t offset = (u8 *)commands - model->map->origin;
	for (size_t i = offset + 4; i <= model->map->originSize && model->map->originSize - i >= 4; i += 4)
	{
		u32 command = CTR_ReadU32LE(model->map->origin + i);
		if (command == UINT32_MAX) { *out = count; return NATIVE_ASSET_OK; }
		if ((command >> 16) != 0 && (command & 0x04000000u) == 0) count++;
	}
	return NATIVE_ASSET_INVALID_DATA;
}

static enum NativeAssetResult NativeVertices_Decode(const struct NativeModelView *model,
    u32 headerIndex, const struct NativeAnimationView *animation, u32 frameIndex,
    struct NativeModelVertex *vertices, size_t capacity, u32 *out)
{
	u32 count, temporal = 0;
	struct NativeFrameView frame;
	struct NativeVertexDecoder decoder;
	enum NativeAssetResult status;
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	*out = 0;
	status = NativeModel_GetVertexCount(model, headerIndex, &count);
	if (status != NATIVE_ASSET_OK) return status;
	if (count > capacity) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
	if (count != 0 && vertices == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	int compressed = animation != NULL ? animation->hasDelta : 0;
	size_t bytes = 0;
	if (animation == NULL)
	{
		if (count != 0)
		{
			status = NativeModel_ReadStaticDeltaWord(model, headerIndex, 0, &temporal);
			if (status != NATIVE_ASSET_OK && status != NATIVE_ASSET_NOT_FOUND) return status;
			compressed = status == NATIVE_ASSET_OK;
		}
		if (compressed)
		{
			size_t bits = 0;
			for (u32 i = 0; i < count; i++)
			{
				status = NativeModel_ReadStaticDeltaWord(model, headerIndex, i, &temporal);
				if (status != NATIVE_ASSET_OK) return status;
				unsigned width = ((temporal >> 6) & 7) + ((temporal >> 3) & 7) + (temporal & 7) + 3;
				if (bits > SIZE_MAX - width) return NATIVE_ASSET_INVALID_DATA;
				bits += width;
			}
			if (bits > SIZE_MAX - 31 || (bits + 31) / 32 > SIZE_MAX / 4) return NATIVE_ASSET_INVALID_DATA;
			bytes = (bits + 31) / 32 * 4;
		}
		else
		{
			size_t records = count;
			if (records > SIZE_MAX / 3) return NATIVE_ASSET_INVALID_DATA;
			bytes = records * 3;
		}
		status = NativeModel_GetStaticFrame(model, headerIndex, bytes, &frame);
	}
	else status = NativeAnimation_GetStoredFrame(animation, frameIndex, &frame);
	if (status != NATIVE_ASSET_OK) return status;
	status = NativeVertexDecoder_Init(frame.vertices, frame.vertexBytes, compressed, &decoder);
	if (status != NATIVE_ASSET_OK) return status;
	for (u32 i = 0; i < count; i++)
	{
		if (compressed)
		{
			status = animation != NULL ? NativeAnimation_ReadDeltaWord(animation, i, &temporal) :
			    NativeModel_ReadStaticDeltaWord(model, headerIndex, i, &temporal);
			if (status != NATIVE_ASSET_OK) return status;
		}
		status = NativeVertexDecoder_Next(&decoder, temporal, &vertices[i]);
		if (status != NATIVE_ASSET_OK) return status;
	}
	*out = count;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeModel_DecodeStaticVertices(const struct NativeModelView *model,
    u32 headerIndex, struct NativeModelVertex *vertices, size_t capacity, u32 *count)
{
	return NativeVertices_Decode(model, headerIndex, NULL, 0, vertices, capacity, count);
}

enum NativeAssetResult NativeModel_DecodeAnimationVertices(const struct NativeModelView *model,
    u32 headerIndex, u32 animationIndex, u32 storedFrameIndex,
    struct NativeModelVertex *vertices, size_t capacity, u32 *count)
{
	struct NativeAnimationView animation;
	if (count == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	*count = 0;
	enum NativeAssetResult status = NativeModel_GetAnimation(model, headerIndex, animationIndex, &animation);
	return status == NATIVE_ASSET_OK ? NativeVertices_Decode(model, headerIndex, &animation, storedFrameIndex,
	    vertices, capacity, count) : status;
}
