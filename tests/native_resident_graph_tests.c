#include <platform/native_scene_assets.h>
#include <namespace_Load.h>
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
static void Fixture(u8 *wire, u8 *ptr)
{
	memset(wire, 0, 1024);
	CTR_WriteU32LE(wire + 0xc, 1);
	CTR_WriteU32LE(wire + 0x10, 0x280);
	CTR_WriteU32LE(wire + 0x14, 1);
	CTR_WriteU32LE(wire + 0x18, 0x208);
	CTR_WriteU32LE(wire + 0x200, 0x280);
	CTR_WriteU32LE(wire + 0x208, 0x240);
	memcpy(wire + 0x240, "fixture", 7);
	CTR_WriteU16LE(wire + 0x250, 17);
	CTR_WriteU32LE(wire + 0x290, 0x240);
	const u32 slots[] = {0x10, 0x18, 0x200, 0x208, 0x290};
	CTR_WriteU32LE(ptr, sizeof(slots));
	for (size_t i = 0; i < 5; i++)
		CTR_WriteU32LE(ptr + 4 + i * 4, slots[i]);
}
struct Rebase
{
	uintptr_t sources[2], targets[2];
};
static int RebaseSources(void *user, u64 saved, u32 bytes, uintptr_t *live)
{
	struct Rebase *r = user;
	for (unsigned i = 0; i < 2; i++)
		if (saved >= r->sources[i] && saved - r->sources[i] <= 1024 && bytes <= 1024 - (saved - r->sources[i]))
		{
			*live = r->targets[i] + (uintptr_t)(saved - r->sources[i]);
			return 1;
		}
	return 0;
}
static int EmptyHitboxes(void)
{
	// Retail termination occupies four bytes, not another whole BSP record.
	u8 wire[580] = {0}, ptr[16];
	CTR_WriteU32LE(wire, 512);
	CTR_WriteU32LE(wire + 512 + 24, 544);
	CTR_WriteU32LE(wire + 512 + 28, 1);
	CTR_WriteU16LE(wire + 544, 1);
	CTR_WriteU32LE(wire + 544 + 20, 576);
	const u32 slots[] = {0, 536, 564};
	CTR_WriteU32LE(ptr, 12);
	for (size_t i = 0; i < 3; i++)
		CTR_WriteU32LE(ptr + 4 + i * 4, slots[i]);
	struct NativePtrMapEntry entries[3];
	struct NativePtrMapView map;
	CHECK(NativePtrMap_Decode(wire, sizeof(wire), ptr, sizeof(ptr), entries, 3, &map) == NATIVE_PTRMAP_OK);
	struct NativeResidentGraph *g = NULL;
	CHECK(NativeResidentGraph_Build(&map, NR_LEVEL, 0, &g, NULL) == NATIVE_ASSET_OK);
	struct Level *level = NativeResidentGraph_Root(g);
	CHECK(level->ptr_mesh_info->bspRoot->data.leaf.bspHitboxArray->flag == 0);
	NativeResidentGraph_Free(g);
	return 0;
}
#define COMMON_H
struct GameTracker
{
	int overlayIndex_Threads, activeMempackIndex;
	struct Level *level2;
};
static struct GameTracker tracker;
static struct
{
	int load_inProgress, queueReady;
	struct Level *ptrLevelFile;
	struct Model *modelMaskHints3D;
	CtrRuntimePointer ptrMPK;
} state;
#define sdata        (&state)
#define GAME_TRACKER (&tracker)
static struct
{
	struct Model *podiumModel_podiumStands;
} data;
enum
{
	OVERLAY_INDEX_MAIN_MENU = 230,
	OVERLAY_INDEX_RACING_OR_BATTLE = 231,
	OVERLAY_INDEX_ADV_HUB = 232,
	OVERLAY_INDEX_PODIUMS = 233
};
static unsigned shrinks, highClears;
static void Platform_LogError(const char *format, ...)
{
	(void)format;
}
static void MEMPACK_ReallocMem(int bytes)
{
	if (bytes != 1028)
		abort();
	shrinks++;
}
static void MEMPACK_SwapPacks(int index)
{
	(void)index;
}
static void MEMPACK_ClearHighMem(void)
{
	highClears++;
}
void LOAD_DramFileCallback(struct LoadQueueSlot *);
#include "../game/LOAD/LOAD_Callbacks.c"
#include "../game/LOAD/LOAD_DramCallback.c"
static int LoaderCallbacks(void)
{
	u8 file[4 + 1024 + 24];
	CTR_WriteU32LE(file, 1024);
	Fixture(file + 4, file + 1028);
	u8 original[sizeof(file)];
	memcpy(original, file, sizeof(file));
	struct LoadQueueSlot queue = {.ptrDestination = file, .size_UNUSED = sizeof(file), .flags = LT_MEMPACK, .callbackFuncPtr = LOAD_Callback_LEV};
	struct LoadQueueSlot copied = queue;
	CHECK(copied.callbackFuncPtr == LOAD_Callback_LEV && copied.ptrDestination == file && sizeof(copied) > 24);
	state.load_inProgress = 1;
	LOAD_DramFileCallback(&copied);
	CHECK(shrinks == 1 && state.queueReady == 1 && state.load_inProgress == 0 && copied.ptrDestination == file + 4 && copied.size_UNUSED == 1024);
	CHECK(state.ptrLevelFile != (void *)(file + 4) && state.ptrLevelFile->numModels == 1 && !memcmp(original, file, sizeof(file)));
	// Same callback sequence with a separate PTR file, as used by AdvHub/menu.
	u8 raw[1024], ptr[24];
	Fixture(raw, ptr);
	queue = (struct LoadQueueSlot){.ptrDestination = raw, .size_UNUSED = 1024, .flags = LT_GETADDR};
	state.load_inProgress = 1;
	LOAD_Callback_LEV(&queue);
	CHECK(state.ptrLevelFile == (void *)raw && state.load_inProgress == 1);
	queue = (struct LoadQueueSlot){.ptrDestination = ptr, .size_UNUSED = sizeof(ptr)};
	LOAD_Callback_PatchMem(&queue);
	CHECK(state.load_inProgress == 0 && highClears == 1 && state.ptrLevelFile != (void *)raw && state.ptrLevelFile->numModels == 1);
	u8 pack[16] = {0};
	CTR_WriteU32LE(pack, 8);
	queue = (struct LoadQueueSlot){.ptrDestination = pack, .size_UNUSED = sizeof(pack), .callbackFuncPtr = LOAD_Callback_DriverModels};
	LOAD_DramFileCallback(&queue);
	struct NativeModelPack *mpk = state.ptrMPK;
	CHECK(mpk && mpk != (void *)(pack + 4) && mpk->models[0] == NULL && mpk->icons == NULL);
	u8 model[32] = {0};
	CTR_WriteU32LE(model, 24);
	CTR_WriteU16LE(model + 4 + 16, 17);
	queue = (struct LoadQueueSlot){.ptrDestination = model, .size_UNUSED = sizeof(model), .callbackFuncPtr = LOAD_Callback_MaskHints3D};
	LOAD_DramFileCallback(&queue);
	CHECK(state.modelMaskHints3D && state.modelMaskHints3D->id == 17 && state.modelMaskHints3D != (void *)(model + 4));
	queue.callbackFuncPtr = LOAD_Callback_Podiums;
	LOAD_Callback_Podiums(&queue);
	CHECK(data.podiumModel_podiumStands == state.modelMaskHints3D);
	NativeSceneAssets_Reset(&gNativeSceneAssets);
	return 0;
}
int main(void)
{
	u8 wire[1024], ptr[24], original[1024];
	Fixture(wire, ptr);
	memcpy(original, wire, sizeof(wire));
	struct NativePtrMapEntry entries[5];
	struct NativePtrMapView map;
	CHECK(NativePtrMap_Decode(wire, sizeof(wire), ptr, sizeof(ptr), entries, 5, &map) == NATIVE_PTRMAP_OK);
	struct NativeResidentGraph *graph = NULL;
	CHECK(NativeResidentGraph_Build(&map, NR_LEVEL, 0, &graph, NULL) == NATIVE_ASSET_OK);
	struct Level *level = NativeResidentGraph_Root(graph);
	CHECK(level->numModels == 1 && level->ptrModelsPtrArray[0] == level->ptrInstDefs[0].model);
	CHECK(!memcmp(original, wire, sizeof(wire)) && level != (void *)wire);
	u32 offset;
	CHECK(NativeResidentGraph_WireOffset(graph, level->ptrModelsPtrArray[0], NR_MODEL, &offset) && offset == 0x240);
	CHECK(NativeResidentGraph_Contains(graph, level, sizeof(*level)));
	size_t bytes = NativeResidentGraph_CheckpointSize(graph);
	u8 *blob = malloc(bytes);
	CHECK(blob);
	CHECK(NativeResidentGraph_CaptureCheckpoint(graph, blob, bytes));
	struct NativeResidentGraph *copy = NULL;
	CHECK(NativeResidentGraph_RestoreCheckpoint(blob, bytes, &copy, NULL, NULL));
	struct Level *copied = NativeResidentGraph_Root(copy);
	CHECK(copied != level && copied->ptrInstDefs[0].model == copied->ptrModelsPtrArray[0]);
	void *rebased = NULL;
	CHECK(NativeResidentGraph_RebaseSavedPointer(blob, bytes, copy, (uintptr_t)level, &rebased) && rebased == copied);
	struct NativeResidentGraph *unchanged = copy;
	CHECK(!NativeResidentGraph_RestoreCheckpoint(blob, bytes - 1, &copy, NULL, NULL) && copy == unchanged);
	size_t objectAt = 48 + sizeof(wire) + 4 + 5 * 4;
	size_t levelPayload = objectAt + 24;
	u32 originalCount = CTR_ReadU32LE(blob + levelPayload + offsetof(struct Level, numModels));
	CTR_WriteU32LE(blob + levelPayload + offsetof(struct Level, numModels), UINT32_MAX);
	CHECK(!NativeResidentGraph_RestoreCheckpoint(blob, bytes, &copy, NULL, NULL) && copy == unchanged);
	CTR_WriteU32LE(blob + levelPayload + offsetof(struct Level, numModels), originalCount);
	blob[12] ^= 1;
	CHECK(!NativeResidentGraph_RestoreCheckpoint(blob, bytes, &copy, NULL, NULL) && copy == unchanged);
	blob[12] ^= 1;
	blob[bytes - 24] ^= 1;
	CHECK(!NativeResidentGraph_RestoreCheckpoint(blob, bytes, &copy, NULL, NULL) && copy == unchanged);
	blob[bytes - 24] ^= 1;
	CHECK(NativeResidentGraph_Build(&map, NR_BYTES, 0, &copy, NULL) == NATIVE_ASSET_INVALID_ARGUMENT && copy == unchanged);
	CTR_WriteU32LE(wire + 0x290, 1024);
	entries[4].targetOffset = 1024;
	CHECK(NativeResidentGraph_Build(&map, NR_LEVEL, 0, &copy, NULL) != NATIVE_ASSET_OK && copy == unchanged);
	Fixture(wire, ptr);
	NativeResidentGraph_Free(graph);
	NativeResidentGraph_Free(copy);
	free(blob);
	// Owner publication waits for the separate PTR, survives source lifetime,
	// and restores two mutually referencing graphs at new addresses.
	struct NativeSceneAssets owners = {0};
	void *root = (void *)(uintptr_t)1;
	CHECK(NativeSceneAssets_BeginRaw(&owners, wire, sizeof(wire)) == NATIVE_PTRMAP_OK);
	CHECK(NativeSceneAssets_Materialize(&owners, wire, NR_LEVEL, &root) == NATIVE_ASSET_NOT_FOUND && root == (void *)(uintptr_t)1);
	CHECK(NativeSceneAssets_CompletePtr(&owners, wire, ptr, sizeof(ptr)) == NATIVE_PTRMAP_OK);
	CHECK(NativeSceneAssets_Materialize(&owners, wire, NR_LEVEL, &root) == NATIVE_ASSET_OK);
	level = root;
	u8 other[1024];
	Fixture(other, ptr);
	CHECK(NativeSceneAssets_Capture(&owners, other, sizeof(other), ptr, sizeof(ptr)) == NATIVE_PTRMAP_OK);
	CHECK(NativeSceneAssets_Materialize(&owners, other, NR_LEVEL, &root) == NATIVE_ASSET_OK);
	struct Level *peer = root;
	struct Model *a = level->ptrModelsPtrArray[0], *b = peer->ptrModelsPtrArray[0];
	level->ptrModelsPtrArray[0] = b;
	peer->ptrModelsPtrArray[0] = a;
	struct NativeLevelView lv;
	struct NativeModelView mv;
	CHECK(NativeSceneAssets_GetLevel(&owners, level, &lv) == NATIVE_ASSET_OK);
	CHECK(NativeSceneAssets_GetModel(&owners, b, &mv) == NATIVE_ASSET_OK && mv.id == 17);
	CHECK(NativeModelLibrary_StoreModel(&owners.library, &mv) == NATIVE_ASSET_OK);
	bytes = NativeSceneAssets_CheckpointSize(&owners);
	blob = malloc(bytes);
	CHECK(blob);
	CHECK(NativeSceneAssets_CaptureCheckpoint(&owners, blob, bytes));
	struct Rebase r = {{(uintptr_t)wire, (uintptr_t)other}, {0x100000000ull, 0x200000000ull}};
	struct NativeSceneAssets restored = {0};
	CHECK(NativeSceneAssets_RestoreCheckpoint(&restored, blob, bytes, RebaseSources, &r));
	void *newA, *newB;
	CHECK(NativeSceneAssets_RebaseSavedPointer(blob, bytes, &restored, (uintptr_t)a, &newA));
	CHECK(NativeSceneAssets_RebaseSavedPointer(blob, bytes, &restored, (uintptr_t)b, &newB));
	CHECK(newA != a && newB != b && NativeSceneAssets_Contains(&restored, newB, sizeof(struct Model)));
	CHECK(NativeSceneAssets_Materialize(&restored, (void *)r.targets[0], NR_LEVEL, &root) == NATIVE_ASSET_OK);
	CHECK(((struct Level *)root)->ptrModelsPtrArray[0] == newB);
	CHECK(NativeSceneAssets_Materialize(&restored, (void *)r.targets[1], NR_LEVEL, &root) == NATIVE_ASSET_OK);
	CHECK(((struct Level *)root)->ptrModelsPtrArray[0] == newA);
	CHECK(!NativeSceneAssets_RestoreCheckpoint(&restored, blob, bytes - 1, RebaseSources, &r));
	CHECK(NativeSceneAssets_GetModel(&restored, newA, &mv) == NATIVE_ASSET_OK);
	CHECK(NativeModelLibrary_Get(&restored.library, 17, &mv) == NATIVE_ASSET_OK);
	memset(wire, 0xcc, sizeof(wire));
	memset(other, 0xcc, sizeof(other));
	CHECK(NativeSceneAssets_GetModel(&owners, a, &mv) == NATIVE_ASSET_OK);
	NativeSceneAssets_ForgetRange(&owners, wire, wire + sizeof(wire));
	CHECK(!NativeSceneAssets_Contains(&owners, a, 1));
	NativeSceneAssets_Reset(&owners);
	NativeSceneAssets_Reset(&restored);
	free(blob);
	if (EmptyHitboxes() || LoaderCallbacks())
		return 1;
	puts("Owned resident graphs: sharing, immutable sources, pending PTR, cross-owner checkpoints and rejection OK");
	return 0;
}
