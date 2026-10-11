#include <common.h>


void JitPool_Clear(struct JitPool *AP)
{
	u32 *currSlot = AP->ptrPoolData;
	u32 loopIndex;

	// clear list of free and taken
	LIST_Clear(&AP->free);
	LIST_Clear(&AP->taken);

	for (loopIndex = 0; loopIndex < (u32)AP->maxItems; loopIndex++)
	{
		// add all pool items to the free list
		LIST_AddFront(&AP->free, (struct Item *)currSlot);

		// NOTE(aalhendi): Retail truncates the record stride to whole words.
		currSlot += AP->itemSize / sizeof(*currSlot);
	}
}


void JitPool_Init(struct JitPool *AP, int maxItems, int itemSize, char *name)
{
#if defined(CTR_NATIVE)
	// Every slot starts with an intrusive Item and can hold host pointers.
	const size_t alignment = CTR_RUNTIME_ABI(sizeof(u32), _Alignof(max_align_t));
	if (maxItems < 0 || itemSize < (int)sizeof(struct Item) ||
	    (size_t)itemSize > (size_t)INT32_MAX - (alignment - 1))
		CTR_TRAP();
	itemSize = (int)(((size_t)itemSize + alignment - 1) & ~(alignment - 1));
	if (maxItems && itemSize > INT32_MAX / maxItems)
		CTR_TRAP();
#endif
	memset(AP, 0, sizeof(struct JitPool));
	AP->maxItems = maxItems;
	AP->itemSize = itemSize;
	AP->poolSize = maxItems * itemSize;
	AP->ptrPoolData = MEMPACK_AllocMem(AP->poolSize, name);
	JitPool_Clear(AP);
}


CtrRuntimePointer JitPool_Add(struct JitPool *AP)
{
	struct Item *item = AP->free.first;
	CtrRuntimePointer result;

	if (item == 0)
	{
		result = 0;
	}
	else
	{
		LIST_RemoveMember(&AP->free, item);
		LIST_AddFront(&AP->taken, item);
		result = (CtrRuntimePointer)item;
	}

	return result;
}


void JitPool_Remove(struct JitPool *AP, struct Item *item)
{
	LIST_RemoveMember(&AP->taken, item);
	LIST_AddFront(&AP->free, item);
}
