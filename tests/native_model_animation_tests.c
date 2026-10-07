#include <platform/native_model_animation.h>

#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s at line %d\n", #c, __LINE__); return 1; } } while (0)

struct Fixture
{
	u8 storage[257], ptr[28];
	struct NativePtrMapEntry entries[6];
	struct NativePtrMapView map;
	struct NativeModelView model;
};

static void Put16(u8 *p, u16 n) { p[0] = (u8)n; p[1] = (u8)(n >> 8); }

static int Decode(struct Fixture *f)
{
	CHECK(NativePtrMap_Decode(f->storage + 1, 256, f->ptr, 28, f->entries, 6, &f->map) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&f->map, 0, &f->model) == NATIVE_ASSET_OK);
	return 0;
}

static int Setup(struct Fixture *f, u16 frameCount)
{
	u8 *a = f->storage + 1;
	const u32 slots[] = {20, 80, 88, 112, 60, 72};
	memset(a, 0, 256);
	Put16(a + 18, 1); CTR_WriteU32LE(a + 20, 24);
	CTR_WriteU32LE(a + 24 + 0x34, 1); CTR_WriteU32LE(a + 80, 88);
	CTR_WriteU32LE(a + 88, 92);
	memcpy(a + 92, "abcdefghijklmnop", 16);
	Put16(a + 108, frameCount); Put16(a + 110, 32);
	CTR_WriteU32LE(a + 112, 212);
	CTR_WriteU32LE(a + 212, 0x12345678); CTR_WriteU32LE(a + 216, 0x87654321);
	for (u32 i = 0; i < 3; i++)
	{
		Put16(a + 116 + i * 32, (u16)(0x8000 + i));
		CTR_WriteU32LE(a + 116 + i * 32 + 24, 28);
		a[116 + i * 32 + 28] = (u8)(i + 1);
	}
	CTR_WriteU32LE(a + 60, 224); CTR_WriteU32LE(a + 72, 212);
	CTR_WriteU32LE(a + 248, 28);
	CTR_WriteU32LE(f->ptr, 24);
	for (u32 i = 0; i < 6; i++) CTR_WriteU32LE(f->ptr + 4 + i * 4, slots[i]);
	return Decode(f);
}

static int TestFrames(void)
{
	struct Fixture f;
	struct NativeAnimationView animation;
	struct NativeFrameView frame;
	struct NativeFrameSelection selection;
	u32 delta;
	u8 copy[256], original[256];
	CHECK(Setup(&f, 3) == 0);
	memcpy(original, f.map.origin, 256);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_OK);
	CHECK(animation.logicalFrameCount == 3 && animation.storedFrameCount == 3 && !animation.interpolated);
	CHECK(animation.hasDelta && strcmp(animation.name, "abcdefghijklmnop") == 0);
	CHECK(NativeAnimation_GetStoredFrame(&animation, 2, &frame) == NATIVE_ASSET_OK);
	CHECK(frame.wire == f.map.origin + 180 && frame.vertices == f.map.origin + 208 && frame.vertexBytes == 4);
	CHECK(frame.position[0] == INT16_MIN + 2 && frame.vertices[0] == 3);
	CHECK(NativeAnimation_GetStoredFrame(&animation, 3, &frame) == NATIVE_ASSET_INDEX_OUT_OF_RANGE && frame.wire == NULL);
	CHECK(NativeAnimation_SelectFrame(&animation, UINT32_MAX, &selection) == NATIVE_ASSET_OK);
	CHECK(selection.logicalIndex == 2 && selection.storedIndex == 2 && !selection.hasNext && selection.next.wire == NULL);
	CHECK(NativeAnimation_ReadDeltaWord(&animation, 1, &delta) == NATIVE_ASSET_OK && delta == 0x87654321);
	CHECK(NativeAnimation_ReadDeltaWord(&animation, UINT32_MAX, &delta) == NATIVE_ASSET_INVALID_DATA && delta == 0);
	CHECK(NativeModel_GetStaticFrame(&f.model, 0, 4, &frame) == NATIVE_ASSET_OK && frame.vertices == f.map.origin + 252);
	CHECK(NativeModel_GetStaticFrame(&f.model, 0, 5, &frame) == NATIVE_ASSET_INVALID_DATA && frame.wire == NULL);
	CHECK(NativeModel_ReadStaticDeltaWord(&f.model, 0, 0, &delta) == NATIVE_ASSET_OK && delta == 0x12345678);
	CHECK(memcmp(original, f.map.origin, 256) == 0);
	memcpy(copy, f.map.origin, 256);
	CHECK(NativePtrMap_Rebind(&f.map, copy, 256) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&f.map, 0, &f.model) == NATIVE_ASSET_OK);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_OK);
	CHECK(NativeAnimation_GetStoredFrame(&animation, 0, &frame) == NATIVE_ASSET_OK && frame.wire == copy + 116);
#if defined(__APPLE__) && defined(__aarch64__)
	CHECK((uintptr_t)frame.vertices > UINT32_MAX);
#endif
	CHECK(Setup(&f, 0x8005) == 0);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_OK);
	CHECK(animation.interpolated && animation.logicalFrameCount == 5 && animation.storedFrameCount == 3);
	CHECK(NativeAnimation_SelectFrame(&animation, 1, &selection) == NATIVE_ASSET_OK);
	CHECK(selection.hasNext && selection.storedIndex == 0 && selection.current.vertices[0] == 1 && selection.next.vertices[0] == 2);
	CHECK(NativeAnimation_SelectFrame(&animation, 4, &selection) == NATIVE_ASSET_OK && !selection.hasNext && selection.storedIndex == 2);
	CHECK(Setup(&f, 0x8004) == 0);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_OK && animation.storedFrameCount == 3);
	CHECK(NativeAnimation_SelectFrame(&animation, UINT32_MAX, &selection) == NATIVE_ASSET_OK && selection.logicalIndex == 3 && selection.hasNext);
	return 0;
}

static int TestFailures(void)
{
	struct Fixture f;
	struct NativeAnimationView animation;
	struct NativeFrameView frame;
	struct NativeFrameSelection selection;
	u32 delta;
	CHECK(Setup(&f, 0) == 0);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_INVALID_DATA && animation.map == NULL);
	CHECK(Setup(&f, 32767) == 0);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_INVALID_DATA);
	CHECK(Setup(&f, 3) == 0);
	Put16(f.map.origin + 110, 27);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_INVALID_DATA);
	Put16(f.map.origin + 110, 32);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_OK);
	CTR_WriteU32LE(f.map.origin + 140, 33);
	CHECK(NativeAnimation_GetStoredFrame(&animation, 0, &frame) == NATIVE_ASSET_INVALID_DATA && frame.vertices == NULL);
	CHECK(NativeAnimation_SelectFrame(&animation, 0, &selection) == NATIVE_ASSET_INVALID_DATA && selection.current.wire == NULL);
	CTR_WriteU32LE(f.map.origin + 140, 0);
	CHECK(NativeAnimation_GetStoredFrame(&animation, 0, &frame) == NATIVE_ASSET_INVALID_DATA);
	// A static offset may differ from 28 and must still fit the requested span.
	CTR_WriteU32LE(f.map.origin + 60, 216);
	CTR_WriteU32LE(f.map.origin + 240, 34);
	CHECK(Decode(&f) == 0);
	CHECK(NativeModel_GetStaticFrame(&f.model, 0, 6, &frame) == NATIVE_ASSET_OK && frame.vertices == f.map.origin + 250);
	// Remove delta relocation and clear the raw field: optional uncompressed data.
	CTR_WriteU32LE(f.ptr, 8); // Only model->headers and header->animations slots remain.
	CHECK(NativePtrMap_Decode(f.map.origin, 256, f.ptr, 28, f.entries, 6, &f.map) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_INVALID_DATA); // Unlisted nonzero animation pointer.
	CTR_WriteU32LE(f.map.origin + 88, 0);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_NOT_FOUND);
	CHECK(Setup(&f, 3) == 0);
	// Omit only the animation's delta slot, retaining model/table/frame slots.
	CTR_WriteU32LE(f.ptr + 16, 60); CTR_WriteU32LE(f.ptr + 20, 72); CTR_WriteU32LE(f.ptr, 20);
	CTR_WriteU32LE(f.map.origin + 112, 0);
	CHECK(Decode(&f) == 0);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_OK && !animation.hasDelta);
	CHECK(NativeAnimation_ReadDeltaWord(&animation, 0, &delta) == NATIVE_ASSET_NOT_FOUND && delta == 0);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 1, &animation) == NATIVE_ASSET_INDEX_OUT_OF_RANGE);
	CHECK(NativeModel_GetAnimation(NULL, 0, 0, &animation) == NATIVE_ASSET_INVALID_ARGUMENT);
	CHECK(Setup(&f, 3) == 0);
	CTR_WriteU32LE(f.map.origin + 112, 0); // Listed zero is origin, not absent delta.
	CHECK(Decode(&f) == 0);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_OK && animation.hasDelta);
	CHECK(NativeAnimation_ReadDeltaWord(&animation, 0, &delta) == NATIVE_ASSET_OK && delta == 0);
	return 0;
}

static int TestUnsignedStride(void)
{
	struct Fixture f;
	u8 large[116 + 0x8000];
	struct NativeAnimationView animation;
	struct NativeFrameView frame;
	CHECK(Setup(&f, 1) == 0);
	memset(large, 0, sizeof(large));
	memcpy(large, f.map.origin, 256);
	Put16(large + 110, 0x8000); // Negative in old s16 declaration, unsigned in renderer.
	large[sizeof(large) - 1] = 0x5a;
	CHECK(NativePtrMap_Decode(large, sizeof(large), f.ptr, 28, f.entries, 6, &f.map) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&f.map, 0, &f.model) == NATIVE_ASSET_OK);
	CHECK(NativeModel_GetAnimation(&f.model, 0, 0, &animation) == NATIVE_ASSET_OK && animation.frameStride == 0x8000);
	CHECK(NativeAnimation_GetStoredFrame(&animation, 0, &frame) == NATIVE_ASSET_OK);
	CHECK(frame.vertexBytes == 0x8000 - 28 && frame.vertices[frame.vertexBytes - 1] == 0x5a);
	return 0;
}

static int TestMalformedCorpus(void)
{
	struct Fixture f;
	struct NativeAnimationView animation;
	struct NativeFrameView frame;
	struct NativeFrameSelection selected;
	u32 random = 0x6a09e667;
	const u32 fields[] = {20, 80, 88, 112, 60, 72, 108, 110, 140, 172, 204, 248};
	for (u32 trial = 0; trial < 2000; trial++)
	{
		CHECK(Setup(&f, 0x8005) == 0);
		random = random * 1664525u + 1013904223u;
		CTR_WriteU32LE(f.map.origin + fields[trial % 12], trial % 2 ? random : random % 320);
		if (NativePtrMap_Decode(f.storage + 1, 256, f.ptr, 28, f.entries, 6, &f.map) != NATIVE_PTRMAP_OK) continue;
		if (NativeModel_Open(&f.map, 0, &f.model) != NATIVE_ASSET_OK) continue;
		enum NativeAssetResult status = NativeModel_GetAnimation(&f.model, 0, 0, &animation);
		if (status != NATIVE_ASSET_OK) { CHECK(animation.map == NULL); continue; }
		status = NativeAnimation_GetStoredFrame(&animation, random % animation.storedFrameCount, &frame);
		if (status == NATIVE_ASSET_OK)
		{
			CHECK(frame.vertices >= f.map.origin && frame.vertices <= f.map.origin + 256);
			CHECK(frame.vertexBytes <= (size_t)(f.map.origin + 256 - frame.vertices));
		}
		else CHECK(frame.wire == NULL && frame.vertices == NULL);
		if (NativeAnimation_SelectFrame(&animation, random, &selected) != NATIVE_ASSET_OK)
			CHECK(selected.current.wire == NULL && selected.next.wire == NULL);
	}
	return 0;
}

int main(void)
{
	if (TestFrames() || TestFailures() || TestUnsignedStride() || TestMalformedCorpus()) return 1;
	puts("Native animation readers: unaligned static/animated frames, interpolation, delta spans, rebind and 2000 malformed fixtures passed.");
	return 0;
}
