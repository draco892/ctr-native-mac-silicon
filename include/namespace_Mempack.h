#ifndef CTR_NATIVE_NAMESPACE_MEMPACK_H
#define CTR_NATIVE_NAMESPACE_MEMPACK_H

#include <macros.h>
#if defined(CTR_NATIVE)
#include <platform/native_memory.h>
#endif

enum MempackConstants
{
	MEMPACK_BOOKMARK_COUNT = 0x10,

#if defined(CTR_NATIVE) && UINTPTR_MAX > UINT32_MAX
	MEMPACK_ALIGNMENT = _Alignof(max_align_t),
#else
	MEMPACK_ALIGNMENT = 4,
#endif
	MEMPACK_ALIGNMENT_MASK = MEMPACK_ALIGNMENT - 1,
	MEMPACK_ALIGNMENT_CLEAR_MASK = -MEMPACK_ALIGNMENT,

	MEMPACK_PS1_RAM_ADDRESS_MASK = 0xffffff,
	MEMPACK_PS1_OVERLAY_ALIGNMENT = 0x800,
	MEMPACK_PS1_OVERLAY_ALIGNMENT_MASK = MEMPACK_PS1_OVERLAY_ALIGNMENT - 1,
	MEMPACK_PS1_END_GUARD_SIZE = 0x800,
	MEMPACK_PS1_END_OF_MEMORY = 0x80200000,
};

#define MEMPACK_ALIGN_SIZE(size) (((size) + MEMPACK_ALIGNMENT_MASK) & MEMPACK_ALIGNMENT_CLEAR_MASK)
CTR_STATIC_ASSERT((MEMPACK_ALIGNMENT & MEMPACK_ALIGNMENT_MASK) == 0);

#ifndef MEMPACK_ACTIVE
#if defined(CTR_NATIVE)
#define MEMPACK_ACTIVE (*Platform_GetActiveMempackSlot())
#else
#define MEMPACK_ACTIVE sdata->PtrMempack
#endif
#endif

#ifndef MEMPACK_POOLS
#if defined(CTR_NATIVE)
#define MEMPACK_POOLS (Platform_GetMempackPools())
#else
#define MEMPACK_POOLS sdata->mempack
#endif
#endif

// Field-offset comments describe the 32-bit baseline. Native 64-bit offsets
// are checked separately below; this structure is never decoded from an asset.
struct Mempack
{
	// 0x0
	s32 packSize; // allocator capacity in bytes

	// 0x4
	void *start; // low cursor reset point

	// 0x8
	void *lastFreeByte; // high allocations grow downward from here

	// 0xC
	void *endOfAllocator; // high cursor reset point; NewPack leaves it unchanged

	// 0x10
	void *endOfMemory; // backing limit; Init includes the reserved end-of-RAM sector

	// 0x14
	void *firstFreeByte; // low allocations grow upward from here

	// 0x18
	s32 sizeOfPrevAllocation; // rounded size of the last low or high allocation

	// 0x1C
	s32 numBookmarks; // amount of bookmarks used

	// 0x20
	void *bookmarks[MEMPACK_BOOKMARK_COUNT]; // address of each bookmark

	// 0x60 -- size of struct
};

// MEMPACK is runtime state, not a structure read from a retail asset.
#if defined(CTR_NATIVE) && UINTPTR_MAX > UINT32_MAX
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, packSize) == 0x0);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, start) == 0x8);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, lastFreeByte) == 0x10);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, endOfAllocator) == 0x18);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, endOfMemory) == 0x20);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, firstFreeByte) == 0x28);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, sizeOfPrevAllocation) == 0x30);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, numBookmarks) == 0x34);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, bookmarks) == 0x38);
CTR_STATIC_ASSERT(sizeof(struct Mempack) == 0xb8);
#else
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, packSize) == 0x0);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, start) == 0x4);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, lastFreeByte) == 0x8);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, endOfAllocator) == 0xc);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, endOfMemory) == 0x10);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, firstFreeByte) == 0x14);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, sizeOfPrevAllocation) == 0x18);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, numBookmarks) == 0x1c);
CTR_STATIC_ASSERT(OFFSETOF(struct Mempack, bookmarks) == 0x20);
CTR_STATIC_ASSERT(sizeof(struct Mempack) == 0x60);
#endif

void MEMPACK_Init(s32 ramSize);
void MEMPACK_SwapPacks(s32 index);
void MEMPACK_NewPack(void *start, s32 size);
s32 MEMPACK_GetFreeBytes(void);
void *MEMPACK_AllocMem(s32 size, const char *name);
void *MEMPACK_AllocHighMem(s32 size, const char *name);
void MEMPACK_ClearHighMem(void);
void *MEMPACK_ReallocMem(s32 size);
s32 MEMPACK_PushState(void);
void MEMPACK_ClearLowMem(void);
void MEMPACK_PopState(void);
void MEMPACK_PopToState(s32 id);

#endif
