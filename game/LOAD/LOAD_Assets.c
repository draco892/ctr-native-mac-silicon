#include <common.h>

#if defined(CTR_NATIVE)
#include <platform/native_lng.h>
#include <platform/native_log.h>
#endif

#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
#include <platform/native_checkpoint.h>
#endif

void LOAD_RunPtrMap(char *origin, int *patchArr, int numPtrs)
{
	int *ptrCurrOffset = patchArr;
	int *ptrEndOffset = &patchArr[numPtrs];

	for (; ptrCurrOffset < ptrEndOffset; ptrCurrOffset++)
	{
		int offset = (*ptrCurrOffset >> 2) << 2;
		// NOTE(aalhendi): Retail adds the aligned offset before the 32-bit base address.
#if defined(CTR_NATIVE)
		// Legacy native consumers still require four-byte host-pointer slots.
		// ARM64 readers use NativePtrMap_Decode/Resolve with explicit asset sizes.
		CTR_STATIC_ASSERT(sizeof(void *) == 4);
		u8 *location = (u8 *)(void *)(origin + offset);
		CTR_WriteU32LE(location, CTR_ReadU32LE(location) + (u32)(uintptr_t)origin);
#else
		int *location = (int *)((u32)offset + (u32)origin);
		*location = *location + (int)origin;
#endif
#if defined(CTR_NATIVE) && defined(CTR_INTERNAL)
		NativeCheckpoint_RegisterPointerSlot((char *)location);
#endif
	}
}

void LOAD_Robots2P(struct BigHeader *bigfile, int p1, int p2, void (*callback)(struct LoadQueueSlot *))
{
	int setIndex = 0;
	int setOffset;
	int racerIndex;
	s16 *characterIDs = GAME_CHARACTER_IDS;
	u8 *robotSetBase = GAME_2P_AI_SETS[0];
	// NOTE(aalhendi): Retail keeps the set pointer in t1 while scanning via a separate table offset.
	register u8(*robotSet)[LOAD_2P_AI_SET_RACER_COUNT] CTR_PSX_REGISTER("$9") = GAME_2P_AI_SETS;
	b32 boolFoundRepeat;
	setOffset = 0;

	// 8 sets, but only check 7 because the last is the Gem Cups pack (4 bosses).
	for (; setIndex < LOAD_2P_AI_SET_COUNT; robotSet++, setIndex++, setOffset += LOAD_2P_AI_SET_RACER_COUNT)
	{
		boolFoundRepeat = false;
		racerIndex = 0;
		do
		{
			// NOTE(aalhendi): The offset-first 32-bit address keeps retail's two-add order.
			u8 racerID = *(u8 *)((u32)(racerIndex + setOffset) + (u32)robotSetBase);
			if ((p1 == racerID) || (p2 == racerID))
			{
				boolFoundRepeat = true;
			}
			racerIndex++;
		} while ((racerIndex < LOAD_2P_AI_SET_RACER_COUNT) && !boolFoundRepeat);

		if (!boolFoundRepeat)
		{
			characterIDs[2] = (*robotSet)[0];
			characterIDs[3] = (*robotSet)[1];
			characterIDs[4] = (*robotSet)[2];
			characterIDs[5] = (*robotSet)[3];

			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_2PARCADEPACK + setIndex, NULL, callback);
			return;
		}
	}
}

void LOAD_Robots1P(int characterID)
{
	int slotIndex = 1;
	int candidateID;
	s16 *slot;

	GAME_CHARACTER_IDS[0] = characterID;
	candidateID = 0;
	slot = &GAME_CHARACTER_IDS[1];

	// NOTE(aalhendi): Retail scans candidate IDs and advances the slot only for IDs kept.
	for (; candidateID < LOAD_CHARACTER_ID_COUNT; candidateID++)
	{
		if (slotIndex >= LOAD_CHARACTER_ID_COUNT)
		{
			break;
		}

		if (candidateID != characterID)
		{
			*slot++ = candidateID;
			slotIndex++;
		}
	}
}

static void (*const LOAD_DriverMPK_SetPointer)(struct LoadQueueSlot *) = LOAD_QUEUE_CALLBACK_SET_POINTER;

// NOTE(aalhendi): Retail queues fixed LOD slots and pack files in branch-local calls.
CtrRuntimePointer LOAD_DriverMPK(struct BigHeader *bigfile, int levelLOD, void (*callback)(struct LoadQueueSlot *))
{
	int gameMode1;
	struct GameTracker *gGT;

	// 3P/4P
	if ((u32)(levelLOD - LOAD_LEVEL_LOD_3P) < LOAD_LEVEL_LOD_3P4P_COUNT)
	{
		// low lod CTR models
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELLOW + GAME_CHARACTER_IDS[0], &GAME_DRIVER_MODEL_EXTRAS[0].fileBase, LOAD_DriverMPK_SetPointer);
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELLOW + GAME_CHARACTER_IDS[1], &GAME_DRIVER_MODEL_EXTRAS[1].fileBase, LOAD_DriverMPK_SetPointer);
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELLOW + GAME_CHARACTER_IDS[2], &GAME_DRIVER_MODEL_EXTRAS[2].fileBase, LOAD_DriverMPK_SetPointer);

		// load 4P MPK of fourth player
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_4PARCADEPACK + GAME_CHARACTER_IDS[3], NULL, callback);
		return sdata->ptrMPK;
	}

	if (levelLOD == LOAD_LEVEL_LOD_1P)
	{
		gGT = GAME_TRACKER;
		gameMode1 = gGT->gameMode1;
		if ((gameMode1 & (TIME_TRIAL | MAIN_MENU)) == TIME_TRIAL)
		{
			goto CheckHighAndPack;
		}

		if (
		    // adv/cutscene mpk when we just need text from MPK
		    ((gameMode1 & (GAME_CUTSCENE | ADVENTURE_ARENA)) != 0) ||

		    // credits
		    ((gGT->gameMode2 & CREDITS) != 0) ||

		    // adventure character select
		    (gGT->levelID == ADVENTURE_GARAGE))
		{
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_ADVENTUREPACK + GAME_CHARACTER_IDS[0], NULL, callback);
			return sdata->ptrMPK;
		}

		if ((gameMode1 & ADVENTURE_BOSS) != 0)
		{
			goto LoadHighAndPack;
		}

		if (
		    // If you are in Adventure cup
		    ((gameMode1 & ADVENTURE_CUP) != 0) &&

		    // purple gem cup
		    (gGT->cup.cupID == 4))
		{
			// high lod model
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELHI + GAME_CHARACTER_IDS[0], &GAME_DRIVER_MODEL_EXTRAS[0].fileBase, LOAD_DriverMPK_SetPointer);

			// pack of four AIs with bosses
			LOAD_AppendQueue(bigfile, LT_GETADDR, BI_2PARCADEPACK + LOAD_PURPLE_GEM_CUP_AI_SET_INDEX, NULL, callback);

			GAME_CHARACTER_IDS[1] = RIPPER_ROO;
			GAME_CHARACTER_IDS[2] = PAPU_PAPU;
			GAME_CHARACTER_IDS[3] = KOMODO_JOE;
			GAME_CHARACTER_IDS[4] = PINSTRIPE;

			return sdata->ptrMPK;
		}

		// NOTE(aalhendi): Retail rereads the tracker slot before deciding whether to choose AI drivers.
		if ((GAME_TRACKER_RELOAD()->gameMode1 & (TIME_TRIAL | MAIN_MENU)) != MAIN_MENU)
		{
			LOAD_Robots1P(GAME_CHARACTER_IDS[0]);
		}

		// arcade mpk
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_1PARCADEPACK + GAME_CHARACTER_IDS[0], NULL, callback);
		return sdata->ptrMPK;
	}

// NOTE(aalhendi): Time trial rechecks the high-LOD condition; boss races enter its body directly.
CheckHighAndPack:
	if ((levelLOD == LOAD_LEVEL_LOD_RELIC) || ((GAME_TRACKER->gameMode1 & TIME_TRIAL) != 0))
	{
	LoadHighAndPack:
		// Do NOT switch the order to optimize Relic,
		// if HI+IDs[1] and PACK+IDs[0] is loaded,
		// then mask-grab breaks for all characters
		// on Hot Air Skyway (except Crash Bandicoot)

		// Load Player 1 [0]
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELHI + GAME_CHARACTER_IDS[0], &GAME_DRIVER_MODEL_EXTRAS[0].fileBase, LOAD_DriverMPK_SetPointer);

		// Load boss or ghost [1]
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_TIMETRIALPACK + GAME_CHARACTER_IDS[1], NULL, callback);
		return sdata->ptrMPK;
	}

	// 2P and other standard LODs
	else
	{
		// med models
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELMED + GAME_CHARACTER_IDS[0], &GAME_DRIVER_MODEL_EXTRAS[0].fileBase, LOAD_DriverMPK_SetPointer);
		LOAD_AppendQueue(bigfile, LT_GETADDR, BI_RACERMODELMED + GAME_CHARACTER_IDS[1], &GAME_DRIVER_MODEL_EXTRAS[1].fileBase, LOAD_DriverMPK_SetPointer);

		LOAD_Robots2P(bigfile, GAME_CHARACTER_IDS[0], GAME_CHARACTER_IDS[1], callback);
		return sdata->ptrMPK;
	}
}

#if !defined(CTR_NATIVE)
struct LngFile
{
	int numStrings;
	int offsetToPtrArr;
	char strings[1];
};
#endif

// param_1 - Pointer to "cd position of bigfile"
// param_2 - language index - 0 ja, 1 en, 2 en2, 3 fr, 4 de, 5 it, 6 es, 7 ne
void LOAD_LangFile(CTR_LNG_BIGFILE_TYPE bigfilePtr, int lang)
{
#if defined(CTR_NATIVE)
	struct NativeLngStorageLayout layout;
	struct BigEntry *entry;
	size_t fileCapacity;
	u32 size = 0;
	u32 count;
	char **strings;
	void *file;
	enum NativeLngResult result;

	if (bigfilePtr == NULL)
		bigfilePtr = sdata->ptrBigfile1;
	if (bigfilePtr == NULL || lang < 0 || lang >= 8 ||
	    bigfilePtr->numEntry <= BI_LANGUAGEFILE + lang || sdata->langBufferSize < NATIVE_LNG_HEADER_SIZE)
	{
		Platform_LogError("[CTR Native] Invalid LNG load request\n");
		CTR_TRAP();
	}
	entry = BIG_GETENTRY(bigfilePtr);
	entry += BI_LANGUAGEFILE + lang;
	// Reject oversized files before LOAD_ReadFile_ex writes full CD sectors.
	if (entry->size < NATIVE_LNG_HEADER_SIZE || entry->size > sdata->langBufferSize)
	{
		Platform_LogError("[CTR Native] LNG file exceeds language buffer bounds\n");
		CTR_TRAP();
	}
	fileCapacity = ((size_t)sdata->langBufferSize + LOAD_CD_DATA_SECTOR_ROUND_MASK) & ~(size_t)LOAD_CD_DATA_SECTOR_ROUND_MASK;
	result = NativeLng_GetStorageLayout(fileCapacity, &layout);
	if (result != NATIVE_LNG_OK || layout.allocationSize > INT32_MAX)
	{
		Platform_LogError("[CTR Native] Invalid LNG storage size\n");
		CTR_TRAP();
	}
	if (sdata->lngFile == NULL)
		sdata->lngFile = MEMPACK_AllocMem((s32)layout.allocationSize, "language file and host table");
	strings = (char **)((u8 *)sdata->lngFile + layout.tableOffset);
	// The read overwrites the old file. Publish the new table only after all
	// bounds and terminators are validated; reuse the same allocation on reload.
	sdata->lngStrings = NULL;
	sdata->numLngStrings = 0;
	memset(strings, 0, layout.stringCapacity * sizeof(*strings));
	file = LOAD_ReadFile_ex(bigfilePtr, LT_SETADDR, BI_LANGUAGEFILE + lang, sdata->lngFile, &size, NULL);
	if (file == NULL || size > (u32)sdata->langBufferSize)
	{
		Platform_LogError("[CTR Native] LNG file read failed\n");
		CTR_TRAP();
	}
	result = NativeLng_Decode(file, size, strings, layout.stringCapacity, &count);
	if (result == NATIVE_LNG_OK && count == 0)
		result = NATIVE_LNG_INVALID_COUNT;
	if (result != NATIVE_LNG_OK)
	{
		Platform_LogError("[CTR Native] LNG decoding failed: %s\n", NativeLng_ResultString(result));
		CTR_TRAP();
	}
	sdata->lngStrings = strings;
	sdata->numLngStrings = (s32)count;
#else
	struct LngFile *lngFile;
	u32 size;

	int i;
	int numStrings;
	char **strArray;


	if (sdata->lngFile == 0)
	{
		sdata->lngFile = MEMPACK_AllocMem(sdata->langBufferSize, NULL /* "lang buffer" */);
	}

	lngFile = sdata->lngFile;

	lngFile = LOAD_ReadFile_ex((struct BigHeader *)bigfilePtr, LT_SETADDR, BI_LANGUAGEFILE + lang, lngFile, &size, NULL);
	if (lngFile == NULL)
	{
		return;
	}

	numStrings = lngFile->numStrings;
	strArray = (char **)((u32)lngFile + lngFile->offsetToPtrArr);

	sdata->numLngStrings = numStrings;
	sdata->lngStrings = strArray;

	for (i = 0; i < numStrings; i++)
	{
		strArray[i] = (char *)((u32)strArray[i] + (u32)lngFile);
	}
#endif
}

// NOTE(aalhendi): Variable-stride group bases come last to preserve retail's return scheduling.
int LOAD_GetBigfileIndex(u32 levelID, int lod, int fileIndexInGroup)
{
	if (levelID < NITRO_COURT)
	{
		return BI_ARCADETRACKS + levelID * LOAD_TRACK_FILES_PER_LOD_GROUP + GAME_LEVEL_BIG_LOD_INDEX[lod - 1] + fileIndexInGroup;
	}

	if ((u32)(levelID - NITRO_COURT) < LOAD_BATTLE_TRACK_COUNT)
	{
		return (levelID - NITRO_COURT) * LOAD_TRACK_FILES_PER_LOD_GROUP + GAME_LEVEL_BIG_LOD_INDEX[lod - 1] + fileIndexInGroup + BI_BATTLETRACKS;
	}

	if ((u32)(levelID - INTRO_RACE_TODAY) < LOAD_INTRO_CUTSCENE_COUNT)
	{
		return (levelID - INTRO_RACE_TODAY) * LOAD_CUTSCENE_FILES_PER_LEVEL + fileIndexInGroup + BI_CUTSCENES_INTRO;
	}

	if ((u32)(levelID - OXIDE_ENDING) < LOAD_OUTRO_CUTSCENE_COUNT)
	{
		return (levelID - OXIDE_ENDING) * LOAD_OUTRO_FILES_PER_LEVEL + fileIndexInGroup + BI_CUTSCENES_OUTRO;
	}

	if (levelID == ADVENTURE_GARAGE)
	{
		return BI_MAINMENUFILE + LOAD_MAIN_MENU_GARAGE_FILE_OFFSET + fileIndexInGroup;
	}

	if (levelID == NAUGHTY_DOG_CRATE)
	{
		return BI_NDBOX + fileIndexInGroup;
	}

	if ((u32)(levelID - CREDITS_CRASH) < LOAD_CREDIT_LEVEL_COUNT)
	{
		return (levelID - CREDITS_CRASH) * LOAD_CUTSCENE_FILES_PER_LEVEL + fileIndexInGroup + BI_CREDITS;
	}

	if (levelID == MAIN_MENU_LEVEL)
	{
		return BI_MAINMENUFILE + fileIndexInGroup;
	}

	if (levelID == SCRAPBOOK)
	{
		return BI_SCRAPBOOK + fileIndexInGroup;
	}

	return (levelID - GEM_STONE_VALLEY) * LOAD_CUTSCENE_FILES_PER_LEVEL + fileIndexInGroup + BI_ADVENTUREHUB;
}
