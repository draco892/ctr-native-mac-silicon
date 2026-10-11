#ifndef PLATFORM_NATIVE_TERRAIN_WORK_H
#define PLATFORM_NATIVE_TERRAIN_WORK_H
#include <ctr_scratchpad.h>
// Retail offsets select scalar records; pointers live in a separate host region.
#if defined(CTR_NATIVE)
void *NativeTerrainWork_At(size_t offset, size_t bytes, size_t alignment);
void NativeTerrainWork_StoreAddress(u32 *slot, const void *value);
CtrRuntimeAddress NativeTerrainWork_LoadAddress(const u32 *slot);
#define CTR_TERRAIN_WORK_PTR(type, offset) ((type *)NativeTerrainWork_At(offset, sizeof(type), _Alignof(type)))
#else
#define CTR_TERRAIN_WORK_PTR(type, offset)          CTR_SCRATCHPAD_PTR(type, offset)
#define NativeTerrainWork_StoreAddress(slot, value) (*(slot) = (u32)(CtrRuntimeAddress)(value))
#define NativeTerrainWork_LoadAddress(slot)         ((CtrRuntimeAddress) * (slot))
#endif
#endif
