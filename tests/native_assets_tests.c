#include <platform/native_asset_loading.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "failed: %s at line %d\n", #condition, __LINE__); return 1; \
} } while (0)

static void Put16(u8 *p, u16 value)
{
	p[0] = (u8)value;
	p[1] = (u8)(value >> 8);
}

static void MakeMap(u8 *map, const u32 *slots, size_t count)
{
	CTR_WriteU32LE(map, (u32)count * 4);
	for (size_t i = 0; i < count; i++)
		CTR_WriteU32LE(map + 4 + i * 4, slots[i]);
}

static void MakeModel(u8 *asset, u32 offset, u32 headers)
{
	memcpy(asset + offset, "abcdefghijklmnop", 16); // No on-disk terminator.
	Put16(asset + offset + 0x10, 0xffff); // Retail ignored-model ID, preserved.
	Put16(asset + offset + 0x12, 1);
	CTR_WriteU32LE(asset + offset + 0x14, headers);
	memcpy(asset + headers, "test_lod", 8);
	Put16(asset + headers + 0x14, 0x8000);
	Put16(asset + headers + 0x16, 2);
	Put16(asset + headers + 0x18, 0xfffe);
	Put16(asset + headers + 0x1a, 4096);
	Put16(asset + headers + 0x1c, 0x7fff);
	CTR_WriteU32LE(asset + headers + 0x34, 3);
}

static int TestMpk(void)
{
	u8 storage[281], snapshot[280], copy[256];
	u8 *file = storage + 1;
	u8 *asset = file + 4; // Every field deliberately unaligned.
	u8 *map = asset + 256;
	const u32 slots[] = {52, 8, 0, 4};
	struct NativePtrMapEntry entries[4];
	struct NativePtrMapView pointers;
	struct NativeMpkView mpk;
	struct NativeModelView model;
	struct NativeModelHeaderView header;
	memset(asset, 0, 256);
	CTR_WriteU32LE(asset, 128);
	CTR_WriteU32LE(asset + 4, 32);
	CTR_WriteU32LE(asset + 8, 32); // Shared model.
	MakeModel(asset, 32, 64);
	MakeMap(map, slots, 4);
	CTR_WriteU32LE(file, 256);
	memcpy(snapshot, file, 280);
	CHECK(NativeAsset_DecodeDram(file, 280, entries, 4, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(pointers.origin == asset && pointers.originSize == 256);
	CHECK(NativeMpk_Open(&pointers, &mpk) == NATIVE_ASSET_OK && mpk.modelCount == 2);
	CHECK(NativeMpk_GetModel(&mpk, 1, &model) == NATIVE_ASSET_OK);
	CHECK(model.offset == 32 && model.id == -1 && model.headerCount == 1);
	CHECK(strcmp(model.name, "abcdefghijklmnop") == 0);
	CHECK(NativeModel_GetHeader(&model, 0, &header) == NATIVE_ASSET_OK);
	CHECK(header.wire == asset + 64 && strcmp(header.name, "test_lod") == 0);
	CHECK(header.maxDistanceLOD == INT16_MIN && header.flags == 2 && header.animationCount == 3);
	CHECK(header.scale[0] == -2 && header.scale[1] == 4096 && header.scale[2] == INT16_MAX);
#if defined(__APPLE__) && defined(__aarch64__)
	CHECK((uintptr_t)header.wire > UINT32_MAX);
#endif
	CHECK(NativeModel_GetHeader(&model, 1, &header) == NATIVE_ASSET_INDEX_OUT_OF_RANGE && header.wire == NULL);
	CHECK(NativeMpk_GetModel(&mpk, 2, &model) == NATIVE_ASSET_INDEX_OUT_OF_RANGE && model.map == NULL);
	CHECK(memcmp(snapshot, file, 280) == 0);
	memcpy(copy, asset, 256);
	CHECK(NativePtrMap_Rebind(&pointers, copy, sizeof(copy)) == NATIVE_PTRMAP_OK);
	CHECK(NativeMpk_Open(&pointers, &mpk) == NATIVE_ASSET_OK);
	CHECK(NativeMpk_GetModel(&mpk, 0, &model) == NATIVE_ASSET_OK);
	CHECK(NativeModel_GetHeader(&model, 0, &header) == NATIVE_ASSET_OK && header.wire == copy + 64);
	for (int reload = 0; reload < 100; reload++)
	{
		Put16(copy + 0x30, (u16)reload);
		CHECK(NativePtrMap_Decode(copy, 256, map, 20, entries, 4, &pointers) == NATIVE_PTRMAP_OK);
		CHECK(NativeMpk_Open(&pointers, &mpk) == NATIVE_ASSET_OK);
		CHECK(NativeMpk_GetModel(&mpk, 0, &model) == NATIVE_ASSET_OK && model.id == reload);
	}
	// Invalid envelopes must never publish an asset view.
	CHECK(NativeAsset_DecodeDram(file, 3, entries, 4, &pointers) == NATIVE_PTRMAP_INVALID_MAP && pointers.origin == NULL);
	CHECK(NativeAsset_DecodeDram(file, 263, entries, 4, &pointers) == NATIVE_PTRMAP_INVALID_MAP);
	CHECK(NativeAsset_DecodeDram(file, 279, entries, 4, &pointers) == NATIVE_PTRMAP_INVALID_MAP);
	CHECK(NativeAsset_DecodeDram(file, 280, entries, 3, &pointers) == NATIVE_PTRMAP_TABLE_TOO_SMALL);
	CTR_WriteU32LE(asset + 4, 256); // PTR bytes are outside the resolved asset.
	CHECK(NativeAsset_DecodeDram(file, 280, entries, 4, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(NativeMpk_Open(&pointers, &mpk) == NATIVE_ASSET_INVALID_DATA);
	CTR_WriteU32LE(asset + 4, 233); // Only 23 bytes remain for a 24-byte model.
	CHECK(NativeAsset_DecodeDram(file, 280, entries, 4, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(NativeMpk_Open(&pointers, &mpk) == NATIVE_ASSET_INVALID_DATA);
	CTR_WriteU32LE(file, 0x80000000u); // Separate PTR requires its own explicit length.
	CHECK(NativeAsset_DecodeDram(file, 280, entries, 4, &pointers) == NATIVE_PTRMAP_INVALID_MAP);
	CTR_WriteU32LE(file, UINT32_MAX);
	CHECK(NativeAsset_DecodeDram(file, 280, entries, 4, &pointers) == NATIVE_PTRMAP_INVALID_MAP);
	return 0;
}

enum { LEVEL_SIZE = 0x36c, LEVEL_MAP_COUNT = 10 };
static const u32 LevelSlots[LEVEL_MAP_COUNT] = {0, 0x10, 0x18, 0x220, 0x224, 0x244, 0x20c, 0x210, 0x218, 0x2a0};

static void MakeLevel(u8 *asset)
{
	memset(asset, 0, LEVEL_SIZE);
	CTR_WriteU32LE(asset, 0x200);
	CTR_WriteU32LE(asset + 0xc, 1);
	CTR_WriteU32LE(asset + 0x10, 0x290);
	CTR_WriteU32LE(asset + 0x14, 2);
	CTR_WriteU32LE(asset + 0x18, 0x220);
	CTR_WriteU32LE(asset + 0x220, 0x230);
	CTR_WriteU32LE(asset + 0x224, 0x230);
	MakeModel(asset, 0x230, 0x250);
	memcpy(asset + 0x290, "instance_name_16", 16);
	CTR_WriteU32LE(asset + 0x2a0, 0x230);
	Put16(asset + 0x2a4, 0x8000);
	Put16(asset + 0x2c0, 0xfffe);
	Put16(asset + 0x2c6, 0x7fff);
	CTR_WriteU32LE(asset + 0x2ac, 0x12345678);
	CTR_WriteU32LE(asset + 0x2b0, 0x80000001);
	CTR_WriteU32LE(asset + 0x2b4, 0x80000000);
	CTR_WriteU32LE(asset + 0x2b8, 0xffffffff);
	CTR_WriteU32LE(asset + 0x2bc, 0xdeadbeef); // Runtime scratch, never dereferenced.
	CTR_WriteU32LE(asset + 0x2cc, 0xfffffffe);
	CTR_WriteU32LE(asset + 0x200, 1);
	CTR_WriteU32LE(asset + 0x204, 2);
	CTR_WriteU32LE(asset + 0x20c, 0x2d0);
	CTR_WriteU32LE(asset + 0x210, 0x32c);
	CTR_WriteU32LE(asset + 0x218, 0x34c);
	CTR_WriteU32LE(asset + 0x21c, 1);
}

static int TestLevel(void)
{
	u8 storage[LEVEL_SIZE + 1], snapshot[LEVEL_SIZE];
	u8 *asset = storage + 1;
	u8 map[4 + 4 * LEVEL_MAP_COUNT];
	struct NativePtrMapEntry entries[LEVEL_MAP_COUNT];
	struct NativePtrMapView pointers;
	struct NativeLevelView level;
	struct NativeModelView model;
	struct NativeMeshView mesh;
	struct NativeInstanceDefView instance;
	MakeLevel(asset);
	MakeMap(map, LevelSlots, LEVEL_MAP_COUNT);
	memcpy(snapshot, asset, LEVEL_SIZE);
	CHECK(NativePtrMap_Decode(asset, LEVEL_SIZE, map, sizeof(map), entries, LEVEL_MAP_COUNT, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(NativeLevel_Open(&pointers, &level) == NATIVE_ASSET_OK);
	CHECK(level.modelCount == 2 && level.modelsOffset == 0x220);
	CHECK(level.instanceCount == 1 && level.instancesOffset == 0x290);
	CHECK(NativeLevel_GetInstance(&level, 0, &instance) == NATIVE_ASSET_OK);
	CHECK(instance.model.offset == 0x230 && instance.model.id == -1);
	CHECK(strcmp(instance.name, "instance_name_16") == 0);
	CHECK(instance.scale[0] == INT16_MIN && instance.position[0] == -2 && instance.rotation[0] == INT16_MAX);
	CHECK(instance.colorRGBA == 0x12345678 && instance.flags == 0x80000001);
	CHECK(instance.unk24 == INT32_MIN && instance.unk28 == -1 && instance.modelID == -2);
	CHECK(NativeLevel_GetInstance(&level, 1, &instance) == NATIVE_ASSET_INDEX_OUT_OF_RANGE && instance.model.map == NULL);
	CHECK(NativeLevel_GetModel(&level, 0, &model) == NATIVE_ASSET_OK && model.offset == 0x230);
	CHECK(NativeLevel_GetModel(&level, 2, &model) == NATIVE_ASSET_INDEX_OUT_OF_RANGE && model.map == NULL);
	CHECK(NativeLevel_GetMesh(&level, &mesh) == NATIVE_ASSET_OK);
	CHECK(mesh.quadCount == 1 && mesh.quads == asset + 0x2d0);
	CHECK(mesh.vertexCount == 2 && mesh.vertices == asset + 0x32c);
	CHECK(mesh.bspCount == 1 && mesh.bsp == asset + 0x34c); // Exact end of file.
	CHECK(memcmp(snapshot, asset, LEVEL_SIZE) == 0);
	// Missing instance-model relocation and a target at EOF are both rejected.
	MakeMap(map, LevelSlots, LEVEL_MAP_COUNT - 1);
	CHECK(NativePtrMap_Decode(asset, LEVEL_SIZE, map, sizeof(map), entries, LEVEL_MAP_COUNT, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(NativeLevel_GetInstance(&level, 0, &instance) == NATIVE_ASSET_INVALID_DATA && instance.model.map == NULL);
	MakeMap(map, LevelSlots, LEVEL_MAP_COUNT);
	CTR_WriteU32LE(asset + 0x2a0, LEVEL_SIZE);
	CHECK(NativePtrMap_Decode(asset, LEVEL_SIZE, map, sizeof(map), entries, LEVEL_MAP_COUNT, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(NativeLevel_GetInstance(&level, 0, &instance) == NATIVE_ASSET_INVALID_DATA);
	CTR_WriteU32LE(asset + 0x2a0, 0x230);
	CHECK(NativePtrMap_Decode(asset, LEVEL_SIZE, map, sizeof(map), entries, LEVEL_MAP_COUNT, &pointers) == NATIVE_PTRMAP_OK);
	// Counts must describe whole spans, with retail strides, not host sizeof.
	CTR_WriteU32LE(asset + 0xc, UINT32_MAX);
	CHECK(NativeLevel_Open(&pointers, &level) == NATIVE_ASSET_INVALID_DATA && level.map == NULL);
	CTR_WriteU32LE(asset + 0xc, 1);
	CTR_WriteU32LE(asset + 0x14, UINT32_MAX);
	CHECK(NativeLevel_Open(&pointers, &level) == NATIVE_ASSET_INVALID_DATA);
	CTR_WriteU32LE(asset + 0x14, 2);
	CHECK(NativeLevel_Open(&pointers, &level) == NATIVE_ASSET_OK);
	CTR_WriteU32LE(asset + 0x204, 3);
	// This overlaps BSP but fits: spans check bounds, not semantic overlap.
	CHECK(NativeLevel_GetMesh(&level, &mesh) == NATIVE_ASSET_OK);
	CTR_WriteU32LE(asset + 0x204, 5);
	CHECK(NativeLevel_GetMesh(&level, &mesh) == NATIVE_ASSET_INVALID_DATA && mesh.vertices == NULL);
	CTR_WriteU32LE(asset + 0x204, 2);
	CTR_WriteU32LE(asset + 0x21c, 2);
	CHECK(NativeLevel_GetMesh(&level, &mesh) == NATIVE_ASSET_INVALID_DATA);
	CTR_WriteU32LE(asset + 0x21c, UINT32_MAX);
	CHECK(NativeLevel_GetMesh(&level, &mesh) == NATIVE_ASSET_INVALID_DATA);
	return 0;
}

static int TestRejectionsAndNulls(void)
{
	u8 asset[256] = {0}, map[12];
	struct NativePtrMapEntry entries[2];
	struct NativePtrMapView pointers;
	struct NativeMpkView mpk;
	struct NativeModelView model;
	struct NativeLevelView level;
	const u32 slots[] = {4, 52};
	MakeMap(map, slots, 2);
	CTR_WriteU32LE(asset + 4, 32);
	MakeModel(asset, 32, 64);
	CHECK(NativePtrMap_Decode(asset, 256, map, sizeof(map), entries, 2, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(NativeMpk_Open(&pointers, &mpk) == NATIVE_ASSET_OK && mpk.modelCount == 1); // NULL icons.
	MakeMap(map, slots, 1); // A nonzero header pointer absent from PTR is invalid.
	CHECK(NativePtrMap_Decode(asset, 256, map, 8, entries, 2, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(NativeMpk_GetModel(&mpk, 0, &model) == NATIVE_ASSET_INVALID_DATA && model.map == NULL);
	MakeMap(map, slots, 2);
	CHECK(NativePtrMap_Decode(asset, 256, map, sizeof(map), entries, 2, &pointers) == NATIVE_PTRMAP_OK);
	Put16(asset + 50, 0xffff);
	CHECK(NativeMpk_GetModel(&mpk, 0, &model) == NATIVE_ASSET_INVALID_DATA && model.map == NULL);
	Put16(asset + 50, 4); // Four headers run off file.
	CHECK(NativeMpk_GetModel(&mpk, 0, &model) == NATIVE_ASSET_INVALID_DATA);
	Put16(asset + 50, 0);
	CHECK(NativeMpk_GetModel(&mpk, 0, &model) == NATIVE_ASSET_OK && model.headerCount == 0);
	CTR_WriteU32LE(asset + 8, 32); // Nonzero unlisted pointer, not a sentinel.
	CHECK(NativeMpk_Open(&pointers, &mpk) == NATIVE_ASSET_INVALID_DATA && mpk.map == NULL);
	CTR_WriteU32LE(asset + 8, 0);
	// An explicitly relocated zero references origin, and is NOT MPK terminator.
	CTR_WriteU32LE(asset + 4, 0);
	CHECK(NativePtrMap_Decode(asset, 256, map, sizeof(map), entries, 2, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(NativeMpk_Open(&pointers, &mpk) == NATIVE_ASSET_OK && mpk.modelCount == 1);
	CHECK(NativeMpk_GetModel(&mpk, 0, &model) == NATIVE_ASSET_OK && model.offset == 0);
	MakeMap(map, slots, 0);
	CHECK(NativePtrMap_Decode(asset, 4, map, 4, NULL, 0, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(NativeMpk_Open(&pointers, &mpk) == NATIVE_ASSET_INVALID_DATA); // Missing list terminator.
	CHECK(NativeLevel_Open(&pointers, &level) == NATIVE_ASSET_INVALID_DATA); // Truncated root.
	u8 emptyLevel[NATIVE_LEVEL_BYTES] = {0};
	CHECK(NativePtrMap_Decode(emptyLevel, sizeof(emptyLevel), map, 4, NULL, 0, &pointers) == NATIVE_PTRMAP_OK);
	CHECK(NativeLevel_Open(&pointers, &level) == NATIVE_ASSET_OK && level.modelCount == 0 && level.instanceCount == 0);
	CHECK(NativeLevel_Open(NULL, &level) == NATIVE_ASSET_INVALID_ARGUMENT && level.map == NULL);
	CHECK(NativeMpk_Open(&pointers, NULL) == NATIVE_ASSET_INVALID_ARGUMENT);
	return 0;
}

static int TestMalformedCorpus(void)
{
	u8 asset[LEVEL_SIZE], map[4 + 4 * LEVEL_MAP_COUNT];
	struct NativePtrMapEntry entries[LEVEL_MAP_COUNT];
	struct NativePtrMapView pointers;
	struct NativeLevelView level;
	struct NativeModelView model;
	struct NativeModelHeaderView header;
	struct NativeMeshView mesh;
	u32 random = 0x19283746;
	const u32 fields[] = {0xc, 0x14, 0x18, 0x10, 0x200, 0x204, 0x21c, 0x20c, 0x210, 0x218, 0x220, 0x242, 0x244};
	MakeMap(map, LevelSlots, LEVEL_MAP_COUNT);
	for (int trial = 0; trial < 2000; trial++)
	{
		MakeLevel(asset);
		random = random * 1664525u + 1013904223u;
		u32 value = trial % 2 == 0 ? random : random % (LEVEL_SIZE + 64);
		CTR_WriteU32LE(asset + fields[trial % (sizeof(fields) / sizeof(fields[0]))], value);
		if (NativePtrMap_Decode(asset, LEVEL_SIZE, map, sizeof(map), entries, LEVEL_MAP_COUNT, &pointers) != NATIVE_PTRMAP_OK)
			continue;
		if (NativeLevel_Open(&pointers, &level) != NATIVE_ASSET_OK)
		{
			CHECK(level.map == NULL);
			continue;
		}
		if (NativeLevel_GetMesh(&level, &mesh) != NATIVE_ASSET_OK)
			CHECK(mesh.quads == NULL && mesh.vertices == NULL && mesh.bsp == NULL);
		for (u32 i = 0; i < level.modelCount; i++)
			if (NativeLevel_GetModel(&level, i, &model) == NATIVE_ASSET_OK)
				for (u32 j = 0; j < model.headerCount; j++)
				{
					CHECK(NativeModel_GetHeader(&model, j, &header) == NATIVE_ASSET_OK);
					CHECK(header.wire >= asset && header.wire <= asset + LEVEL_SIZE - NATIVE_MODEL_HEADER_BYTES);
				}
	}
	return 0;
}

static int TestLoadCompletions(void)
{
	u8 file[4 + LEVEL_SIZE + 4 + LEVEL_MAP_COUNT * 4];
	u8 *asset = file + 4, *ptr = asset + LEVEL_SIZE;
	u8 original[sizeof(file)];
	struct NativePtrMapEntry entries[LEVEL_MAP_COUNT];
	struct NativeAssetLoad load = {0};
	struct NativeDramLayout layout;
	struct NativeLevelView level;
	struct NativeModelView model;
	MakeLevel(asset);
	MakeMap(ptr, LevelSlots, LEVEL_MAP_COUNT);
	CTR_WriteU32LE(file, LEVEL_SIZE);
	memcpy(original, file, sizeof(file));
	CHECK(NativeAssetLoad_CheckDram(file, sizeof(file), &layout));
	CHECK(layout.payload == asset && layout.payloadBytes == LEVEL_SIZE && layout.ptr == ptr);
	CHECK(layout.relocationCount == LEVEL_MAP_COUNT);
	CHECK(NativeAssetLoad_CompleteDram(file, sizeof(file), entries, LEVEL_MAP_COUNT, &load) == NATIVE_PTRMAP_OK);
	CHECK(load.state == NATIVE_ASSET_LOAD_READY && load.pointers.originSize == LEVEL_SIZE);
	CHECK(NativeLevel_Open(&load.pointers, &level) == NATIVE_ASSET_OK);
	CHECK(NativeLevel_GetModel(&level, 0, &model) == NATIVE_ASSET_OK);
	CHECK(memcmp(file, original, sizeof(file)) == 0);
	CHECK(NativeAssetLoad_CompletePtr(&load, ptr, 4 + LEVEL_MAP_COUNT * 4, entries, LEVEL_MAP_COUNT) == NATIVE_PTRMAP_INVALID_ARGUMENT);
	CHECK(load.state == NATIVE_ASSET_LOAD_READY);
	// A new corrupt file must discard the previously published view.
	CHECK(NativeAssetLoad_CompleteDram(file, 3, entries, LEVEL_MAP_COUNT, &load) == NATIVE_PTRMAP_INVALID_MAP);
	CHECK(load.state == NATIVE_ASSET_LOAD_EMPTY && load.pointers.origin == NULL && load.payload == NULL);
	CTR_WriteU32LE(file, 0xffffffffu);
	CHECK(NativeAssetLoad_CompleteDram(file, 4 + LEVEL_SIZE, NULL, 0, &load) == NATIVE_PTRMAP_OK);
	CHECK(load.state == NATIVE_ASSET_LOAD_WAITING_PTR && load.payload == asset && load.payloadBytes == LEVEL_SIZE);
	CHECK(load.pointers.origin == NULL);
	CHECK(NativeAssetLoad_CompletePtr(&load, ptr, 3, entries, LEVEL_MAP_COUNT) == NATIVE_PTRMAP_INVALID_MAP);
	CHECK(load.state == NATIVE_ASSET_LOAD_WAITING_PTR && load.pointers.origin == NULL);
	CHECK(NativeAssetLoad_CompletePtr(&load, ptr, 4 + LEVEL_MAP_COUNT * 4, entries, 0) == NATIVE_PTRMAP_TABLE_TOO_SMALL);
	CHECK(load.state == NATIVE_ASSET_LOAD_WAITING_PTR);
	CHECK(NativeAssetLoad_CompletePtr(&load, ptr, 4 + LEVEL_MAP_COUNT * 4, entries, LEVEL_MAP_COUNT) == NATIVE_PTRMAP_OK);
	// PTR can be released/overwritten after successful completion: all decoded
	// slots/targets live in caller-owned entries, not in the temporary PTR buffer.
	memset(ptr, 0xff, 4 + LEVEL_MAP_COUNT * 4);
	CHECK(NativeLevel_Open(&load.pointers, &level) == NATIVE_ASSET_OK);
	CHECK(NativeLevel_GetModel(&level, 1, &model) == NATIVE_ASSET_OK);
	CHECK(NativeAssetLoad_BeginRaw(asset, LEVEL_SIZE, &load) == NATIVE_PTRMAP_OK);
	CHECK(load.state == NATIVE_ASSET_LOAD_WAITING_PTR && load.pointers.origin == NULL);
	MakeMap(ptr, LevelSlots, LEVEL_MAP_COUNT);
	CHECK(NativeAssetLoad_CompletePtr(&load, ptr, 4 + LEVEL_MAP_COUNT * 4, entries, LEVEL_MAP_COUNT) == NATIVE_PTRMAP_OK);
	CHECK(NativeLevel_Open(&load.pointers, &level) == NATIVE_ASSET_OK);
	NativeAssetLoad_Reset(&load);
	CHECK(load.state == NATIVE_ASSET_LOAD_EMPTY && load.payload == NULL);
	CHECK(NativeAssetLoad_CompletePtr(&load, ptr, 4 + LEVEL_MAP_COUNT * 4, entries, LEVEL_MAP_COUNT) == NATIVE_PTRMAP_INVALID_ARGUMENT);
	CHECK(NativeAssetLoad_BeginRaw(NULL, LEVEL_SIZE, &load) == NATIVE_PTRMAP_INVALID_ARGUMENT);
	// Preflight catches duplicate normalized slots before legacy patching.
	CTR_WriteU32LE(file, LEVEL_SIZE);
	CTR_WriteU32LE(ptr + 8, LevelSlots[0]);
	CHECK(!NativeAssetLoad_CheckDram(file, sizeof(file), &layout) && layout.payload == NULL);
	return 0;
}

int main(void)
{
	if (TestMpk() || TestLevel() || TestRejectionsAndNulls() || TestMalformedCorpus() || TestLoadCompletions())
		return 1;
	printf("Native MPK/LEV readers: wire tables, models, headers, mesh spans, rebind/reload and malformed fixtures passed (%zu-bit pointers).\n", sizeof(void *) * 8);
	return 0;
}
