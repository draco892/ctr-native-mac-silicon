#include <platform.h>
#include <namespace_Mempack.h>
#include <platform/native_memory.h>
#if defined(CTR_INTERNAL)
#include <platform/native_checkpoint.h>
#endif

#include <string.h>

// Host64 resident graphs, intrusive pools and render queues contain expanded
// pointers and records. They cannot share the retail 2 MiB memory budget.
// Serialized asset addresses retain their separate 32-bit decoding contract.
#if defined(CTR_NATIVE_HOST64)
#define CTR_NATIVE_MEMPACK_BUFFER_SIZE  0x800000u
#else
#define CTR_NATIVE_MEMPACK_BUFFER_SIZE  0x200000u
#endif
#define CTR_NATIVE_MEMPACK_START_OFFSET 0xba9f0u
#define CTR_NATIVE_MEMPACK_SIZE (CTR_NATIVE_MEMPACK_BUFFER_SIZE - CTR_NATIVE_MEMPACK_START_OFFSET - MEMPACK_PS1_END_GUARD_SIZE)

CTR_STATIC_ASSERT(CTR_NATIVE_MEMPACK_START_OFFSET + CTR_NATIVE_MEMPACK_SIZE + MEMPACK_PS1_END_GUARD_SIZE == CTR_NATIVE_MEMPACK_BUFFER_SIZE);
CTR_STATIC_ASSERT(CTR_NATIVE_MEMPACK_START_OFFSET % MEMPACK_ALIGNMENT == 0);
CTR_STATIC_ASSERT(CTR_NATIVE_MEMPACK_SIZE % MEMPACK_ALIGNMENT == 0);

_Alignas(max_align_t) global_variable u8 s_mempackMemory[CTR_NATIVE_MEMPACK_BUFFER_SIZE];
global_variable struct PlatformMempackArena s_mempackArena;

void Platform_ConfigureMempackArena(void)
{
	s_mempackArena.base = &s_mempackMemory[0];
	s_mempackArena.start = &s_mempackMemory[CTR_NATIVE_MEMPACK_START_OFFSET];
	s_mempackArena.endOfMemory = &s_mempackMemory[CTR_NATIVE_MEMPACK_BUFFER_SIZE];
	s_mempackArena.size = CTR_NATIVE_MEMPACK_SIZE;
	s_mempackArena.backingSize = CTR_NATIVE_MEMPACK_BUFFER_SIZE;
}

const struct PlatformMempackArena *Platform_InitMempackArena(void)
{
	memset(s_mempackMemory, 0, sizeof(s_mempackMemory));
	Platform_ConfigureMempackArena();
#if defined(CTR_INTERNAL)
	NativeCheckpoint_OnMempackArenaReset();
#endif
	return &s_mempackArena;
}

const struct PlatformMempackArena *Platform_GetMempackArena(void)
{
	return &s_mempackArena;
}

void *Platform_GetMempackBacking(void)
{
	return &s_mempackMemory[0];
}

int Platform_GetMempackBackingSize(void)
{
	return (int)sizeof(s_mempackMemory);
}
