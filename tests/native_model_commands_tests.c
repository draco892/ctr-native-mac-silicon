#include <platform/native_model_commands.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s at line %d\n", #c, __LINE__); return 1; } } while (0)
static void Put16(u8 *p, u16 n) { p[0] = (u8)n; p[1] = (u8)(n >> 8); }
static void Fixture(u8 *a, u8 *ptr)
{
	memset(a, 0, 320);
	Put16(a + 18, 1); CTR_WriteU32LE(a + 20, 24);
	CTR_WriteU32LE(a + 56, 88); CTR_WriteU32LE(a + 64, 180); CTR_WriteU32LE(a + 68, 160);
	CTR_WriteU32LE(a + 88, 2);
	CTR_WriteU32LE(a + 92, 0x88010001); // Restart, cached color 0, texture 1.
	CTR_WriteU32LE(a + 96, 0x00020200); // Color 1, fresh vertex 1.
	CTR_WriteU32LE(a + 100, 0x00030400); // Color 2, fresh vertex 2.
	CTR_WriteU32LE(a + 104, 5); // Color-only: cached A=0, direct B=1.
	CTR_WriteU32LE(a + 108, 0x40040400); // Continuation reuses first of previous triangle.
	CTR_WriteU32LE(a + 112, 0x04010200); // Cached vertex in slot 1, new color 1.
	CTR_WriteU32LE(a + 116, UINT32_MAX);
	CTR_WriteU32LE(a + 160, 0x112233); CTR_WriteU32LE(a + 164, 0x445566); CTR_WriteU32LE(a + 168, 0x778899);
	CTR_WriteU32LE(a + 180, 200);
	for (u32 i = 0; i < 12; i++) a[200 + i] = (u8)(i + 1);
	const u32 slots[] = {20, 56, 64, 68, 180};
	CTR_WriteU32LE(ptr, 20);
	for (u32 i = 0; i < 5; i++) CTR_WriteU32LE(ptr + 4 + i * 4, slots[i]);
}
static int Open(u8 *a, size_t bytes, u8 *ptr, struct NativePtrMapEntry *entries,
    struct NativePtrMapView *map, struct NativeModelView *model, struct NativeModelCommands *state)
{
	CHECK(NativePtrMap_Decode(a, bytes, ptr, 24, entries, 5, map) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(map, 0, model) == NATIVE_ASSET_OK);
	return NativeModelCommands_Open(model, 0, state);
}
static int TestGolden(void)
{
	u8 storage[321], *a = storage + 1, ptr[24], snapshot[320];
	struct NativePtrMapEntry entries[5]; struct NativePtrMapView map;
	struct NativeModelView model; struct NativeModelCommands state, before;
	struct NativeModelTriangle t;
	Fixture(a, ptr); memcpy(snapshot, a, 320);
	CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_OK);
	if (sizeof(void *) > 4) CHECK((uintptr_t)a > UINT32_MAX);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_OK);
	CHECK(t.vertices[0] == 0 && t.vertices[1] == 1 && t.vertices[2] == 2);
	CHECK(t.command == 0x88010001 && t.colors[0] == 0x112233 && t.colors[1] == 0x445566 && t.colors[2] == 0x778899);
	CHECK(t.textured && t.texture.u[0] == 1 && t.texture.v[2] == 10 && t.texture.u[3] == 11 && t.texture.v[3] == 12);
	CHECK(t.texture.clut == 0x0403 && t.texture.tpage == 0x0807);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_OK);
	CHECK(t.vertices[0] == 0 && t.vertices[1] == 2 && t.vertices[2] == 3 && !t.textured);
	CHECK(t.colors[0] == 0x112233 && t.colors[1] == 0x445566 && t.colors[2] == 0x778899);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_OK);
	CHECK(t.vertices[0] == 2 && t.vertices[1] == 3 && t.vertices[2] == 0 && t.colors[2] == 0x445566);
	CHECK(state.freshVertices == 4);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_NOT_FOUND && t.command == 0);
	before = state;
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_NOT_FOUND && memcmp(&before, &state, sizeof(state)) == 0);
	CHECK(memcmp(snapshot, a, 320) == 0);
	// Copy/rebind retains only offsets; reopening gives identical triangles.
	u8 copy[320]; memcpy(copy, a, 320);
	CHECK(NativePtrMap_Rebind(&map, copy, 320) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&map, 0, &model) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Open(&model, 0, &state) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_OK && t.vertices[2] == 2 && t.textured);
	// A restart discards the prior strip length and keys its first face by
	// the restart command, even if following vertices contain another texture.
	Fixture(a, ptr); CTR_WriteU32LE(a + 108, 0x80040000);
	CTR_WriteU32LE(a + 112, 0x00050200); CTR_WriteU32LE(a + 116, 0x00060401);
	CTR_WriteU32LE(a + 120, UINT32_MAX);
	CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_OK && t.textured);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_OK && !t.textured);
	CHECK(t.vertices[0] == 3 && t.vertices[1] == 4 && t.vertices[2] == 5 && t.command == 0x80040000);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_NOT_FOUND);
	return 0;
}
static int TestRejections(void)
{
	u8 a[320], ptr[24]; struct NativePtrMapEntry entries[5]; struct NativePtrMapView map;
	struct NativeModelView model; struct NativeModelCommands state, before; struct NativeModelTriangle t;
	Fixture(a, ptr); CTR_WriteU32LE(a + 92, 0x84010001); // Uninitialized cache slot.
	CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_OK); before = state;
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_INVALID_DATA && t.command == 0);
	CHECK(memcmp(&before, &state, sizeof(state)) == 0);
	Fixture(a, ptr); CTR_WriteU32LE(a + 92, 0x88010401); // Cached color 2 outside copied cache.
	CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_INVALID_DATA);
	Fixture(a, ptr); CTR_WriteU32LE(a + 88, 129);
	CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_INVALID_DATA);
	Fixture(a, ptr); CTR_WriteU32LE(a + 116, 0);
	CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_INVALID_DATA);
	Fixture(a, ptr); CTR_WriteU32LE(a + 180, 312); // Incomplete 12-byte texture.
	CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_OK); before = state;
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_INVALID_DATA && memcmp(&before, &state, sizeof(state)) == 0);
	Fixture(a, ptr); CTR_WriteU32LE(a + 68, 316); // Incomplete copied color span.
	CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_INVALID_DATA);
	// Unlisted zero texture slot is the retail untextured fallback.
	Fixture(a, ptr); CTR_WriteU32LE(a + 180, 0); CTR_WriteU32LE(ptr, 16);
	CHECK(NativePtrMap_Decode(a, 320, ptr, 20, entries, 5, &map) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&map, 0, &model) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Open(&model, 0, &state) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_OK && !t.textured);
	// Listed zero references origin, rather than the optional-null fallback.
	Fixture(a, ptr); CTR_WriteU32LE(a + 180, 0);
	CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_OK && t.textured);
	// Nonzero pointer absent from PTR is rejected when its texture is used.
	CTR_WriteU32LE(ptr, 16);
	CTR_WriteU32LE(a + 180, 200);
	CHECK(NativePtrMap_Decode(a, 320, ptr, 20, entries, 5, &map) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&map, 0, &model) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Open(&model, 0, &state) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_INVALID_DATA);
	// No colors/texture tables are needed by an empty list.
	Fixture(a, ptr); CTR_WriteU32LE(a + 88, 0); CTR_WriteU32LE(a + 92, UINT32_MAX);
	CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Next(&state, &t) == NATIVE_ASSET_NOT_FOUND && state.freshVertices == 0);
	Fixture(a, ptr); CTR_WriteU32LE(a + 56, 0); CTR_WriteU32LE(ptr, 16);
	const u32 absentSlots[] = {20, 64, 68, 180};
	for (u32 i = 0; i < 4; i++) CTR_WriteU32LE(ptr + 4 + i * 4, absentSlots[i]);
	CHECK(NativePtrMap_Decode(a, 320, ptr, 20, entries, 5, &map) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&map, 0, &model) == NATIVE_ASSET_OK);
	CHECK(NativeModelCommands_Open(&model, 0, &state) == NATIVE_ASSET_NOT_FOUND && state.map == NULL);
	CHECK(NativeModelCommands_Next(NULL, &t) == NATIVE_ASSET_INVALID_ARGUMENT);
	CHECK(NativeModelCommands_Open(NULL, 0, &state) == NATIVE_ASSET_INVALID_ARGUMENT);
	return 0;
}
static int TestMutations(void)
{
	u32 random = 0x6a09e667;
	for (u32 trial = 0; trial < 2000; trial++)
	{
		u8 storage[321], *a = storage + 1, ptr[24], copy[320];
		struct NativePtrMapEntry entries[5]; struct NativePtrMapView map; struct NativeModelView model;
		struct NativeModelCommands state, before; struct NativeModelTriangle t;
		Fixture(a, ptr);
		for (u32 i = 92; i < 116; i += 4) { random = random * 1664525u + 1013904223u; CTR_WriteU32LE(a + i, random); }
		memcpy(copy, a, 320);
		CHECK(Open(a, 320, ptr, entries, &map, &model, &state) == NATIVE_ASSET_OK);
		unsigned calls = 0;
		for (;;)
		{
			before = state;
			enum NativeAssetResult status = NativeModelCommands_Next(&state, &t);
			CHECK(++calls <= 7);
			if (status == NATIVE_ASSET_NOT_FOUND) break;
			if (status != NATIVE_ASSET_OK) { CHECK(memcmp(&before, &state, sizeof(state)) == 0 && t.command == 0); break; }
			CHECK(state.cursor > before.cursor && t.vertices[0] < state.freshVertices && t.vertices[1] < state.freshVertices && t.vertices[2] < state.freshVertices);
		}
		CHECK(memcmp(a, copy, 320) == 0);
	}
	return 0;
}
int main(void)
{
	if (TestGolden() || TestRejections() || TestMutations()) return 1;
	puts("Native draw commands: triangle strips, continuation, cache reuse, colors, texture metadata, bounds and 2000 mutations passed.");
	return 0;
}
