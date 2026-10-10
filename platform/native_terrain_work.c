#include <platform/native_terrain_work.h>
#include <platform/native_checkpoint.h>
#include <string.h>
enum
{
	TERRAIN_POINTER_SLOTS = CTR_SCRATCHPAD_SIZE / sizeof(u32)
};
void *NativeTerrainWork_At(size_t offset, size_t bytes, size_t alignment)
{
	if (offset > CTR_SCRATCHPAD_SIZE || bytes > CTR_SCRATCHPAD_SIZE - offset || !alignment || offset % alignment)
		CTR_TRAP();
	u8 *base = NativeHostScratch_Get(NATIVE_HOST_SCRATCH_TERRAIN_PAYLOAD, CTR_SCRATCHPAD_SIZE, _Alignof(max_align_t));
	if (!base || (uintptr_t)(base + offset) % alignment)
		CTR_TRAP();
	return base + offset;
}
static uintptr_t *TerrainAddressSlot(const u32 *slot)
{
	uintptr_t base = (uintptr_t)NativeTerrainWork_At(0, CTR_SCRATCHPAD_SIZE, _Alignof(max_align_t)), address = (uintptr_t)slot;
	if (address < base || address - base >= CTR_SCRATCHPAD_SIZE || (address - base) % sizeof(u32))
		CTR_TRAP();
	uintptr_t *p = NativeHostScratch_Get(NATIVE_HOST_SCRATCH_TERRAIN_POINTERS, TERRAIN_POINTER_SLOTS * sizeof(*p), _Alignof(uintptr_t));
	if (!p)
		CTR_TRAP();
	return p + (address - base) / sizeof(u32);
}
void NativeTerrainWork_StoreAddress(u32 *slot, const void *value)
{
	uintptr_t *p = TerrainAddressSlot(slot);
	*p = (uintptr_t)value;
	*slot = 0;
#if defined(CTR_INTERNAL)
	NativeCheckpoint_RegisterPointerSlotSized(p, sizeof(*p));
#endif
}
CtrRuntimeAddress NativeTerrainWork_LoadAddress(const u32 *slot)
{
	return *TerrainAddressSlot(slot);
}
