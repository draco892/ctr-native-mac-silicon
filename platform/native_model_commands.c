#include <platform/native_model_commands.h>
#include <string.h>

static enum NativeAssetResult NativeCommands_Pointer(const struct NativePtrMapView *map,
    u32 slot, size_t bytes, const u8 **out)
{
	void *p;
	*out = NULL;
	if (slot > map->originSize || map->originSize - slot < 4) return NATIVE_ASSET_INVALID_DATA;
	enum NativePtrMapResult result = NativePtrMap_Resolve(map, slot, bytes, &p);
	if (result == NATIVE_PTRMAP_OK) { *out = p; return NATIVE_ASSET_OK; }
	return result == NATIVE_PTRMAP_SLOT_NOT_FOUND && CTR_ReadU32LE(map->origin + slot) == 0 ?
	    NATIVE_ASSET_NOT_FOUND : NATIVE_ASSET_INVALID_DATA;
}

enum NativeAssetResult NativeModelCommands_Open(const struct NativeModelView *model,
    u32 headerIndex, struct NativeModelCommands *out)
{
	struct NativeModelCommands result = {0};
	struct NativeModelHeaderView header;
	const u8 *commands, *colors;
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	enum NativeAssetResult status = NativeModel_GetHeader(model, headerIndex, &header);
	if (status != NATIVE_ASSET_OK) return status;
	u32 h = (u32)(header.wire - model->map->origin);
	status = NativeCommands_Pointer(model->map, h + 0x20, 4, &commands);
	if (status != NATIVE_ASSET_OK) return status;
	result.map = model->map;
	result.cursor = (size_t)(commands - model->map->origin) + 4;
	result.cachedColorCount = CTR_ReadU32LE(commands);
	// Color indices contain seven bits; the native cache never exceeds 128 words.
	if (result.cachedColorCount > 128) return NATIVE_ASSET_INVALID_DATA;
	status = NativeCommands_Pointer(model->map, h + 0x2c,
	    result.cachedColorCount * 4u, &colors);
	if (status != NATIVE_ASSET_OK && !(status == NATIVE_ASSET_NOT_FOUND && result.cachedColorCount == 0))
		return NATIVE_ASSET_INVALID_DATA;
	result.colorsOffset = colors != NULL ? (size_t)(colors - model->map->origin) : SIZE_MAX;
	result.textureSlot = h + 0x28;
	for (size_t i = result.cursor; i <= result.map->originSize && result.map->originSize - i >= 4; i += 4)
	{
		if (CTR_ReadU32LE(result.map->origin + i) == UINT32_MAX)
		{
			result.end = i; *out = result; return NATIVE_ASSET_OK;
		}
	}
	return NATIVE_ASSET_INVALID_DATA;
}

static enum NativeAssetResult NativeCommands_Color(const struct NativeModelCommands *state,
    u32 index, int cached, u32 *out)
{
	if (state->colorsOffset == SIZE_MAX || (cached && index >= state->cachedColorCount))
		return NATIVE_ASSET_INVALID_DATA;
	size_t offset = state->colorsOffset;
	if (offset > state->map->originSize || state->map->originSize - offset < (index + 1u) * 4u)
		return NATIVE_ASSET_INVALID_DATA;
	*out = CTR_ReadU32LE(state->map->origin + offset + index * 4u);
	return NATIVE_ASSET_OK;
}

static enum NativeAssetResult NativeCommands_Texture(const struct NativeModelCommands *state,
    u32 command, struct NativeModelTriangle *out)
{
	u32 index = command & 0x1ff;
	const u8 *table, *texture;
	if (index == 0) return NATIVE_ASSET_OK;
	enum NativeAssetResult status = NativeCommands_Pointer(state->map, state->textureSlot, index * 4u, &table);
	if (status != NATIVE_ASSET_OK) return NATIVE_ASSET_INVALID_DATA;
	size_t slot = (size_t)(table - state->map->origin) + (index - 1u) * 4u;
	if (slot > UINT32_MAX) return NATIVE_ASSET_INVALID_DATA;
	status = NativeCommands_Pointer(state->map, (u32)slot, 12, &texture);
	if (status == NATIVE_ASSET_NOT_FOUND) return NATIVE_ASSET_OK; // Null entry emits untextured G3.
	if (status != NATIVE_ASSET_OK) return status;
	out->textured = 1;
	out->texture.u[0] = texture[0]; out->texture.v[0] = texture[1];
	out->texture.u[1] = texture[4]; out->texture.v[1] = texture[5];
	out->texture.u[2] = texture[8]; out->texture.v[2] = texture[9];
	out->texture.u[3] = texture[10]; out->texture.v[3] = texture[11];
	out->texture.clut = CTR_ReadU16LE(texture + 2);
	out->texture.tpage = CTR_ReadU16LE(texture + 6);
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeModelCommands_Next(struct NativeModelCommands *state,
    struct NativeModelTriangle *out)
{
	struct NativeModelTriangle triangle = {0};
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (state == NULL || state->map == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	struct NativeModelCommands pending = *state;
	while (pending.cursor < pending.end)
	{
		u32 command = CTR_ReadU32LE(pending.map->origin + pending.cursor);
		pending.cursor += 4;
		if ((command >> 16) == 0)
		{
			u32 a, b;
			if (NativeCommands_Color(&pending, (command >> 9) & 127, command & 1, &a) != NATIVE_ASSET_OK ||
			    NativeCommands_Color(&pending, (command >> 2) & 127, command & 2, &b) != NATIVE_ASSET_OK)
				return NATIVE_ASSET_INVALID_DATA;
			pending.rollingColors[1] = a; pending.rollingColors[2] = a; pending.rollingColors[3] = b;
			continue;
		}
		u32 slot = (command >> 16) & 255, vertex, color;
		if (command & 0x04000000u)
		{
			if (!pending.cached[slot]) return NATIVE_ASSET_INVALID_DATA;
			vertex = pending.cache[slot];
		}
		else
		{
			if (pending.freshVertices == UINT32_MAX) return NATIVE_ASSET_INVALID_DATA;
			vertex = pending.freshVertices++;
			pending.cache[slot] = vertex; pending.cached[slot] = 1;
		}
		if (NativeCommands_Color(&pending, (command >> 9) & 127, command & 0x08000000u, &color) != NATIVE_ASSET_OK)
			return NATIVE_ASSET_INVALID_DATA;
		for (unsigned i = 0; i < 3; i++)
		{
			pending.rollingVertices[i] = pending.rollingVertices[i + 1];
			pending.rollingColors[i] = pending.rollingColors[i + 1];
		}
		pending.rollingVertices[3] = vertex; pending.rollingColors[3] = color;
		if ((command & 0x80000000u) || pending.stripLength == 0) pending.stripCommand = command;
		if (command & 0x80000000u) pending.stripLength = 0;
		if (pending.stripLength > 2 && (command & 0x40000000u))
		{
			pending.rollingVertices[1] = pending.rollingVertices[0];
			pending.rollingColors[1] = pending.rollingColors[0];
		}
		if (pending.stripLength < 2) { pending.stripLength++; continue; }
		triangle.command = pending.stripLength == 2 ? pending.stripCommand : command;
		// Only the distinction 0/1/2/>2 matters; avoid overflow on long strips.
		pending.stripLength = 3;
		for (unsigned i = 0; i < 3; i++)
		{
			triangle.vertices[i] = pending.rollingVertices[i + 1];
			triangle.colors[i] = pending.rollingColors[i + 1];
		}
		enum NativeAssetResult status = NativeCommands_Texture(&pending, triangle.command, &triangle);
		if (status != NATIVE_ASSET_OK) return status;
		*state = pending; *out = triangle;
		return NATIVE_ASSET_OK;
	}
	*state = pending;
	return NATIVE_ASSET_NOT_FOUND;
}
