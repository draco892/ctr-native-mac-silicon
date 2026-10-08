#ifndef PLATFORM_NATIVE_MODEL_COMMANDS_H
#define PLATFORM_NATIVE_MODEL_COMMANDS_H

#include <platform/native_asset_readers.h>

struct NativeModelTexture
{
	u8 u[4], v[4];
	u16 clut, tpage;
};
struct NativeModelTriangle
{
	// Indices into the fresh-vertex array returned by native_model_vertices.
	u32 vertices[3], colors[3], command;
	struct NativeModelTexture texture;
	int textured;
};
struct NativeModelCommands
{
	const struct NativePtrMapView *map;
	size_t cursor, end, colorsOffset;
	u32 textureSlot, cachedColorCount, freshVertices, stripLength, stripCommand;
	u32 cache[256], rollingVertices[4], rollingColors[4];
	u8 cached[256];
};
// Borrowed immutable asset/map/entries; reopen after reload/Rebind. Initialized
// iterator fields must only be changed by Next. No allocation
// or GPU state. Open checks the command terminator and copied color-cache span.
// Next reconstructs pre-culling triangles with source RGB words and UV/CLUT/page
// metadata. It does not sample VRAM, light colors or transform positions.
// NOT_FOUND means absent command list (Open) or exhausted list (Next).
// Failed Next clears output and leaves iterator unchanged; a successful end
// commits trailing commands. Cached vertices must have been written in this list.
enum NativeAssetResult NativeModelCommands_Open(const struct NativeModelView *model,
    u32 headerIndex, struct NativeModelCommands *out);
enum NativeAssetResult NativeModelCommands_Next(struct NativeModelCommands *state,
    struct NativeModelTriangle *out);

#endif
