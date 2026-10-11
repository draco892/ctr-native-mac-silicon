#include <platform/native_scene_assets.h>
#include <namespace_Display.h>
#include <namespace_Mempack.h>
#include <namespace_DrawLevel.h>
#include <ovr_226.h>
#include <ovr_227.h>
#include <ovr_228.h>
#include <ovr_229.h>
#include <ctr_gte.h>
#include <psx/gtereg.h>
#include <prim.h>
#include <gpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COMMON_H
u8 *gCTRNativeScratchpadBase;
// Only the unrelated game aggregate/services are replaced. Geometry, layouts,
// GTE, packet linking, recursive dispatch and all four retail setup tables are real.
struct GameTracker
{
	int gameMode2, levelID, numPlyrCurrGame;
};
static struct
{
	struct GameTracker *gGT;
	struct Mempack mempack[4];
} state;
#define sdata (&state)
static struct
{
	u8 *PtrClipBuffer[4];
	void *ptrRenderedQuadblockDestination_forEachPlayer[4];
} data;
static struct
{
	struct QuadBlock *quadBlocksRendered[512];
} sdata_static;
enum
{
	LEV_SWAP = 0x400
};
static int MainDB_GetClipSize(int level, int players)
{
	(void)level;
	(void)players;
	return 4096;
}
#include "../game/226/R226.c"
#include "../game/227/R227.c"
#include "../game/228/R228.c"
#include "../game/229/R229.c"
#include "../game/226/226_00_DrawLevelOvr1P.c"
#include "../game/227/227_00_DrawLevelOvr2P.c"
#include "../game/228/228_00_DrawLevelOvr3P.c"
#include "../game/229/229_00_DrawLevelOvr4P.c"

#define CHECK(c)                                                 \
	do                                                           \
	{                                                            \
		if (!(c))                                                \
		{                                                        \
			fprintf(stderr, "failed: %s at %d\n", #c, __LINE__); \
			return 1;                                            \
		}                                                        \
	} while (0)
static int WorkspaceAndViewports(void)
{
	_Alignas(max_align_t) u8 retail[1024];
	memset(retail, 0xa5, sizeof(retail));
	gCTRNativeScratchpadBase = retail;
	NativeHostScratch_Reset();
	_Alignas(max_align_t) u8 packets[65536] = {0}, clips[4][16384] = {0};
	u32 ot[4][2048];
	memset(ot, 0xff, sizeof(ot));
	struct QuadBlock block = {0};
	struct mesh_info mesh = {.ptrQuadBlockArray = &block};
	struct PrimMem mem = {.start = packets, .cursor = packets, .end = packets + sizeof(packets), .guardEnd = packets + sizeof(packets)};
	struct PushBuffer pb[4] = {0};
	struct DrawLevelOvr1PRenderList lists[4] = {0};
	struct GameTracker gt = {0};
	state.gGT = &gt;
	int visible = 0;
	for (unsigned i = 0; i < 4; i++)
	{
		data.PtrClipBuffer[i] = clips[i];
		data.ptrRenderedQuadblockDestination_forEachPlayer[i] = sdata_static.quadBlocksRendered;
		pb[i].ptrOT = ot[i];
		pb[i].rect.w = 512;
		pb[i].rect.h = 216;
		pb[i].distanceToScreen_PREV = 256;
		for (unsigned axis = 0; axis < 3; axis++)
			pb[i].matrix_ViewProj.m[axis][axis] = 4096;
	}
	gt.numPlyrCurrGame = 1;
	DrawLevelOvr1P(lists, pb, (struct BSP *)&mesh, &mem, &visible, NULL);
	gt.numPlyrCurrGame = 2;
	DrawLevelOvr2P(lists, pb, (struct BSP *)&mesh, &mem, &visible, &visible, NULL);
	gt.numPlyrCurrGame = 3;
	DrawLevelOvr3P(lists, pb, (struct BSP *)&mesh, &mem, &visible, &visible, &visible, NULL);
	gt.numPlyrCurrGame = 4;
	DrawLevelOvr4P(lists, pb, (struct BSP *)&mesh, &mem, &visible, &visible, &visible, &visible, NULL);
	CHECK(mem.cursor == packets && mem.primitiveCount == 0);
	for (unsigned i = 0; i < 4; i++)
		CHECK(NativeTerrainWork_LoadAddress(&DrawLevelOvr1P_Scratch()->entry.clip.playerClipCursorPtr32[i]) == (uintptr_t)clips[i]);
	CHECK(NativeTerrainWork_LoadAddress(&DrawLevelOvr1P_Scratch()->primMemEndPtr32) == (uintptr_t)mem.end);
	// Both recursive frames are disjoint; the deepest frame retains all 9 records
	// and the selector metadata at +0xb4 inside the bounded payload.
	struct DrawLevelOvr1PScratchVertex *first = DrawLevelOvr1P_GetScratchVertices(), *child = DrawLevelOvr1P_GetSubdivisionFrame(0),
	                                   *deep = DrawLevelOvr1P_GetSubdivisionFrame(1);
	CHECK((u8 *)child - (u8 *)first == 0xb8 && (u8 *)deep - (u8 *)child == 0xb8);
	deep[8].depth = 0x1234;
	DrawLevelOvr1P_SetGridFaceSlotWord(deep, 12);
	CHECK(deep[8].depth == 0x1234 && DrawLevelOvr1P_GetGridFaceSlotWord(deep) == 12);
	for (size_t i = 0; i < sizeof(retail); i++)
		CHECK(retail[i] == 0xa5);
	return 0;
}
static int ResidentTextures(void)
{
	u8 wire[1024] = {0}, ptr[4 + 6 * 4];
	CTR_WriteU32LE(wire + 0xd4, 0x200);
	CTR_WriteU32LE(wire + 0x224, 0x300);
	CTR_WriteU32LE(wire + 8, 0x340);
	CTR_WriteU32LE(wire + 0x340, 0x200);
	CTR_WriteU16LE(wire + 0x344, 1);
	CTR_WriteU32LE(wire + 0x34c, 0x200);
	CTR_WriteU32LE(wire + 0x350, 0x340);
	const u32 slots[] = {8, 0xd4, 0x224, 0x340, 0x34c, 0x350};
	CTR_WriteU32LE(ptr, sizeof(slots));
	for (size_t i = 0; i < 6; i++)
		CTR_WriteU32LE(ptr + 4 + i * 4, slots[i]);
	CHECK(NativeSceneAssets_Capture(&gNativeSceneAssets, wire, sizeof(wire), ptr, sizeof(ptr)) == NATIVE_PTRMAP_OK);
	void *root = NULL;
	CHECK(NativeSceneAssets_Materialize(&gNativeSceneAssets, wire, NR_LEVEL, &root) == NATIVE_ASSET_OK);
	struct Level *level = root;
	struct TextureLayout *texture = level->ptrLowTexArray;
	CHECK(DrawLevelOvr1P_SeedMosaic(texture) == 0x300);
	uintptr_t target = DrawLevelOvr1P_MosaicAddress();
	CHECK(target && target != 0x300 && DrawLevelOvr1P_IsNativeLevelTexturePointer(target));
	CHECK(DrawLevelOvr1P_TreatAsRetailNegativeTextureWord(0x300));
	CHECK(DrawLevelOvr1P_ResolveTexturePointerChecked((uintptr_t)level->ptr_anim_tex | 1u) == texture);
	struct QuadBlock quad = {.ptr_texture_mid = {texture, texture, texture, texture}};
	struct DrawLevelOvr1PScratchVertex *projected = DrawLevelOvr1P_GetScratchVertices();
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 12);
	CHECK(DrawLevelOvr1P_ResolveProjectedMidTexture(&quad, projected) == texture);
	DrawLevelOvr1P_SetGridFaceSlotWord(projected, 16);
	CHECK(DrawLevelOvr1P_ResolveProjectedMidTexture(&quad, projected) == NULL);
	memset(wire, 0xcc, sizeof(wire));
	CHECK(DrawLevelOvr1P_IsNativeLevelTexturePointer(target));
	NativeSceneAssets_Reset(&gNativeSceneAssets);
	return 0;
}
static int GeometryAndPackets(void)
{
	NativeHostScratch_Reset();
	NativeGpuLinks_Reset();
	struct GameTracker gt = {.numPlyrCurrGame = 1};
	state.gGT = &gt;
	MATRIX matrix = {.m = {{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}};
	gte_SetRotMatrix(&matrix);
	gte_SetTransMatrix(&matrix);
	gte_SetGeomOffset(256, 108);
	gte_SetGeomScreen(256);
	DrawLevelOvr1P_Scratch()->depthClipThreshold = 128;
	struct DrawLevelOvr1PScratchVertex *v = DrawLevelOvr1P_GetScratchVertices();
	const s16 xy[4][2] = {{-64, -64}, {64, -64}, {-64, 64}, {64, 64}};
	const int ids[4] = {0, 1, 2, 3};
	for (unsigned i = 0; i < 4; i++)
	{
		v[i].pos[0] = xy[i][0];
		v[i].pos[1] = xy[i][1];
		v[i].pos[2] = 400;
		v[i].flags = 0x2010 + (u16)i * 0x202;
		v[i].color_hi[0] = (u8)(10 + i * 20);
		v[i].color_hi[1] = 100;
		v[i].color_hi[2] = 200;
		CTR_GteLoadS16TripletV0(v[i].pos);
		gte_rtps();
		CTR_GteStoreSXY(v[i].posScreen);
		u32 z;
		gte_stsz(&z);
		v[i].depth = (u16)z;
	}
	struct DrawLevelOvr1PScratchVertex *child = DrawLevelOvr1P_GetSubdivisionFrame(0), *deep = DrawLevelOvr1P_GetSubdivisionFrame(1);
	DrawLevelOvr1P_BuildGridSubdivisionFrame(child, v, ids, 1);
	CHECK(child[4].pos[0] == 0 && child[4].pos[1] == -64 && child[4].pos[2] == 400 && child[4].depth == 400);
	CHECK(child[6].pos[0] == 0 && child[6].pos[1] == 0 && child[6].color_hi[0] == 40);
	Ovr226_800a56f4_BuildGround4x2ListSubdivisionFrame(deep, v, ids);
	CHECK(deep[4].pos[0] == 0 && deep[4].pos[1] == -64);
	Ovr226_800a4594_BuildGround4x1RenderedSubdivisionFrame(deep, v, ids);
	CHECK(deep[4].pos[0] == 0 && deep[8].pos[1] == 64 && deep[4].clipNear == 0);
	Ovr226_800a17d8_BuildFullDynamicSubdivisionFrame(deep, v, ids);
	CHECK(deep[4].pos[0] == 0);
	Ovr226_800a24e8_BuildWaterListSubdivisionFrame(deep, v, ids);
	CHECK(deep[6].pos[0] == 0 && deep[6].pos[1] == 0);
	DrawLevelOvr1P_BuildGridSubdivisionFrame4x4(deep, v, ids, 1);
	CHECK(deep[6].depth == 400);
	DrawLevelOvr1P_RenderScratch()->recursiveNearDepthThreshold = 320;
	CHECK(DrawLevelOvr1P_GetProjectedRecursiveNearMask(v, ids) == 0);
	v[0].depth = 100;
	CHECK(DrawLevelOvr1P_GetProjectedRecursiveNearMask(v, ids) != 0);
	v[0].depth = 400;
	_Alignas(max_align_t) u8 packets[4096] = {0}, clips[16384] = {0};
	u32 ot[2048];
	memset(ot, 0xff, sizeof(ot));
	CHECK(NativeGpuLinks_RegisterRange(packets, sizeof(packets), NULL));
	CHECK(NativeGpuLinks_RegisterRange(ot, sizeof(ot), NULL));
	struct PushBuffer pb = {.ptrOT = ot, .rect = {.w = 512, .h = 216}};
	struct PrimMem mem = {.start = packets, .cursor = packets, .end = packets + sizeof(packets), .guardEnd = packets + sizeof(packets)};
	struct TextureLayout tex = {.clut = 0x1234, .tpage = 0x60};
	struct QuadBlock block = {0};
	DrawLevelOvr1P_Scratch()->uv.uv0.halves.high = tex.clut;
	DrawLevelOvr1P_Scratch()->uv.uv1.halves.high = tex.tpage;
	CHECK(DrawLevelOvr1P_EmitPreparedProjectedTriRawCodeAtOtEntry(&pb, &mem, &block, v, ids, &tex, &ot[7], -1));
	POLY_GT3 *poly = (void *)packets;
	CHECK(poly->code == 0x34 && poly->clut == 0x1234 && poly->tpage == 0x60 && poly->x0 == 215);
	CHECK(NativeGpuLinks_ToHostPointer(ot[7]) == poly && mem.primitiveCount == 1);
	CHECK(DrawLevelOvr1P_EmitPreparedProjectedQuadRawCodeAtOtEntry(&pb, &mem, &block, v, ids, &tex, &ot[8], -1));
	CHECK(((POLY_GT4 *)(packets + sizeof(POLY_GT3)))->code == 0x3c && mem.primitiveCount == 2);
	// Exercise the real copied-grid near dispatcher through both recursive
	// frames. The depth cap must reach a direct primitive rather than frame 3.
	Ovr226_800a0ddc_CopyScratchInitTable();
	Ovr226_800a0e44_ApplyBucketSetup(R226.bucketSetupAddresses[3]);
	DrawLevelOvr1P_Scratch()->drawOrderOrHeader = 0x80000000u;
	DrawLevelOvr1P_Scratch()->clipWindowPacked = 512u | (216u << 16);
	DrawLevelOvr1P_RenderScratch()->recursiveNearDepthThreshold = 320;
	for (unsigned i = 0; i < 4; i++)
	{
		v[i].pos[2] = 100;
		v[i].depth = 100;
	}
	int before = mem.primitiveCount;
	CHECK(DrawLevelOvr1P_DispatchCopiedGridFace(&pb, &mem, &block, v, ids, 0, &tex, 0, 0, 12, 10));
	CHECK(mem.primitiveCount > before && deep[6].depth == 100);
	for (unsigned i = 0; i < 4; i++)
	{
		v[i].pos[2] = 400;
		v[i].depth = 400;
	}
	DrawLevelOvr1P_Scratch()->uv.uv0.halves.high = tex.clut;
	DrawLevelOvr1P_Scratch()->uv.uv1.halves.high = tex.tpage;
	data.PtrClipBuffer[0] = clips;
	DrawLevelOvr1P_SetClipRecordStart(clips);
	DrawLevelOvr1P_SetClipRecordCursor(clips);
	DrawLevelOvr1P_Scratch()->projectedCenter.z = 400;
	CHECK(Ovr226_800a34d4_WriteWaterRenderedClippedRecordAtOtEntry(&pb, v, ids, 4, ot + 9));
	struct DrawLevelOvr1PClipRecord *record = (void *)clips;
	CHECK(DrawLevelOvr1P_DecodeOt(record->otEntry) == ot + 9 && record->header == 0x80000001u && record->clut == 0x1234);
	CHECK(DrawLevelOvr1P_GetClipRecordCursor() == clips + 0x3c);
	return 0;
}
int main(void)
{
	if (WorkspaceAndViewports() || ResidentTextures() || GeometryAndPackets())
		return 1;
	puts("Retail terrain: four viewports, bounded recursive frames, ground/water midpoints, GT3/GT4 and clip OT tokens OK");
	return 0;
}
