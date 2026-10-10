#include <common.h>
#ifdef CTR_NATIVE_GAME_SCENE
#include <platform/native_scene_assets.h>
#endif

#if CTR_NATIVE_HOST64 && defined(CTR_NATIVE_GAME_SCENE)
static void *LOAD_ResidentRoot(const void *source, enum NativeResidentKind kind)
{
	void *root = NULL;
	if (NativeSceneAssets_Materialize(&gNativeSceneAssets, source, kind, &root) != NATIVE_ASSET_OK)
	{
		Platform_LogError("[CTR Native] Cannot publish resident asset graph\n");
		CTR_TRAP();
	}
	return root;
}
#endif

// NOTE(aalhendi): Qualify selected callback stores to preserve retail's
// return delay slots without changing the shared state layout.
void LOAD_Callback_Overlay_Generic(struct LoadQueueSlot *lqs)
{
	(void)lqs;
	*(volatile int *)&sdata->load_inProgress = 0;
}

void LOAD_Callback_Overlay_230(void)
{
	sdata->load_inProgress = 0;
	GAME_TRACKER->overlayIndex_Threads = OVERLAY_INDEX_MAIN_MENU;
}

void LOAD_Callback_Overlay_231(void)
{
	sdata->load_inProgress = 0;
	GAME_TRACKER->overlayIndex_Threads = OVERLAY_INDEX_RACING_OR_BATTLE;
}

void LOAD_Callback_Overlay_232(void)
{
	sdata->load_inProgress = 0;
	GAME_TRACKER->overlayIndex_Threads = OVERLAY_INDEX_ADV_HUB;
}

void LOAD_Callback_Overlay_233(void)
{
	sdata->load_inProgress = 0;
	GAME_TRACKER->overlayIndex_Threads = OVERLAY_INDEX_PODIUMS;
}

void LOAD_Callback_MaskHints3D(struct LoadQueueSlot *lqs)
{
#if CTR_NATIVE_HOST64 && defined(CTR_NATIVE_GAME_SCENE)
	struct Model *model = LOAD_ResidentRoot(lqs->ptrDestination, NR_MODEL);
#else
	struct Model *model = (struct Model *)lqs->ptrDestination;
#endif

	sdata->load_inProgress = 0;
	*(struct Model *volatile *)&sdata->modelMaskHints3D = model;
}

void LOAD_Callback_Podiums(struct LoadQueueSlot *lqs)
{
#if CTR_NATIVE_HOST64 && defined(CTR_NATIVE_GAME_SCENE)
	struct Model *model = LOAD_ResidentRoot(lqs->ptrDestination, NR_MODEL);
#else
	struct Model *model = (struct Model *)lqs->ptrDestination;
#endif

	sdata->load_inProgress = 0;
	data.podiumModel_podiumStands = model;
}

void LOAD_Callback_LEV(struct LoadQueueSlot *lqs)
{
#ifdef CTR_NATIVE_GAME_SCENE
    struct NativeLevelView sceneLevel;
    if (NativeSceneAssets_GetLevel(&gNativeSceneAssets, lqs->ptrDestination, &sceneLevel) == NATIVE_ASSET_NOT_FOUND &&
        NativeSceneAssets_BeginRaw(&gNativeSceneAssets, lqs->ptrDestination, lqs->size_UNUSED) != NATIVE_PTRMAP_OK)
    { Platform_LogError("[CTR Native] Cannot retain pending LEV\n"); CTR_TRAP(); }
#endif
	if ((lqs->flags & LT_GETADDR) == 0)
	{
		sdata->load_inProgress = 0;
	}

#if CTR_NATIVE_HOST64 && defined(CTR_NATIVE_GAME_SCENE)
	// Separate PTR files complete this pending identity in PatchMem; the load
	// gate remains closed until a validated resident Level is available.
	struct Level *level = (lqs->flags & LT_GETADDR) ? (struct Level *)lqs->ptrDestination : LOAD_ResidentRoot(lqs->ptrDestination, NR_LEVEL);
	*(struct Level *volatile *)&sdata->ptrLevelFile = level;
#else
	*(struct Level *volatile *)&sdata->ptrLevelFile = (struct Level *)lqs->ptrDestination;
#endif
}

void LOAD_Callback_PatchMem(struct LoadQueueSlot *lqs)
{
	// CTR doesn't load one lev DRAM for AdvHub,
	// it loads one ReadFile for LEV in a sub-mempack,
	// it loads one ReadFile for PtrMap with AllocHighMem

	// that's why the patch map is handled here
	struct DramPointerMap *patchMap = lqs->ptrDestination;
#if !CTR_NATIVE_HOST64
	int patchNum;
#endif

	sdata->load_inProgress = 0;
	// NOTE(aalhendi): Retail reads the patch count after clearing the load gate.
#if !CTR_NATIVE_HOST64
	patchNum = patchMap->numBytes >> DRAM_POINTER_MAP_WORD_SHIFT;
#endif
#ifdef CTR_NATIVE_GAME_SCENE
    if (NativeSceneAssets_CompletePtr(&gNativeSceneAssets, sdata->ptrLevelFile, patchMap, lqs->size_UNUSED) != NATIVE_PTRMAP_OK)
    { Platform_LogError("[CTR Native] Cannot complete immutable LEV map\n"); CTR_TRAP(); }
#endif

#if CTR_NATIVE_HOST64 && defined(CTR_NATIVE_GAME_SCENE)
	sdata->ptrLevelFile = LOAD_ResidentRoot(sdata->ptrLevelFile, NR_LEVEL);
#else
	LOAD_RunPtrMap((char *)sdata->ptrLevelFile, DRAM_GETOFFSETS(patchMap), patchNum);
#endif

	MEMPACK_SwapPacks(0);
	MEMPACK_ClearHighMem();
	MEMPACK_SwapPacks(GAME_TRACKER->activeMempackIndex);
}

void LOAD_Callback_DriverModels(struct LoadQueueSlot *lqs)
{
#if CTR_NATIVE_HOST64 && defined(CTR_NATIVE_GAME_SCENE)
	CtrRuntimePointer destination = LOAD_ResidentRoot(lqs->ptrDestination, NR_MPK);
#else
	CtrRuntimePointer destination = (CtrRuntimePointer)lqs->ptrDestination;
#endif

	sdata->load_inProgress = 0;
	*(volatile CtrRuntimePointer *)&sdata->ptrMPK = destination;
}

void LOAD_HubCallback(struct LoadQueueSlot *lqs)
{
	struct GameTracker *gGT;

	*(volatile int *)&sdata->load_inProgress = 0;
	LOAD_Callback_PatchMem(lqs);

	gGT = GAME_TRACKER;
	gGT->level2 = sdata->ptrLevelFile;
	MEMPACK_SwapPacks(gGT->activeMempackIndex);
}
