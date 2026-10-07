#include <namespace_Mempack.h>
#include <platform.h>
#include <platform/native_gpu_links.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Substitute only the resident state binding and checkpoint notification.
// Allocator, backing arena and GPU token bridge are production sources.
static struct Mempack pools[4];
static struct Mempack *activePack = &pools[0];
static int arenaResets;

struct Mempack **Platform_GetActiveMempackSlot(void)
{
	return &activePack;
}

struct Mempack *Platform_GetMempackPools(void)
{
	return pools;
}

void NativeCheckpoint_OnMempackArenaReset(void)
{
	arenaResets++;
}

void CTR_ErrorScreen(u8 r, u8 g, u8 b)
{
	(void)r;
	(void)g;
	(void)b;
}

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "failed: %s at line %d\n", #condition, __LINE__); \
			return 1; \
		} \
	} while (0)

static int TestArenaAndAllocator(void)
{
	struct HostObject { void *next; long double value; };
	const struct PlatformMempackArena *arena;
	u8 *start;
	u8 *end;
	u8 *low;
	u8 *high;
	s32 mark;

	MEMPACK_Init(0x200000);
	arena = Platform_GetMempackArena();
	start = arena->start;
	end = start + arena->size;
	CHECK(arenaResets == 1);
	CHECK(arena->base == Platform_GetMempackBacking());
	CHECK(arena->backingSize == Platform_GetMempackBackingSize());
	CHECK(start == (u8 *)arena->base + 0xba9f0);
	CHECK(arena->size == 0x144e10);
	CHECK(arena->endOfMemory == (u8 *)arena->base + arena->backingSize);
	CHECK(activePack->endOfMemory == arena->endOfMemory);
	CHECK(activePack->start == start);
	CHECK(activePack->endOfAllocator == end);
	CHECK(MEMPACK_GetFreeBytes() == arena->size);
	CHECK((uintptr_t)start % MEMPACK_ALIGNMENT == 0);
#if defined(__APPLE__) && defined(__aarch64__)
	// Exercise the actual high address that MEMPACK_Init used to truncate.
	CHECK((uintptr_t)start > UINT32_MAX);
#endif

	low = MEMPACK_AllocMem(1, "low");
	high = MEMPACK_AllocHighMem(1, "high");
	CHECK(low == start);
	CHECK(high == end - MEMPACK_ALIGNMENT);
	CHECK((uintptr_t)low % MEMPACK_ALIGNMENT == 0);
	CHECK((uintptr_t)high % MEMPACK_ALIGNMENT == 0);
	CHECK(MEMPACK_GetFreeBytes() == arena->size - 2 * MEMPACK_ALIGNMENT);
	memset(low, 0x5a, MEMPACK_ALIGNMENT);
	memset(high, 0xa5, MEMPACK_ALIGNMENT);
	CHECK(low[0] == 0x5a && high[0] == 0xa5);

	MEMPACK_ClearHighMem();
	CHECK(activePack->lastFreeByte == end);
	CHECK(MEMPACK_ReallocMem(2 * MEMPACK_ALIGNMENT + 1) == start + 3 * MEMPACK_ALIGNMENT);
	CHECK(low[0] == 0x5a);
	CHECK(MEMPACK_ReallocMem(1) == start + MEMPACK_ALIGNMENT);
	CHECK(MEMPACK_GetFreeBytes() == arena->size - MEMPACK_ALIGNMENT);

	mark = MEMPACK_PushState();
	CHECK(mark == 0);
	CHECK(MEMPACK_AllocMem(1, "temporary") == start + MEMPACK_ALIGNMENT);
	MEMPACK_PopState();
	CHECK(activePack->firstFreeByte == start + MEMPACK_ALIGNMENT);
	CHECK(activePack->numBookmarks == 0);
	for (int i = 0; i < MEMPACK_BOOKMARK_COUNT; i++)
	{
		CHECK(MEMPACK_PushState() == i);
		MEMPACK_AllocMem(1, "bookmark");
	}
	CHECK(MEMPACK_PushState() == MEMPACK_BOOKMARK_COUNT);
	MEMPACK_PopToState(0);
	CHECK(activePack->firstFreeByte == start + MEMPACK_ALIGNMENT);
	CHECK(activePack->numBookmarks == 0);
	MEMPACK_ClearLowMem();
	CHECK(activePack->firstFreeByte == start);
	CHECK(MEMPACK_GetFreeBytes() == arena->size);
	CHECK(MEMPACK_AllocMem(arena->size, "full arena") == start);
	CHECK(MEMPACK_GetFreeBytes() == 0);
	MEMPACK_ClearLowMem();
	CHECK(MEMPACK_AllocHighMem(arena->size, "full high arena") == start);
	CHECK(MEMPACK_GetFreeBytes() == 0);
	MEMPACK_ClearHighMem();
	MEMPACK_ClearLowMem();
	{
		struct HostObject *object = MEMPACK_AllocMem((s32)sizeof(*object), "host object");
		object->next = high;
		object->value = 1.25L;
		CHECK(object->next == high && object->value == 1.25L);
	}
	MEMPACK_ClearLowMem();

	MEMPACK_SwapPacks(1);
	CHECK(activePack == &pools[1]);
	MEMPACK_NewPack(start, 4 * MEMPACK_ALIGNMENT);
	CHECK(MEMPACK_AllocMem(1, "subpack") == start);
	MEMPACK_NewPack(start + 1, 4 * MEMPACK_ALIGNMENT);
	CHECK(activePack->start == start + MEMPACK_ALIGNMENT);
	CHECK(activePack->packSize == 3 * MEMPACK_ALIGNMENT);
	CHECK(MEMPACK_AllocMem(1, "unaligned subpack low") == start + MEMPACK_ALIGNMENT);
	CHECK(MEMPACK_AllocHighMem(1, "unaligned subpack high") == start + 3 * MEMPACK_ALIGNMENT);
	CHECK(MEMPACK_GetFreeBytes() == MEMPACK_ALIGNMENT);
	MEMPACK_SwapPacks(0);
	CHECK(activePack == &pools[0]);
	CHECK(MEMPACK_GetFreeBytes() == arena->size);

	MEMPACK_Init(0x200000);
	CHECK(arenaResets == 2);
	CHECK(start[0] == 0 && high[0] == 0);
	CHECK(activePack->start == start);
	CHECK(MEMPACK_GetFreeBytes() == arena->size);
	return 0;
}

static int TestGpuTokens(void)
{
	u8 *backing = Platform_GetMempackBacking();
	size_t size = (size_t)Platform_GetMempackBackingSize();
	u32 token;

	NativeGpuLinks_Reset();
	CHECK(NativeGpuLinks_RegisterRange(backing, size, &token));
	CHECK(token < NATIVE_GPU_LINK_TERMINATOR);
	CHECK(NativeGpuLinks_FromHostPointer(backing) == token);
	CHECK(NativeGpuLinks_ToHostPointer(token) == backing);
	CHECK(NativeGpuLinks_FromHostPointer(backing + size - 1) == token + size - 1);
	CHECK(NativeGpuLinks_ToHostPointer(token + (u32)size - 1) == backing + size - 1);
	CHECK(NativeGpuLinks_ToHostPointer(token | 0xab000000u) == backing);
	CHECK(NativeGpuLinks_IsRegisteredHostRange(backing, size));
	CHECK(!NativeGpuLinks_IsRegisteredHostRange(backing, size + 1));
	CHECK(!NativeGpuLinks_IsRegisteredHostPointer(backing + size));
	CHECK(NativeGpuLinks_ToHostPointer(NATIVE_GPU_LINK_TERMINATOR) == NULL);
	CHECK(!NativeGpuLinks_RegisterRange(NULL, size, NULL));
	CHECK(!NativeGpuLinks_RegisterRange(backing, SIZE_MAX, NULL));
	NativeGpuLinks_Reset();
	CHECK(!NativeGpuLinks_IsRegisteredHostPointer(backing));
	CHECK(NativeGpuLinks_ToHostPointer(token) == NULL);
	return 0;
}

static int TestRejectedAllocation(const char *mode)
{
	MEMPACK_Init(0x200000);
	if (strcmp(mode, "rounded-low") == 0 || strcmp(mode, "rounded-high") == 0)
	{
		MEMPACK_NewPack(activePack->start, MEMPACK_ALIGNMENT - 1);
		if (strcmp(mode, "rounded-low") == 0)
			MEMPACK_AllocMem(1, "must reject rounding beyond capacity");
		else
			MEMPACK_AllocHighMem(1, "must reject rounding beyond capacity");
	}
	else if (strcmp(mode, "negative-low") == 0)
		MEMPACK_AllocMem(-1, "must reject negative size");
	else if (strcmp(mode, "negative-high") == 0)
		MEMPACK_AllocHighMem(-1, "must reject negative size");
	else if (strcmp(mode, "overflow-low") == 0)
		MEMPACK_AllocMem(INT32_MAX, "must reject rounding overflow");
	else if (strcmp(mode, "overflow-high") == 0)
		MEMPACK_AllocHighMem(INT32_MAX, "must reject rounding overflow");
	else if (strcmp(mode, "negative-pack") == 0)
		MEMPACK_NewPack(activePack->start, -1);
	else if (strcmp(mode, "unalignable-pack") == 0)
		MEMPACK_NewPack((u8 *)activePack->start + 1, 1);
	else if (strcmp(mode, "realloc-capacity") == 0)
	{
		MEMPACK_NewPack(activePack->start, 2 * MEMPACK_ALIGNMENT);
		MEMPACK_AllocMem(1, "low");
		MEMPACK_AllocHighMem(1, "high");
		MEMPACK_ReallocMem(2 * MEMPACK_ALIGNMENT);
	}
	else
		return 2;
	return 0; // The driver fails if any invalid allocation reaches this return.
}

int main(int argc, char **argv)
{
	if (argc == 2)
		return TestRejectedAllocation(argv[1]);
	CHECK(TestArenaAndAllocator() == 0);
	CHECK(TestGpuTokens() == 0);
	printf("Native memory tests passed: %zu-bit pointers, alignment %d\n", sizeof(void *) * 8, (int)MEMPACK_ALIGNMENT);
	return 0;
}
