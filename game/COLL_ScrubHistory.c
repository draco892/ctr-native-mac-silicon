#include <common.h>

enum
{
	COLL_SCRUB_DEPTH_REPEAT_STEP = 0x100,
	COLL_SCRUB_DEPTH_REPEAT_LIMIT = 0x401,
	COLL_HITBOX_SCRUB_DEPTH_BONUS = 0x200,
	COLL_MOVED_PLAYER_HIT_RADIUS = 0x19,
};

void COLL_MOVED_FindScrub(struct QuadBlock *qb, s32 triangleID, struct ScratchpadStruct *sps)
{
#if defined(CTR_NATIVE_HOST64)
    struct BspSearchTriangle *history = sps->scrubHistory;
    s32 *count = &sps->scrubCount;
#else
    struct ScratchpadStructExtended *ext = (struct ScratchpadStructExtended *)sps;
    struct BspSearchTriangle *history = ext->bspSearchTriangle;
    s32 *count = &ext->numTriangles;
#endif
	u16 searchFlags = sps->Union.QuadBlockColl.searchFlags;

	if (qb == NULL)
	{
		sps->Union.QuadBlockColl.searchFlags = searchFlags & ~COLL_SEARCH_REPEAT_SCRUB;
		sps->Input1.scrubDepth = 0;
		(*count) = 0;
		return;
	}

	if ((*count) < 0 || (*count) > 15) CTR_TRAP();
	for (s32 i = (*count) - 1; i >= 0; i--)
	{
		struct BspSearchTriangle *tri = &history[i];

		if ((tri->quadblock == qb) && (tri->triangleID == triangleID))
		{
			s32 scrubDepth = tri->scrubDepth;
			s16 scrub = scrubDepth;

			if (scrubDepth < COLL_SCRUB_DEPTH_REPEAT_LIMIT)
			{
				scrubDepth = CTR_MipsAddLo(scrubDepth, COLL_SCRUB_DEPTH_REPEAT_STEP);
				scrub = scrubDepth;
				tri->scrubDepth = scrubDepth;
			}

			sps->Union.QuadBlockColl.searchFlags = searchFlags | COLL_SEARCH_REPEAT_SCRUB;
			sps->Input1.scrubDepth = scrub;
			return;
		}
	}

	// A sweep can add at most fifteen entries; never write beyond its history.
	if ((*count) < 0 || (*count) >= 15) CTR_TRAP();
	{
		struct BspSearchTriangle *tri = &history[(*count)];

		tri->quadblock = qb;
		tri->triangleID = triangleID;
		tri->scrubDepth = 0;
	}

	sps->Union.QuadBlockColl.searchFlags = searchFlags & ~COLL_SEARCH_REPEAT_SCRUB;
	sps->Input1.scrubDepth = 0;
	(*count)++;
}

