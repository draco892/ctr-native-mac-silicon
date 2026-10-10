#ifndef CTR_RENDER_WORK_H
#define CTR_RENDER_WORK_H
#include <macros.h>
enum MainRenderLevelGeometryScratchConstants
{
	MAIN_RENDER_LEVEL_GEOMETRY_FULL_DYNAMIC_FADE_OFFSET = 0x140,
};

// NOTE(aalhendi): Retail seeds scratchpad 0x14..0x2c here before RenderLists/226;
// later 226 paths can reuse part of this range for split-ground thresholds.
struct MainRenderLevelGeometryScratch
{
	u8 reserved0[0x14];

	s32 depthScale;
	s32 bspLodDistanceThreshold;
	s32 textureLodDepthThreshold0;
	s32 textureLodDepthThreshold1;
	s32 topLevelNearDepthThreshold;
	s32 recursiveNearDepthThreshold;
	s32 fullDynamicFadeDepthStart;
};

CTR_STATIC_ASSERT(offsetof(struct MainRenderLevelGeometryScratch, depthScale) == 0x14);
CTR_STATIC_ASSERT(offsetof(struct MainRenderLevelGeometryScratch, bspLodDistanceThreshold) == 0x18);
CTR_STATIC_ASSERT(offsetof(struct MainRenderLevelGeometryScratch, textureLodDepthThreshold0) == 0x1c);
CTR_STATIC_ASSERT(offsetof(struct MainRenderLevelGeometryScratch, textureLodDepthThreshold1) == 0x20);
CTR_STATIC_ASSERT(offsetof(struct MainRenderLevelGeometryScratch, topLevelNearDepthThreshold) == 0x24);
CTR_STATIC_ASSERT(offsetof(struct MainRenderLevelGeometryScratch, recursiveNearDepthThreshold) == 0x28);
CTR_STATIC_ASSERT(offsetof(struct MainRenderLevelGeometryScratch, fullDynamicFadeDepthStart) == 0x2c);
CTR_STATIC_ASSERT(sizeof(struct MainRenderLevelGeometryScratch) == 0x30);

#endif
