#ifndef PLATFORM_NATIVE_PTRMAP_H
#define PLATFORM_NATIVE_PTRMAP_H

#include <macros.h>

enum NativePtrMapResult
{
	NATIVE_PTRMAP_OK,
	NATIVE_PTRMAP_INVALID_ARGUMENT,
	NATIVE_PTRMAP_INVALID_MAP,
	NATIVE_PTRMAP_TABLE_TOO_SMALL,
	NATIVE_PTRMAP_INVALID_SLOT,
	NATIVE_PTRMAP_INVALID_TARGET,
	NATIVE_PTRMAP_DUPLICATE_SLOT,
	NATIVE_PTRMAP_SLOT_NOT_FOUND,
};

// Only offsets are retained in the decoded table; no process addresses enter
// the original asset or these records. Slot low bits are cleared like retail.
struct NativePtrMapEntry
{
	u32 slotOffset;
	u32 targetOffset;
};
CTR_STATIC_ASSERT(sizeof(struct NativePtrMapEntry) == 8);

struct NativePtrMapView
{
	u8 *origin;
	size_t originSize;
	const struct NativePtrMapEntry *entries;
	size_t count;
};

// PTR header: a little-endian u32 byte count followed by that many bytes of
// four-byte slot offsets. No implicit arena length or sector padding.
enum NativePtrMapResult NativePtrMap_GetCount(const void *map, size_t mapSize, size_t *count);

// Caller owns origin, map and writable entries, in non-overlapping storage.
// View is cleared on error; entries are scratch until success. Decode validates
// every slot/target, sorts by slot and rejects duplicate slots. Asset/map bytes
// are never patched. Target zero means origin; originSize means one-past-end.
enum NativePtrMapResult NativePtrMap_Decode(void *origin, size_t originSize,
    const void *map, size_t mapSize, struct NativePtrMapEntry *entries,
    size_t capacity, struct NativePtrMapView *view);

// requiredBytes verifies a complete object/range at the resolved target. Zero
// permits a one-past-end sentinel; a positive size rejects it. Output is NULL
// on failure. View/entries must originate from Decode and remain unmodified.
enum NativePtrMapResult NativePtrMap_Resolve(const struct NativePtrMapView *view,
    u32 slotOffset, size_t requiredBytes, void **target);

// Rebind after copying/restoring the same asset: offsets and entries survive
// unchanged. Re-decode when file contents or the pointer map change.
enum NativePtrMapResult NativePtrMap_Rebind(struct NativePtrMapView *view,
    void *origin, size_t originSize);

#endif
