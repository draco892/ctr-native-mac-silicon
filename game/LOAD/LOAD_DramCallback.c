#include <common.h>
#ifdef CTR_NATIVE_GAME_SCENE
#include <platform/native_scene_assets.h>
#endif
#ifdef CTR_NATIVE
#include <platform/native_asset_loading.h>
#endif

void LOAD_DramFileCallback(struct LoadQueueSlot *lqs)
{
	char *fileBuf = lqs->ptrDestination;
	void (*callback)(struct LoadQueueSlot *) = lqs->callbackFuncPtr;

	if (fileBuf != NULL)
	{
#if defined(CTR_NATIVE)
		struct NativeDramLayout nativeLayout;
		// size_UNUSED is the actual BIGFILE entry size. Validate before either
		// patching pointers or shrinking away the embedded PTR allocation.
		if (!NativeAssetLoad_CheckDram(fileBuf, lqs->size_UNUSED, &nativeLayout))
		{
			Platform_LogError("[CTR Native] Invalid DRAM file or pointer map\n");
			CTR_TRAP();
		}
		int ptrMapOffset = nativeLayout.ptr == NULL ? -1 : (int)nativeLayout.payloadBytes;
#ifdef CTR_NATIVE_GAME_SCENE
		// Preserve wire ownership before legacy consumers patch pointer words.
		enum NativePtrMapResult sceneStatus =
		    nativeLayout.ptr != NULL
		        ? NativeSceneAssets_Capture(&gNativeSceneAssets, nativeLayout.payload, nativeLayout.payloadBytes, nativeLayout.ptr, nativeLayout.ptrBytes)
		        : NativeSceneAssets_BeginRaw(&gNativeSceneAssets, nativeLayout.payload, nativeLayout.payloadBytes);
		if (sceneStatus != NATIVE_PTRMAP_OK)
		{
			Platform_LogError("[CTR Native] Cannot retain immutable asset\n");
			CTR_TRAP();
		}
#endif
#else
		int ptrMapOffset = *(int *)&fileBuf[0];
#endif
#if !CTR_NATIVE_HOST64
		char *realFileBuf = &fileBuf[4];
#endif

		if (ptrMapOffset >= 0)
		{
#if !CTR_NATIVE_HOST64
			struct DramPointerMap *dpm = (struct DramPointerMap *)&realFileBuf[ptrMapOffset];
			LOAD_RunPtrMap(realFileBuf, (int *)DRAM_GETOFFSETS(dpm), dpm->numBytes >> 2);
#endif

#if defined(CTR_NATIVE)
			if ((lqs->flags & LT_MEMPACK) != 0)
#else
			if ((lqs->flags & LT_SETADDR) != 0)
#endif
			{
				MEMPACK_ReallocMem(ptrMapOffset + 4);
			}
		}
		else
		{
			lqs->flags |= LT_GETADDR;
		}

		lqs->ptrDestination = &fileBuf[4];
#if defined(CTR_NATIVE)
		// Downstream callbacks receive bytes at ptrDestination, excluding both
		// the DRAM prefix and any embedded PTR. Raw read callbacks retain eSize.
		lqs->size_UNUSED = (u32)nativeLayout.payloadBytes;
#endif
	}

#if defined(CTR_NATIVE)
	// NOTE(aalhendi): CTR_NATIVE keeps host callback pointers and queue sentinels.
	if ((callback != NULL) && (callback != LOAD_DramFileCallback) && (callback != (void (*)(struct LoadQueueSlot *)) - 1) &&
	    (callback != LOAD_QUEUE_CALLBACK_SET_POINTER))
#else
	if ((callback != NULL) && (((u32)(u32)callback & 0xff000000) == 0x80000000))
#endif
	{
		callback(lqs);
	}

	sdata->queueReady = 1;
}
