#ifndef PLATFORM_NATIVE_MODEL_DRAW_H
#define PLATFORM_NATIVE_MODEL_DRAW_H
#include <platform/native_model_commands.h>
#include <platform/native_model_projection.h>
#include <platform/native_vram.h>

struct NativeModelDrawWorkspace
{
	struct NativeModelVertex *current, *next;
	struct NativePackedModelVertex *packed;
	size_t capacity;
};
struct NativeDrawTriangle
{
	struct NativeModelTriangle source;
	s16 screen[3][2];
	u16 depth[3];
	u32 projectionFlags;
	s64 signedArea;
	u16 averageDepth;
	struct NativeTexturePixel corners[3];
	int hasCornerPixels;
};
struct NativeModelDraw
{
	struct NativeModelCommands commands;
	struct NativeProjectionConfig projection;
	const struct NativePackedModelVertex *packed;
	struct NativeVramView vram;
	u32 vertexCount;
};
// Frame preparation plus triangle commands. animationIndex=UINT32_MAX selects
// static; otherwise logicalIndex clamps using the animation reader. Scratch
// arrays must be mutually disjoint and distinct from all asset/state storage.
// Caller owns them. Output clears on errors; scratch is unpublished until OK.
// The initialized iterator borrows packed/model map/asset/entries/VRAM. Keep
// them unchanged/alive; reopening is required after reload or Rebind. Projection
// config is copied. No allocation, globals or resident game-structure access.
enum NativeAssetResult NativeModelDraw_Open(const struct NativeModelView *model,u32 headerIndex,
    u32 animationIndex,u32 logicalIndex,const struct NativeProjectionConfig *projection,
    const struct NativeModelDrawWorkspace *workspace,const struct NativeVramView *vram,
    struct NativeModelDraw *out);
// Projects all three source corners using a fresh RTPT state for EACH triangle.
// Flags therefore aggregate all three vertices, not legacy RTPS continuation
// flags. No culling, clipping, lighting, ordering table or rasterization. Area
// and arithmetic averageDepth are diagnostics, not NCLIP/MAC0/AVSZ3 emulation.
// Optional corner sampling is strict integer UV only, not interpolated shading.
// NOT_FOUND means exhausted. Other errors clear output and preserve iterator.
enum NativeAssetResult NativeModelDraw_Next(struct NativeModelDraw *draw,struct NativeDrawTriangle *out);
#endif
