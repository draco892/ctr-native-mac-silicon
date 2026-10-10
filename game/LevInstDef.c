#include <common.h>

// NOTE(aalhendi): These lists alternate between InstDef* and Instance* in place.
// Retail keeps the peer at 0x2c; host64 uses a shared full-width peer slot.
// Shared PVS lists therefore toggle once
// per reference, just as retail does; do not deduplicate the traversal.
// Alias-qualified slots and byte-safe peer reads support either declared type.
typedef void *LevInstDefLink CTR_MAY_ALIAS;
#if !defined(CTR_NATIVE_HOST64)
CTR_STATIC_ASSERT(offsetof(struct InstDef, ptrInstance) == offsetof(struct Instance, instDef));
#endif

static inline void *LevInstDef_Peer(const void *record)
{
	return INST_DefinitionPeer(record);
}


void LevInstDef_UnPack(struct mesh_info *ptr_mesh_info)
{
	struct QuadBlock *qbCurr;
	struct QuadBlock *qbEnd;
	LevInstDefLink *visInstSrc;
	struct Level *level1;

	qbCurr = ptr_mesh_info->ptrQuadBlockArray;
	qbEnd = qbCurr + ptr_mesh_info->numQuadBlock;

	// loop through all quadblocks
	for (; qbCurr < qbEnd; qbCurr++)
	{
		if ((qbCurr->pvs != 0) && (qbCurr->pvs->visInstSrc != 0))
		{
			// loop through all instance pointers visible on quadblock
			for (visInstSrc = (LevInstDefLink *)qbCurr->pvs->visInstSrc; visInstSrc[0] != NULL; visInstSrc++)
			{
				visInstSrc[0] = LevInstDef_Peer(visInstSrc[0]);
			}
		}
	}

	level1 = GAME_TRACKER->level1;
	visInstSrc = (LevInstDefLink *)level1->ptrInstDefPtrArray;

	if (visInstSrc != NULL)
	{
		// loop through all instDef pointers in the LEV
		for (; visInstSrc[0] != 0; visInstSrc++)
		{
			visInstSrc[0] = LevInstDef_Peer(visInstSrc[0]);
		}
	}
}


void LevInstDef_RePack(struct mesh_info *ptr_mesh_info, b32 boolAdvHub)
{
	struct QuadBlock *qbCurr;
	struct QuadBlock *qbEnd;
	LevInstDefLink *visInstSrc;
	struct Level *level1;
	struct Thread *th;

	qbCurr = ptr_mesh_info->ptrQuadBlockArray;
	qbEnd = qbCurr + ptr_mesh_info->numQuadBlock;

	// loop through all quadblocks
	for (; qbCurr < qbEnd; qbCurr++)
	{
		if ((qbCurr->pvs != 0) && (qbCurr->pvs->visInstSrc != 0))
		{
			// loop through all instance pointers visible on quadblock
			for (visInstSrc = (LevInstDefLink *)qbCurr->pvs->visInstSrc; visInstSrc[0] != NULL; visInstSrc++)
			{
				visInstSrc[0] = LevInstDef_Peer(visInstSrc[0]);
			}
		}
	}

	level1 = GAME_TRACKER->level1;
	visInstSrc = (LevInstDefLink *)level1->ptrInstDefPtrArray;

	if (visInstSrc != NULL)
	{
		// loop through all instDef pointers in the LEV
		for (; visInstSrc[0] != NULL; visInstSrc++)
		{
			struct Instance *inst = visInstSrc[0];
			struct InstDef *instDef = inst->instDef;

			// if on adv hub
			if (boolAdvHub != 0)
			{
				th = inst->thread;
				if (th != 0)
				{
					th->flags |= THREAD_FLAG_DEAD;
				}

				// Return the level-owned instance to the free pool.
				LIST_AddFront(&GAME_TRACKER->JitPools.instance.free, (struct Item *)visInstSrc[0]);
			}

			// go back to instDef
			visInstSrc[0] = instDef;
		}
	}

	PROC_CheckAllForDead();
}
