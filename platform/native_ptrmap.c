#include <platform/native_ptrmap.h>

#include <stdlib.h>
#include <string.h>

enum NativePtrMapResult NativePtrMap_GetCount(const void *map, size_t mapSize, size_t *count)
{
	u32 bytes;
	if (count == NULL)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	*count = 0;
	if (map == NULL)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	if (mapSize < 4)
		return NATIVE_PTRMAP_INVALID_MAP;
	bytes = CTR_ReadU32LE(map);
	if ((bytes & 3u) != 0 || bytes > mapSize - 4)
		return NATIVE_PTRMAP_INVALID_MAP;
	*count = bytes / 4u;
	return NATIVE_PTRMAP_OK;
}

static int NativePtrMap_CompareSlots(const void *left, const void *right)
{
	const struct NativePtrMapEntry *a = left;
	const struct NativePtrMapEntry *b = right;
	return (a->slotOffset > b->slotOffset) - (a->slotOffset < b->slotOffset);
}

enum NativePtrMapResult NativePtrMap_Decode(void *origin, size_t originSize,
    const void *map, size_t mapSize, struct NativePtrMapEntry *entries,
    size_t capacity, struct NativePtrMapView *view)
{
	size_t count;
	enum NativePtrMapResult result;
	const u8 *offsets;
	if (view == NULL)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	memset(view, 0, sizeof(*view));
	if (origin == NULL || originSize > UINT32_MAX ||
	    (uintptr_t)origin > UINTPTR_MAX - originSize)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	result = NativePtrMap_GetCount(map, mapSize, &count);
	if (result != NATIVE_PTRMAP_OK)
		return result;
	if (count > capacity || count > SIZE_MAX / sizeof(*entries))
		return NATIVE_PTRMAP_TABLE_TOO_SMALL;
	if (count != 0 && entries == NULL)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	offsets = (const u8 *)map + 4;
	for (size_t i = 0; i < count; i++)
	{
		u32 slot = CTR_ReadU32LE(offsets + i * 4) & ~3u;
		u32 target;
		if (slot > originSize || originSize - slot < 4)
			return NATIVE_PTRMAP_INVALID_SLOT;
		target = CTR_ReadU32LE((u8 *)origin + slot);
		if (target > originSize)
			return NATIVE_PTRMAP_INVALID_TARGET;
		entries[i].slotOffset = slot;
		entries[i].targetOffset = target;
	}
	if (count != 0)
		qsort(entries, count, sizeof(*entries), NativePtrMap_CompareSlots);
	for (size_t i = 1; i < count; i++)
		if (entries[i - 1].slotOffset == entries[i].slotOffset)
			return NATIVE_PTRMAP_DUPLICATE_SLOT;
	view->origin = origin;
	view->originSize = originSize;
	view->entries = entries;
	view->count = count;
	return NATIVE_PTRMAP_OK;
}

enum NativePtrMapResult NativePtrMap_Resolve(const struct NativePtrMapView *view,
    u32 slotOffset, size_t requiredBytes, void **target)
{
	size_t low = 0;
	size_t high;
	u32 offset;
	if (target == NULL)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	*target = NULL;
	if (view == NULL || view->origin == NULL || (view->count != 0 && view->entries == NULL))
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	high = view->count;
	while (low < high)
	{
		size_t middle = low + (high - low) / 2;
		if (view->entries[middle].slotOffset < slotOffset)
			low = middle + 1;
		else
			high = middle;
	}
	if (low == view->count || view->entries[low].slotOffset != slotOffset)
		return NATIVE_PTRMAP_SLOT_NOT_FOUND;
	offset = view->entries[low].targetOffset;
	if (offset > view->originSize || requiredBytes > view->originSize - offset)
		return NATIVE_PTRMAP_INVALID_TARGET;
	*target = view->origin + offset;
	return NATIVE_PTRMAP_OK;
}

enum NativePtrMapResult NativePtrMap_Rebind(struct NativePtrMapView *view,
    void *origin, size_t originSize)
{
	if (view == NULL || view->origin == NULL || origin == NULL || originSize != view->originSize ||
	    (uintptr_t)origin > UINTPTR_MAX - originSize)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	view->origin = origin;
	return NATIVE_PTRMAP_OK;
}
