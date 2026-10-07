#if defined(CTR_NATIVE)
#include <namespace_Mempack.h>
#include <platform.h>
#include <stdio.h>

void CTR_ErrorScreen(u8 r, u8 g, u8 b);

static s32 MEMPACK_NativeAlignSize(s32 size)
{
	if ((size < 0) || (size > INT32_MAX - MEMPACK_ALIGNMENT_MASK))
	{
		CTR_TRAP();
	}
	return MEMPACK_ALIGN_SIZE(size);
}
#else
#include <common.h>
#endif


void MEMPACK_Init(s32 ramSize)
{
#if defined(CTR_NATIVE)
	// NOTE(aalhendi): Native uses host-backed RAM; PSX reserves the arena after the largest overlay.
	s32 packSize;
	const struct PlatformMempackArena *arena = Platform_InitMempackArena();
	(void)ramSize;

	packSize = arena->size;

	printf("[CTR] MEMPACK native backing: base=%p\n", arena->base);

	MEMPACK_NewPack(arena->start, packSize);
	MEMPACK_ACTIVE->endOfAllocator = (u8 *)arena->start + packSize;
	MEMPACK_ACTIVE->endOfMemory = arena->endOfMemory;

	printf("[CTR] MEMPACK native arena: start=%p size=%08x end=%p\n", arena->start, (u32)packSize, MEMPACK_ACTIVE->endOfAllocator);

#else
	u32 startPtr;
	u32 maxOverlayEnd;
	struct Mempack *ptrMempack;
	register u32 addressMask CTR_PSX_REGISTER("$3");
	register u32 overlayBase CTR_PSX_REGISTER("$4");

	// NOTE(aalhendi): Keep the two candidate lifetimes through the join so GCC retains retail's max-selection branches.
	register u32 overlayEndA CTR_PSX_REGISTER("$4") = (u32)AH_EndOfFile;
	register u32 overlayEndB CTR_PSX_REGISTER("$3") = (u32)RB_EndOfFile;
	if (overlayEndA < overlayEndB)
	{
		overlayEndA = (u32)MM_EndOfFile;
		if (overlayEndA >= overlayEndB)
			maxOverlayEnd = overlayEndA;
		else
			maxOverlayEnd = overlayEndB;
	}
	else
	{
		overlayEndB = (u32)MM_EndOfFile;
		CTR_PSX_KEEP_VALUE_RELAXED(overlayEndB);
		if (overlayEndB >= overlayEndA)
			maxOverlayEnd = overlayEndB;
		else
			maxOverlayEnd = overlayEndA;
	}
	CTR_PSX_OBSERVE_VALUE(overlayEndA);
	CTR_PSX_OBSERVE_VALUE(overlayEndB);
	overlayEndB = (u32)CS_EndOfFile;
	if (overlayEndB >= maxOverlayEnd)
		maxOverlayEnd = overlayEndB;
	CTR_PSX_OBSERVE_VALUE(overlayEndB);

	// Round the overlay footprint up to a CD sector, relative to its load address.
	// Leave the final sector of RAM outside the allocator.
	addressMask = MEMPACK_PS1_RAM_ADDRESS_MASK;
	overlayBase = (u32)OVR_Region3;
	startPtr = overlayBase + ((((maxOverlayEnd - overlayBase) + MEMPACK_PS1_OVERLAY_ALIGNMENT_MASK) >> 11) << 11);
	addressMask &= startPtr;
	ramSize -= addressMask;
	ramSize -= MEMPACK_PS1_END_GUARD_SIZE;

	ptrMempack = MEMPACK_ACTIVE;
	ptrMempack->start = (void *)startPtr;
	ptrMempack->endOfAllocator = (void *)(startPtr + ramSize);
	ptrMempack->lastFreeByte = (void *)(startPtr + ramSize);
	ptrMempack->packSize = ramSize;
	ptrMempack->numBookmarks = 0;
	ptrMempack->endOfMemory = (void *)MEMPACK_PS1_END_OF_MEMORY;
	ptrMempack->firstFreeByte = ptrMempack->start;
#endif
}


void MEMPACK_SwapPacks(s32 index)
{
	MEMPACK_ACTIVE = &MEMPACK_POOLS[index];
	// NOTE(aalhendi): Retail publishes the active pack before the return delay slot.
	CTR_PSX_OBSERVE_MEMORY(MEMPACK_ACTIVE);
}


void MEMPACK_NewPack(void *start, s32 size)
{
#if defined(CTR_NATIVE)
	uintptr_t padding;
	if ((start == NULL) || (size < 0))
	{
		CTR_TRAP();
	}
	// Subpacks can arrive with arbitrary bounds. Align inward so both allocation
	// cursors stay aligned without exposing bytes outside the supplied window.
	padding = (-(uintptr_t)start) & (uintptr_t)MEMPACK_ALIGNMENT_MASK;
	if (padding > (uintptr_t)size)
	{
		CTR_TRAP();
	}
	start = (u8 *)start + padding;
	size = (size - (s32)padding) & MEMPACK_ALIGNMENT_CLEAR_MASK;
#endif
	struct Mempack *ptrMempack = MEMPACK_ACTIVE;
	ptrMempack->start = start;
	// NOTE(aalhendi): Preserve retail's start-pointer readback instead of forwarding the argument.
	CTR_PSX_RELOAD(ptrMempack->start);
	start = (u8 *)start + size;
	ptrMempack->lastFreeByte = start;
	ptrMempack->packSize = size;
	ptrMempack->numBookmarks = 0;
	ptrMempack->endOfMemory = start;
	ptrMempack->firstFreeByte = ptrMempack->start;
}


inline s32 MEMPACK_GetFreeBytes(void)
{
	struct Mempack *ptrMempack = MEMPACK_ACTIVE;

#if defined(CTR_NATIVE)
	return (s32)((u8 *)ptrMempack->lastFreeByte - (u8 *)ptrMempack->firstFreeByte);
#else
	return (u32)ptrMempack->lastFreeByte - (u32)ptrMempack->firstFreeByte;
#endif
}


void *MEMPACK_AllocMem(s32 allocSize, const char *name)
{
	struct Mempack *ptrMempack;
	register s32 newAllocSize CTR_PSX_REGISTER("$5");
	void *firstFreeByte;
	u8 *cursor;
	(void)name;

#if defined(CTR_NATIVE)
	newAllocSize = MEMPACK_NativeAlignSize(allocSize);
	if (MEMPACK_GetFreeBytes() < newAllocSize)
#else
	if (MEMPACK_GetFreeBytes() < allocSize)
#endif
	{
		CTR_ErrorScreen(0xFF, 0, 0);
		for (;;)
		{
			CTR_TRAP();
		}
	}

	// NOTE(aalhendi): Keep rounding in a1 while the old cursor remains available for the return value.
#if !defined(CTR_NATIVE)
	newAllocSize = allocSize + MEMPACK_ALIGNMENT_MASK;
	CTR_PSX_KEEP_VALUE_RELAXED(newAllocSize);
	newAllocSize &= MEMPACK_ALIGNMENT_CLEAR_MASK;
#endif
	ptrMempack = MEMPACK_ACTIVE;
	ptrMempack->sizeOfPrevAllocation = newAllocSize;

	cursor = ptrMempack->firstFreeByte;
	firstFreeByte = (void *)cursor;
	cursor += newAllocSize;
	ptrMempack->firstFreeByte = (void *)cursor;

	return (void *)firstFreeByte;
}


void *MEMPACK_AllocHighMem(s32 allocSize, const char *name)
{
	u8 *newLastFreeByte;
	s32 newAllocSize;
	struct Mempack *ptrMempack;
	(void)name;

#if defined(CTR_NATIVE)
	newAllocSize = MEMPACK_NativeAlignSize(allocSize);
	if (MEMPACK_GetFreeBytes() < newAllocSize)
#else
	if (MEMPACK_GetFreeBytes() < allocSize)
#endif
	{
		for (;;)
		{
			CTR_TRAP();
		}
	}

#if !defined(CTR_NATIVE)
	newAllocSize = MEMPACK_ALIGN_SIZE(allocSize);
#endif
	ptrMempack = MEMPACK_ACTIVE;
	ptrMempack->sizeOfPrevAllocation = newAllocSize;

	newLastFreeByte = (u8 *)ptrMempack->lastFreeByte - newAllocSize;
	ptrMempack->lastFreeByte = (void *)newLastFreeByte;

	return (void *)newLastFreeByte;
}


void MEMPACK_ClearHighMem(void)
{
	struct Mempack *ptrMempack = MEMPACK_ACTIVE;
	ptrMempack->lastFreeByte = ptrMempack->endOfAllocator;
}


void *MEMPACK_ReallocMem(s32 allocSize)
{
	struct Mempack *ptrMempack = MEMPACK_ACTIVE;

	// Resize the last low allocation in place; the return value is its new end, not its start.
#if defined(CTR_NATIVE)
	s32 newAllocSize = MEMPACK_NativeAlignSize(allocSize);
	if ((newAllocSize - ptrMempack->sizeOfPrevAllocation) > MEMPACK_GetFreeBytes())
	{
		CTR_TRAP();
	}
#else
	s32 newAllocSize = MEMPACK_ALIGN_SIZE(allocSize);
#endif
	ptrMempack->firstFreeByte = (void *)((u8 *)ptrMempack->firstFreeByte - ptrMempack->sizeOfPrevAllocation + newAllocSize);
	ptrMempack->sizeOfPrevAllocation = newAllocSize;

	return ptrMempack->firstFreeByte;
}


s32 MEMPACK_PushState(void)
{
	struct Mempack *ptrMempack = MEMPACK_ACTIVE;
	s32 numBookmarks = ptrMempack->numBookmarks;
	if (numBookmarks < MEMPACK_BOOKMARK_COUNT)
	{
		ptrMempack->bookmarks[numBookmarks] = ptrMempack->firstFreeByte;
		ptrMempack->numBookmarks = numBookmarks + 1;
		return numBookmarks;
	}

	return numBookmarks;
}


void MEMPACK_ClearLowMem(void)
{
	struct Mempack *ptrMempack = MEMPACK_ACTIVE;

	ptrMempack->numBookmarks = 0;
	ptrMempack->firstFreeByte = ptrMempack->start;
}


void MEMPACK_PopState(void)
{
	struct Mempack *ptrMempack = MEMPACK_ACTIVE;
	s32 numBookmarks = ptrMempack->numBookmarks;
	if (numBookmarks > 0)
	{
		numBookmarks--;
		ptrMempack->numBookmarks = numBookmarks;
		ptrMempack->firstFreeByte = ptrMempack->bookmarks[numBookmarks];
	}
}


void MEMPACK_PopToState(s32 id)
{
	struct Mempack *ptrMempack = MEMPACK_ACTIVE;

	ptrMempack->numBookmarks = id;
	ptrMempack->firstFreeByte = ptrMempack->bookmarks[id];
}
