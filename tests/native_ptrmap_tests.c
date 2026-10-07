#include <platform/native_ptrmap.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "failed: %s at line %d\n", #condition, __LINE__); \
			return 1; \
		} \
	} while (0)

static int TestGraphAndRebind(void)
{
	u8 storage[33];
	u8 *asset = storage + 1; // Deliberately unaligned source fields.
	u8 original[32];
	u8 copy[32];
	u8 mapStorage[22];
	u8 *map = mapStorage + 1;
	u8 originalMap[20];
	struct NativePtrMapEntry entries[4];
	struct NativePtrMapView view;
	size_t count;
	void *target;

	memset(asset, 0x55, 32);
	CTR_WriteU32LE(asset, 8); // Forward reference.
	CTR_WriteU32LE(asset + 4, 8); // Shared target.
	CTR_WriteU32LE(asset + 8, 0); // Cycle and a reference to origin, not NULL.
	CTR_WriteU32LE(asset + 12, 32); // One-past-end sentinel.
	CTR_WriteU32LE(map, 16);
	CTR_WriteU32LE(map + 4, 12);
	CTR_WriteU32LE(map + 8, 8);
	CTR_WriteU32LE(map + 12, 7); // Retail clears low two slot bits: 7 -> 4.
	CTR_WriteU32LE(map + 16, 0);
	memcpy(original, asset, 32);
	memcpy(originalMap, map, 20);
	CHECK(NativePtrMap_GetCount(map, 20, &count) == NATIVE_PTRMAP_OK && count == 4);
	CHECK(NativePtrMap_Decode(asset, 32, map, 20, entries, 4, &view) == NATIVE_PTRMAP_OK);
	CHECK(view.count == 4 && entries[0].slotOffset == 0 && entries[3].slotOffset == 12);
	CHECK(NativePtrMap_Resolve(&view, 0, 4, &target) == NATIVE_PTRMAP_OK && target == asset + 8);
#if defined(__APPLE__) && defined(__aarch64__)
	CHECK((uintptr_t)target > UINT32_MAX);
#endif
	CHECK(NativePtrMap_Resolve(&view, 4, 4, &target) == NATIVE_PTRMAP_OK && target == asset + 8);
	CHECK(NativePtrMap_Resolve(&view, 8, 4, &target) == NATIVE_PTRMAP_OK && target == asset);
	CHECK(NativePtrMap_Resolve(&view, 12, 0, &target) == NATIVE_PTRMAP_OK && target == asset + 32);
	CHECK(NativePtrMap_Resolve(&view, 12, 1, &target) == NATIVE_PTRMAP_INVALID_TARGET && target == NULL);
	CHECK(NativePtrMap_Resolve(&view, 0, 25, &target) == NATIVE_PTRMAP_INVALID_TARGET && target == NULL);
	CHECK(NativePtrMap_Resolve(&view, 16, 0, &target) == NATIVE_PTRMAP_SLOT_NOT_FOUND && target == NULL);
	CHECK(memcmp(original, asset, 32) == 0 && memcmp(originalMap, map, 20) == 0);
	CHECK(NativePtrMap_Decode(asset, 32, map, 20, entries, 4, &view) == NATIVE_PTRMAP_OK);
	memcpy(copy, asset, 32);
	CHECK(NativePtrMap_Rebind(&view, copy, 31) == NATIVE_PTRMAP_INVALID_ARGUMENT);
	CHECK(view.origin == asset);
	CHECK(NativePtrMap_Rebind(&view, copy, 32) == NATIVE_PTRMAP_OK);
	CHECK(NativePtrMap_Resolve(&view, 0, 4, &target) == NATIVE_PTRMAP_OK && target == copy + 8);
	CHECK(NativePtrMap_Resolve(&view, 8, 4, &target) == NATIVE_PTRMAP_OK && target == copy);
	CTR_WriteU32LE(copy, 4);
	CHECK(NativePtrMap_Decode(copy, 32, map, 20, entries, 4, &view) == NATIVE_PTRMAP_OK);
	CHECK(NativePtrMap_Resolve(&view, 0, 4, &target) == NATIVE_PTRMAP_OK && target == copy + 4);
	return 0;
}

static int TestMalformedCorpus(void)
{
	u8 asset[64];
	u8 map[68];
	struct NativePtrMapEntry entries[16];
	struct NativePtrMapView view;
	u32 random = 0xabcdef01;
	void *target;
	for (int trial = 0; trial < 2000; trial++)
	{
		CTR_WriteU32LE(map, 64);
		for (u32 i = 0; i < 16; i++)
		{
			random = random * 1664525u + 1013904223u;
			CTR_WriteU32LE(asset + i * 4, random % 80);
			CTR_WriteU32LE(map + 4 + i * 4, random % 80);
		}
		enum NativePtrMapResult result = NativePtrMap_Decode(asset, sizeof(asset), map, sizeof(map), entries, 16, &view);
		if (result == NATIVE_PTRMAP_OK)
		{
			for (size_t i = 0; i < view.count; i++)
			{
				CHECK(entries[i].slotOffset <= sizeof(asset) - 4);
				CHECK(entries[i].targetOffset <= sizeof(asset));
				CHECK(NativePtrMap_Resolve(&view, entries[i].slotOffset, 0, &target) == NATIVE_PTRMAP_OK);
				CHECK(target == asset + entries[i].targetOffset);
			}
		}
		else
			CHECK(view.origin == NULL && view.count == 0);
	}
	return 0;
}

static int TestMalformedMaps(void)
{
	u8 asset[16] = {0};
	u8 map[12] = {0};
	struct NativePtrMapEntry entries[2];
	struct NativePtrMapView view;
	size_t count = 99;
	void *target = asset;

	CHECK(NativePtrMap_GetCount(NULL, 4, &count) == NATIVE_PTRMAP_INVALID_ARGUMENT && count == 0);
	CHECK(NativePtrMap_GetCount(map, 4, NULL) == NATIVE_PTRMAP_INVALID_ARGUMENT);
	for (size_t size = 0; size < 4; size++)
		CHECK(NativePtrMap_GetCount(map, size, &count) == NATIVE_PTRMAP_INVALID_MAP && count == 0);
	CTR_WriteU32LE(map, 3);
	CHECK(NativePtrMap_GetCount(map, sizeof(map), &count) == NATIVE_PTRMAP_INVALID_MAP);
	CTR_WriteU32LE(map, UINT32_MAX - 3u);
	CHECK(NativePtrMap_GetCount(map, sizeof(map), &count) == NATIVE_PTRMAP_INVALID_MAP);
	CTR_WriteU32LE(map, 8);
	CTR_WriteU32LE(map + 4, 0);
	CTR_WriteU32LE(map + 8, 4);
	CHECK(NativePtrMap_GetCount(map, 11, &count) == NATIVE_PTRMAP_INVALID_MAP);
	CHECK(NativePtrMap_Decode(asset, 16, map, 12, entries, 1, &view) == NATIVE_PTRMAP_TABLE_TOO_SMALL);
	CHECK(view.origin == NULL && view.count == 0);
	CHECK(NativePtrMap_Decode(NULL, 16, map, 12, entries, 2, &view) == NATIVE_PTRMAP_INVALID_ARGUMENT);
	CHECK(NativePtrMap_Decode(asset, 16, map, 12, NULL, 2, &view) == NATIVE_PTRMAP_INVALID_ARGUMENT);
	CHECK(NativePtrMap_Decode(asset, 16, map, 12, entries, 2, NULL) == NATIVE_PTRMAP_INVALID_ARGUMENT);
	CHECK(NativePtrMap_Decode(asset, 3, map, 12, entries, 2, &view) == NATIVE_PTRMAP_INVALID_SLOT);
	const u32 invalidSlots[] = {16, 20, UINT32_MAX};
	for (size_t i = 0; i < len(invalidSlots); i++)
	{
		CTR_WriteU32LE(map + 8, invalidSlots[i]);
		CHECK(NativePtrMap_Decode(asset, 16, map, 12, entries, 2, &view) == NATIVE_PTRMAP_INVALID_SLOT);
		CHECK(view.origin == NULL);
	}
	CTR_WriteU32LE(map + 8, 4);
	CTR_WriteU32LE(asset + 4, 17);
	CHECK(NativePtrMap_Decode(asset, 16, map, 12, entries, 2, &view) == NATIVE_PTRMAP_INVALID_TARGET);
	CHECK(NativePtrMap_Resolve(&view, 0, 0, &target) == NATIVE_PTRMAP_INVALID_ARGUMENT && target == NULL);
	CTR_WriteU32LE(asset + 4, UINT32_MAX);
	CHECK(NativePtrMap_Decode(asset, 16, map, 12, entries, 2, &view) == NATIVE_PTRMAP_INVALID_TARGET);
	CTR_WriteU32LE(asset + 4, 0);
	CTR_WriteU32LE(map + 8, 3); // Duplicate normalized slot.
	CHECK(NativePtrMap_Decode(asset, 16, map, 12, entries, 2, &view) == NATIVE_PTRMAP_DUPLICATE_SLOT);
	CHECK(view.origin == NULL && view.count == 0);
	CTR_WriteU32LE(map, 0);
	CHECK(NativePtrMap_Decode(asset, 16, map, 4, NULL, 0, &view) == NATIVE_PTRMAP_OK);
	CHECK(view.count == 0);
	CHECK(NativePtrMap_Resolve(&view, 0, 0, &target) == NATIVE_PTRMAP_SLOT_NOT_FOUND);
	CHECK(NativePtrMap_Resolve(&view, 0, 0, NULL) == NATIVE_PTRMAP_INVALID_ARGUMENT);
	return 0;
}

static int TestRepeatedLoads(void)
{
	u8 asset[64];
	u8 map[68];
	struct NativePtrMapEntry entries[16];
	struct NativePtrMapView view;
	void *target;
	CTR_WriteU32LE(map, 64);
	for (int reload = 0; reload < 100; reload++)
	{
		for (u32 i = 0; i < 16; i++)
		{
			CTR_WriteU32LE(map + 4 + i * 4, (15 - i) * 4);
			CTR_WriteU32LE(asset + i * 4, ((i + (u32)reload) % 16) * 4);
		}
		CHECK(NativePtrMap_Decode(asset, sizeof(asset), map, sizeof(map), entries, 16, &view) == NATIVE_PTRMAP_OK);
		for (u32 i = 0; i < 16; i++)
		{
			CHECK(NativePtrMap_Resolve(&view, i * 4, 4, &target) == NATIVE_PTRMAP_OK);
			CHECK(target == asset + ((i + (u32)reload) % 16) * 4);
		}
	}
	return 0;
}

int main(void)
{
	CHECK(TestGraphAndRebind() == 0);
	CHECK(TestMalformedMaps() == 0);
	CHECK(TestRepeatedLoads() == 0);
	CHECK(TestMalformedCorpus() == 0);
	puts("Pointer-map tests passed: 32-bit offsets, host resolution, bounds, cycles and rebind");
	return 0;
}
