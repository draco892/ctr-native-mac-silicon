#ifndef NATIVE_CHECKPOINT_RELOCATION_H
#define NATIVE_CHECKPOINT_RELOCATION_H
#include <macros.h>
// Fixed-width metadata. Payloads remain specific to the recorded host ABI.
struct NativeCheckpointAddressRange { u32 kind, size; u64 start; };
CTR_STATIC_ASSERT(sizeof(struct NativeCheckpointAddressRange)==16);
struct NativeCheckpointPointerSlotRecord { u32 slotRegion,slotOffset,width,reserved; };
int NativeCheckpointSlots_Validate(const struct NativeCheckpointAddressRange *ranges,size_t rangeCount,
    const struct NativeCheckpointPointerSlotRecord *slots,size_t slotCount);
int NativeCheckpointRanges_Validate(const struct NativeCheckpointAddressRange *ranges,size_t count);
int NativeCheckpointRanges_Owner(const struct NativeCheckpointAddressRange *ranges,size_t count,u64 address,u32 *index,u32 *offset);
int NativeCheckpointRanges_Rebase(const struct NativeCheckpointAddressRange *oldRanges,size_t oldCount,
    const struct NativeCheckpointAddressRange *liveRanges,size_t liveCount,u64 address,u64 *out);
// Explicit widths preserve four-byte wire slots and eight-byte host slots.
int NativeCheckpointSlot_Read(const void *slot,u32 width,u64 *out);
int NativeCheckpointSlot_Write(void *slot,u32 width,u64 address);
int NativeCheckpointImage_Rebase(u64 address,u64 oldAnchor,u64 liveAnchor,u32 width,u64 *out);
#endif
