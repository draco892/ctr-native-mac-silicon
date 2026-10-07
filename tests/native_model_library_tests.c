#include <platform/native_model_library.h>

#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s at line %d\n", #c, __LINE__); return 1; } } while (0)

struct Fixture
{
	u8 bytes[129], ptr[12];
	struct NativePtrMapEntry entries[2];
	struct NativePtrMapView map;
	struct NativeMpkView mpk;
};

static int MakeFixture(struct Fixture *f, s16 first, s16 second)
{
	u8 *asset = f->bytes + 1;
	memset(asset, 0, 128);
	CTR_WriteU32LE(asset + 4, 32);
	CTR_WriteU32LE(asset + 8, 64);
	memcpy(asset + 32, "first", 5);
	memcpy(asset + 64, "second", 6);
	asset[48] = (u8)first; asset[49] = (u8)((u16)first >> 8);
	asset[80] = (u8)second; asset[81] = (u8)((u16)second >> 8);
	CTR_WriteU32LE(f->ptr, 8);
	CTR_WriteU32LE(f->ptr + 4, 8);
	CTR_WriteU32LE(f->ptr + 8, 4);
	CHECK(NativePtrMap_Decode(asset, 128, f->ptr, 12, f->entries, 2, &f->map) == NATIVE_PTRMAP_OK);
	CHECK(NativeMpk_Open(&f->map, &f->mpk) == NATIVE_ASSET_OK);
	return 0;
}

static int TestOverridesAndLifetime(void)
{
	struct Fixture a, b;
	struct NativeModelLibrary library, snapshot;
	struct NativeModelView model;
	u8 copy[128];
	NativeModelLibrary_Reset(&library);
	CHECK(NativeModelLibrary_Get(&library, 5, &model) == NATIVE_ASSET_NOT_FOUND && model.map == NULL);
	CHECK(MakeFixture(&a, 5, -1) == 0 && MakeFixture(&b, 5, 10) == 0);
	CHECK(NativeModelLibrary_StoreMpk(&library, &a.mpk) == NATIVE_ASSET_OK);
	CHECK(NativeModelLibrary_Get(&library, 5, &model) == NATIVE_ASSET_OK && model.map == &a.map);
	CHECK(NativeModelLibrary_Get(&library, -1, &model) == NATIVE_ASSET_INDEX_OUT_OF_RANGE);
	CHECK(NativeModelLibrary_Get(&library, 227, &model) == NATIVE_ASSET_INDEX_OUT_OF_RANGE);
	CHECK(NativeModelLibrary_StoreMpk(&library, &b.mpk) == NATIVE_ASSET_OK);
	CHECK(NativeModelLibrary_Get(&library, 5, &model) == NATIVE_ASSET_OK && model.map == &b.map);
	CHECK(NativeModelLibrary_Get(&library, 10, &model) == NATIVE_ASSET_OK && strcmp(model.name, "second") == 0);
	NativeModelLibrary_DropOwner(&library, &a.map); // Does not erase B's override.
	CHECK(NativeModelLibrary_Get(&library, 5, &model) == NATIVE_ASSET_OK);
	NativeModelLibrary_DropOwner(&library, &b.map);
	CHECK(NativeModelLibrary_Get(&library, 5, &model) == NATIVE_ASSET_NOT_FOUND);
	CHECK(NativeModelLibrary_Get(&library, 10, &model) == NATIVE_ASSET_NOT_FOUND);
	CHECK(NativeModelLibrary_StoreMpk(&library, &a.mpk) == NATIVE_ASSET_OK);
	memcpy(copy, a.map.origin, 128);
	CHECK(NativePtrMap_Rebind(&a.map, copy, 128) == NATIVE_PTRMAP_OK);
	CHECK(NativeModelLibrary_Get(&library, 5, &model) == NATIVE_ASSET_OK && model.map->origin == copy && model.offset == 32);
#if defined(__APPLE__) && defined(__aarch64__)
	CHECK((uintptr_t)(model.map->origin + model.offset) > UINT32_MAX);
#endif
	snapshot = library;
	CHECK(MakeFixture(&b, 12, -2) == 0);
	CHECK(NativeModelLibrary_StoreMpk(&library, &b.mpk) == NATIVE_ASSET_INVALID_DATA);
	CHECK(memcmp(&library, &snapshot, sizeof(library)) == 0); // No partial ID 12 store.
	CHECK(MakeFixture(&b, 12, 227) == 0);
	CHECK(NativeModelLibrary_StoreMpk(&library, &b.mpk) == NATIVE_ASSET_INVALID_DATA);
	CHECK(memcmp(&library, &snapshot, sizeof(library)) == 0);
	CHECK(MakeFixture(&b, 12, 13) == 0);
	// Corrupt the second model's header count: valid first model cannot leak in.
	b.map.origin[82] = 0xff; b.map.origin[83] = 0xff;
	CHECK(NativeModelLibrary_StoreMpk(&library, &b.mpk) == NATIVE_ASSET_INVALID_DATA);
	CHECK(memcmp(&library, &snapshot, sizeof(library)) == 0);
	for (int reload = 0; reload < 100; reload++)
	{
		NativeModelLibrary_DropOwner(&library, &a.map);
		CHECK(MakeFixture(&a, (s16)(reload % 20), -1) == 0);
		CHECK(NativeModelLibrary_StoreMpk(&library, &a.mpk) == NATIVE_ASSET_OK);
		CHECK(NativeModelLibrary_Get(&library, reload % 20, &model) == NATIVE_ASSET_OK);
	}
	return 0;
}

static int TestDuplicateAndClear(void)
{
	struct Fixture f;
	struct NativeModelLibrary library;
	struct NativeModelView model;
	NativeModelLibrary_Reset(&library);
	CHECK(MakeFixture(&f, 0, 0) == 0);
	CHECK(NativeModelLibrary_StoreMpk(&library, &f.mpk) == NATIVE_ASSET_OK);
	CHECK(NativeModelLibrary_Get(&library, 0, &model) == NATIVE_ASSET_OK && model.offset == 64);
	NativeModelLibrary_DropOwner(&library, &f.map);
	CHECK(MakeFixture(&f, 226, 225) == 0);
	CHECK(NativeMpk_GetModel(&f.mpk, 0, &model) == NATIVE_ASSET_OK);
	CHECK(NativeModelLibrary_StoreModel(&library, &model) == NATIVE_ASSET_OK);
	CHECK(NativeModelLibrary_StoreMpk(&library, &f.mpk) == NATIVE_ASSET_OK);
	NativeModelLibrary_Clear(&library);
	CHECK(NativeModelLibrary_Get(&library, 225, &model) == NATIVE_ASSET_NOT_FOUND);
	CHECK(NativeModelLibrary_Get(&library, 226, &model) == NATIVE_ASSET_OK);
	NativeModelLibrary_DropOwner(&library, &f.map);
	CHECK(NativeModelLibrary_Get(&library, 226, &model) == NATIVE_ASSET_NOT_FOUND);
	CHECK(NativeModelLibrary_StoreMpk(&library, &f.mpk) == NATIVE_ASSET_OK);
	NativeModelLibrary_Reset(&library);
	CHECK(NativeModelLibrary_Get(&library, 226, &model) == NATIVE_ASSET_NOT_FOUND);
	CHECK(NativeModelLibrary_StoreMpk(&library, NULL) == NATIVE_ASSET_INVALID_ARGUMENT);
	CHECK(NativeModelLibrary_Get(NULL, 0, &model) == NATIVE_ASSET_INVALID_ARGUMENT && model.map == NULL);
	return 0;
}

int main(void)
{
	if (TestOverridesAndLifetime() || TestDuplicateAndClear()) return 1;
	puts("Native model library: ID lookup, transactional stores, overrides, clear, owner release, rebind and 100 reloads passed.");
	return 0;
}
