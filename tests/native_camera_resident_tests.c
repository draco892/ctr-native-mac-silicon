#include <platform/native_resident.h>
#include <ctr_gte.h>
#include <psx/gtereg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)                                                 \
	do                                                           \
	{                                                            \
		if (!(c))                                                \
		{                                                        \
			fprintf(stderr, "failed: %s at %d\n", #c, __LINE__); \
			return 1;                                            \
		}                                                        \
	} while (0)
#define COMMON_H
static struct
{
	u32 trigApprox[1024];
} data;
static const s16 trigs[1024][2] = {
#include "../platform/native_trig_table.inc"
};
u8 *gCTRNativeScratchpadBase;
#include "../game/MATH/MATH_7_MatrixStubs.c"
#include "../game/PushBuffer_Core.c"
// Run the real alternating PVS traversal with only unrelated services stubbed.
struct Thread
{
	u32 flags;
};
enum
{
	THREAD_FLAG_DEAD = 0x800
};
static struct
{
	struct Level *level1;
	struct
	{
		struct
		{
			struct LinkedList free;
		} instance;
	} JitPools;
} tracker;
#define GAME_TRACKER (&tracker)
static void LIST_AddFront(struct LinkedList *list, struct Item *item)
{
	(void)list;
	(void)item;
	abort();
}
static void PROC_CheckAllForDead(void)
{
}
#include "../game/LevInstDef.c"
static int Viewports(void)
{
	_Alignas(max_align_t) u8 scratch[1024];
	memset(scratch, 0, sizeof(scratch));
	gCTRNativeScratchpadBase = scratch;
	for (unsigned i = 0; i < 1024; i++)
		data.trigApprox[i] = (u16)trigs[i][0] | ((u32)(u16)trigs[i][1] << 16);
	struct PushBuffer pb[4];
	u32 ot[4][16];
	for (int total = 1; total <= 4; total++)
		for (int id = 0; id < total; id++)
		{
			memset(&pb[id], 0xa5, sizeof(pb[id]));
			pb[id].ptrOT = ot[id];
			pb[id].renderBucketOTRangeEnd = ot[id] + 16;
			PushBuffer_Init(&pb[id], id, total);
			CHECK(pb[id].cameraID == id && pb[id].ptrOT == ot[id] && pb[id].renderBucketOTRangeEnd == ot[id] + 16);
			CHECK(pb[id].rect.w == (total <= 2 ? 512 : 253) && pb[id].rect.h == (total == 1 ? 216 : 106));
			CHECK(pb[id].rect.x == (total >= 3 && (id & 1) ? 259 : 0));
			CHECK(pb[id].rect.y == ((total == 2 && id == 1) || (total >= 3 && id >= 2) ? 110 : 0));
			CHECK(pb[id].aspectX == (total == 2 ? 8 : 4) && pb[id].aspectY == 3);
			CHECK(pb[id].distanceToScreen_PREV == (total <= 2 ? 256 : 128) && pb[id].distanceToScreen_CURR == pb[id].distanceToScreen_PREV);
			CHECK(pb[id].matrix_Proj.m[0][0] == 0x1c71 && pb[id].matrix_Proj.m[1][1] == 4096 && pb[id].matrix_Proj.t[2] == 0);
			pb[id].rot = (SVec3){0};
			pb[id].pos = (SVec3){31, -47, 83};
			memset(&gteRegs, 0, sizeof(gteRegs));
			PushBuffer_UpdateFrustum(&pb[id]);
			CHECK(pb[id].matrix_Camera.m[0][0] == 4096 && pb[id].matrix_Camera.m[1][1] == 4096 && pb[id].matrix_Camera.m[2][2] == 4096);
			CHECK(pb[id].matrix_Camera.t[0] == 31 && pb[id].matrix_Camera.t[1] == -47 && pb[id].matrix_Camera.t[2] == 83);
			CHECK(pb[id].matrix_CameraTranspose.t[0] == -31 && pb[id].matrix_CameraTranspose.t[1] == 47 && pb[id].matrix_CameraTranspose.t[2] == -83);
			CHECK(pb[id].matrix_ViewProj.m[1][1] == 2304 && pb[id].matrix_ViewProj.t[1] == 26);
			CHECK(pb[id].bbox.min.x <= 31 && pb[id].bbox.max.x >= 31 && pb[id].bbox.min.z <= 83 && pb[id].bbox.max.z >= 83);
			CHECK(PushBuffer_GetFrustumSavedCameraZ() == 83);
			for (unsigned f = 0; f < 6; f++)
				CHECK(pb[id].RenderListJmpIndex[f] >= 0 && pb[id].RenderListJmpIndex[f] < 8);
			CHECK(pb[id].ptrOT == ot[id] && pb[id].renderBucketOTRangeEnd == ot[id] + 16);
			PushBuffer_SetPsyqGeom(&pb[id]);
			CHECK(gteRegs.CP2C.p[26].d == (u32)pb[id].distanceToScreen_PREV);
		}
	u32 random = 0x5a8de;
	for (unsigned trial = 0; trial < 2000; trial++)
	{
		struct PushBuffer *p = &pb[trial % 4];
		for (unsigned i = 0; i < 3; i++)
		{
			random = random * 1664525u + 1013904223u;
			s16 value = (s16)random;
			memcpy((u8 *)&p->rot + i * 2, &value, 2);
		}
		p->pos = (SVec3){(s16)(trial - 1000), (s16)(trial / 3), -273};
		MATRIX expected = {0};
		ConvertRotToMatrix(&expected, &p->rot);
		PushBuffer_UpdateFrustum(p);
		CHECK(memcmp(expected.m, p->matrix_Camera.m, sizeof(expected.m)) == 0);
		for (unsigned i = 0; i < 3; i++)
			for (unsigned j = 0; j < 3; j++)
				CHECK(p->matrix_CameraTranspose.m[i][j] == expected.m[j][i]);
		CHECK(p->ptrOT == ot[trial % 4]);
	}
	return 0;
}
static int Peers(void)
{
	struct InstDef definitions[2] = {0};
	size_t size = sizeof(struct Instance) + 4 * sizeof(struct InstDrawPerPlayer);
	struct Instance *instances[2] = {calloc(1, size), calloc(1, size)};
	CHECK(instances[0] && instances[1]);
	for (unsigned i = 0; i < 2; i++)
	{
		INST_LinkDefinition(instances[i], &definitions[i]);
		CHECK(INST_DefinitionPeer(&definitions[i]) == instances[i] && INST_DefinitionPeer(instances[i]) == &definitions[i]);
		for (unsigned player = 0; player < 4; player++)
		{
			INST_GETIDPP(instances[i])[player].ptrDeltaArray = (CtrRuntimeAddress)&definitions[i];
			INST_GETIDPP(instances[i])[player].instFlags = (int)(0x100 + player);
		}
		CHECK(INST_GETIDPP(instances[i])[3].ptrDeltaArray == (CtrRuntimeAddress)&definitions[i]);
	}
	void *list[3] = {&definitions[0], &definitions[1], NULL};
	struct PVS pvs = {.visInstSrc = (struct Instance **)list};
	struct QuadBlock quads[2] = {{.pvs = &pvs}, {.pvs = &pvs}};
	struct mesh_info mesh = {.numQuadBlock = 2, .ptrQuadBlockArray = quads};
	struct Level level = {0};
	tracker.level1 = &level;
	LevInstDef_UnPack(&mesh); // Shared list toggles twice, preserving retail semantics.
	CHECK(list[0] == &definitions[0] && list[1] == &definitions[1]);
	mesh.numQuadBlock = 1;
	LevInstDef_UnPack(&mesh);
	CHECK(list[0] == instances[0] && list[1] == instances[1]);
	LevInstDef_RePack(&mesh, 0);
	CHECK(list[0] == &definitions[0]);
	INST_LinkDefinition(instances[0], NULL);
	CHECK(INST_DefinitionPeer(instances[0]) == NULL);
	struct Driver driver;
	memset(&driver, 0xa5, sizeof(driver));
	driver.ghostTape = (struct GhostTape *)instances[1];
	driver.ghostID = 17;
	memset(&driver, 0, DRIVER_RUNTIME_BASE_SIZE);
	CHECK(driver.ghostTape == (struct GhostTape *)instances[1] && driver.ghostID == 17 && driver.funcPtrs[0] == NULL);
	CHECK(sizeof(driver) > DRIVER_NTSC_RETAIL_SIZE && DRIVER_RUNTIME_BASE_SIZE == offsetof(struct Driver, ghostTape));
	struct SpawnType1 *spawn = calloc(1, sizeof(*spawn) + 2 * sizeof(void *));
	struct IconGroup *icons = calloc(1, sizeof(*icons) + 2 * sizeof(struct Icon *));
	CHECK(spawn && icons);
	CHECK((uintptr_t)ST1_GETPOINTERS(spawn) % _Alignof(void *) == 0);
	CHECK((uintptr_t)ICONGROUP_GETICONS(icons) % _Alignof(void *) == 0);
	ST1_GETPOINTERS(spawn)[1] = instances[1];
	ICONGROUP_GETICONS(icons)[1] = (struct Icon *)instances[0];
	CHECK(ST1_GETPOINTERS(spawn)[1] == instances[1] && ICONGROUP_GETICONS(icons)[1] == (struct Icon *)instances[0]);
	CHECK(DRIVER_RUNTIME_POOL_BYTES % _Alignof(max_align_t) == 0 && DRIVER_RUNTIME_POOL_BYTES - sizeof(struct Item) > sizeof(struct Driver));
	free(spawn);
	free(icons);
	free(instances[0]);
	free(instances[1]);
	return 0;
}
int main(void)
{
	if (Viewports() || Peers())
		return 1;
	printf("Resident camera: 1–4 viewports, matrices/frustum (2000 rotations), full-width peers/PVS and driver initialization OK; PushBuffer=%zu CameraDC=%zu "
	       "Driver=%zu Instance=%zu IDPP=%zu Level=%zu\n",
	       sizeof(struct PushBuffer), sizeof(struct CameraDC), sizeof(struct Driver), sizeof(struct Instance), sizeof(struct InstDrawPerPlayer),
	       sizeof(struct Level));
	return 0;
}
