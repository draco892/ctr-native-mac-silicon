#include <platform/native_resident.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)                                                 \
	do                                                           \
	{                                                            \
		if (!(c))                                                \
		{                                                        \
			fprintf(stderr, "failed: %s at %d\n", #c, __LINE__); \
			return 1;                                            \
		}                                                        \
	} while (0)
struct Fixture
{
	_Alignas(max_align_t) u8 bytes[4096];
	u8 ptr[260];
	size_t count;
	struct NativePtrMapEntry entries[64];
	struct NativePtrMapView map;
};
static void Init(struct Fixture *f)
{
	memset(f, 0, sizeof(*f));
}
static void Slot(struct Fixture *f, u32 at, u32 target)
{
	CTR_WriteU32LE(f->bytes + at, target);
	CTR_WriteU32LE(f->ptr + 4 + f->count++ * 4, at);
}
static int Decode(struct Fixture *f)
{
	CTR_WriteU32LE(f->ptr, (u32)f->count * 4);
	return NativePtrMap_Decode(f->bytes, sizeof(f->bytes), f->ptr, 4 + f->count * 4, f->entries, 64, &f->map) == NATIVE_PTRMAP_OK;
}
static int LevelAndModels(void)
{
	struct Fixture f;
	Init(&f);
	struct InstDef *defs = calloc(2, sizeof(*defs));
	struct Model models[2] = {0};
	struct ModelHeader headers[2] = {0};
	struct Model *modelPtrs[2] = {&models[0], &models[1]};
	struct mesh_info mesh = {0};
	CHECK(defs);
#if defined(CTR_NATIVE_HOST64)
	CHECK((uintptr_t)defs > UINT32_MAX);
#endif
	Slot(&f, 0, 0x200);
	Slot(&f, 16, 0x240);
	Slot(&f, 24, 0x300);
	CTR_WriteU32LE(f.bytes + 12, 2);
	CTR_WriteU32LE(f.bytes + 20, 2);
	Slot(&f, 0x250, 0x340);
	Slot(&f, 0x290, 0x358); // model records have 24-byte wire stride.
	for (unsigned i = 0; i < 2; i++)
	{
		u32 at = 0x240 + i * 0x40;
		memcpy(f.bytes + at, i ? "crate" : "fruit", 6);
		CTR_WriteU16LE(f.bytes + at + 20, (u16)-37);
		CTR_WriteU16LE(f.bytes + at + 22, 4096);
		CTR_WriteU32LE(f.bytes + at + 28, 0xaabbccdd);
		CTR_WriteU32LE(f.bytes + at + 32, 0x8021);
		CTR_WriteU32LE(f.bytes + at + 36, (u32)-17);
		CTR_WriteU32LE(f.bytes + at + 40, 53);
		CTR_WriteU32LE(f.bytes + at + 44, 0xdeadbeef); // runtime ptrInstance scratch is ignored.
		CTR_WriteU16LE(f.bytes + at + 48, (u16)-1234);
		CTR_WriteU16LE(f.bytes + at + 54, 765);
		CTR_WriteU32LE(f.bytes + at + 60, 42 + i);
		u32 m = 0x340 + i * 24;
		memcpy(f.bytes + m, "test model", 11);
		CTR_WriteU16LE(f.bytes + m + 16, (u16)(20 + i));
		CTR_WriteU16LE(f.bytes + m + 18, 1);
		Slot(&f, m + 20, 0x400 + i * 64);
	}
	CTR_WriteU16LE(f.bytes + 0x400 + 24, 4096);
	CTR_WriteU32LE(f.bytes + 0x400 + 16, 0x12345678);
	Slot(&f, 0x400 + 32, 0x600);
	Slot(&f, 0x400 + 48, 0x604);
	Slot(&f, 0x400 + 44, 0x608);
	CTR_WriteU32LE(f.bytes + 0xd8, 0x88776655);
	CTR_WriteU32LE(f.bytes + 0xdc, 7);
	CTR_WriteU16LE(f.bytes + 0x6c, (u16)-123);
	CTR_WriteU16LE(f.bytes + 0x178 + 4, 321); // stars[0]
	for (unsigned i = 0; i < 3; i++)
	{
		CTR_WriteU16LE(f.bytes + 0x48 + i * 12, (u16)(i * 17));
		CTR_WriteU32LE(f.bytes + 0x50 + i * 12, 0x10203040 + i);
	}
	CHECK(Decode(&f));
	u8 saved[4096];
	memcpy(saved, f.bytes, sizeof(saved));
	struct NativeResidentBinding bindings[] = {{NR_MESH, 0x200, 1, &mesh},
	                                           {NR_INSTDEF, 0x240, 2, defs},
	                                           {NR_MODELS, 0x300, 2, modelPtrs},
	                                           {NR_MODEL, 0x340, 2, models},
	                                           {NR_MODEL_HEADER, 0x400, 2, headers}};
	struct NativeResidentContext c = {.map = &f.map, .bindings = bindings, .count = 5};
	struct Level level;
	CHECK(NativeResident_DecodeLevel(&c, 0, &level) == NATIVE_ASSET_OK);
	CHECK(level.ptr_mesh_info == &mesh && level.ptrInstDefs == defs && level.ptrModelsPtrArray == modelPtrs);
	CHECK(level.numInstances == 2 && level.numModels == 2 && level.clearColorRGBA == 0x88776655 && level.configFlags == 7);
	CHECK(level.DriverSpawn[0].pos.x == -123 && level.stars.numStars == 321);
	for (unsigned i = 0; i < 3; i++)
		CHECK(level.glowGradient[i].pointFrom == (s16)(i * 17) && level.glowGradient[i].colorTo == 0x10203040 + i);
	for (unsigned i = 0; i < 2; i++)
	{
		CHECK(NativeResident_DecodeInstDef(&c, 0x240 + i * 64, &defs[i]) == NATIVE_ASSET_OK);
		CHECK(defs[i].model == &models[i] && defs[i].ptrInstance == NULL && defs[i].residentPeer == NULL);
		CHECK(defs[i].scale.x == -37 && defs[i].scale.y == 4096 && defs[i].colorRGBA == 0xaabbccdd);
		CHECK(defs[i].unk24 == -17 && defs[i].unk28 == 53 && defs[i].pos.x == -1234 && defs[i].rot.x == 765 && defs[i].modelID == (int)(42 + i));
		CHECK(NativeResident_DecodeModel(&c, 0x340 + i * 24, &models[i]) == NATIVE_ASSET_OK && models[i].headers == &headers[i]);
		CHECK(NativeResident_DecodeModelHeader(&c, 0x400 + i * 64, &headers[i]) == NATIVE_ASSET_OK);
	}
	CHECK(headers[0].ptrCommandList == (CtrRuntimeAddress)(f.bytes + 0x600) && headers[0].unk3 == (CtrRuntimeAddress)(f.bytes + 0x604));
	CHECK(headers[0].ptrColors == (u32 *)(f.bytes + 0x608) && headers[0].scale.x == 4096 && headers[0].unk1 == 0x12345678);
	CHECK(memcmp(f.bytes, saved, sizeof(saved)) == 0);
	struct Level unchanged = level;
	c.count = 1;
	CHECK(NativeResident_DecodeLevel(&c, 0, &level) == NATIVE_ASSET_NOT_FOUND && memcmp(&level, &unchanged, sizeof(level)) == 0);
	c.count = 5;
	bindings[1].count = 1;
	CHECK(NativeResident_DecodeLevel(&c, 0, &level) == NATIVE_ASSET_NOT_FOUND);
	bindings[1].count = 2;
	bindings[1].resident = (u8 *)defs + 1;
	CHECK(NativeResident_DecodeLevel(&c, 0, &level) == NATIVE_ASSET_INVALID_DATA);
	bindings[1].resident = defs;
	CHECK(NativeResident_DecodeLevel(&c, 4000, &level) == NATIVE_ASSET_INVALID_ARGUMENT);
	CHECK(NativeResident_DecodeLevel(&c, 0, (struct Level *)f.bytes) == NATIVE_ASSET_INVALID_ARGUMENT);
	CHECK(memcmp(&level, &unchanged, sizeof(level)) == 0 && memcmp(f.bytes, saved, sizeof(saved)) == 0);
	free(defs);
	return 0;
}
static int Geometry(void)
{
	struct Fixture f;
	Init(&f);
	struct QuadBlock quads[2] = {0};
	struct BSP bsp[2] = {0};
	struct LevVertex vertices[2] = {0};
	struct InstDef def = {0};
	struct OVert ocean = {0};
	struct NavFrame nav[2] = {0};
	struct IconGroup4 texture = {0};
	struct AnimTex animation = {0};
	struct NativeResidentBinding bindings[] = {{NR_QUAD, 0x200, 2, quads},          {NR_BSP, 0x300, 2, bsp},           {NR_VERTEX, 0x400, 2, vertices},
	                                           {NR_INSTDEF, 0x500, 1, &def},        {NR_OVERT, 0x580, 1, &ocean},      {NR_NAVFRAME, 0x600, 2, nav},
	                                           {NR_ICONGROUP4, 0x700, 1, &texture}, {NR_ANIMTEX, 0x780, 1, &animation}};
	struct NativeResidentContext c = {.map = &f.map, .bindings = bindings, .count = 8};
	CTR_WriteU32LE(f.bytes, 2);
	CTR_WriteU32LE(f.bytes + 4, 2);
	CTR_WriteU32LE(f.bytes + 28, 2);
	Slot(&f, 12, 0x200);
	Slot(&f, 16, 0x400);
	Slot(&f, 24, 0x300);
	CTR_WriteU16LE(f.bytes + 0x200 + 60, (u16)-7);
	f.bytes[0x200 + 62] = 83;
	CTR_WriteU16LE(f.bytes + 0x200 + 72, (u16)-1000);
	Slot(&f, 0x200 + 28, 0x700);
	Slot(&f, 0x200 + 32, 0x781);
	CTR_WriteU16LE(f.bytes + 0x300, 1);
	CTR_WriteU32LE(f.bytes + 0x300 + 24, 1);
	Slot(&f, 0x300 + 28, 0x25c);
	CTR_WriteU16LE(f.bytes + 0x320 + 24, 0x4012); // branch scalar child ID stays 16 bits.
	Slot(&f, 0x800, 0x410);
	Slot(&f, 0x804, 0x580); // water to second vertex.
	Slot(&f, 0x810, 0x400);
	CTR_WriteU32LE(f.bytes + 0x814, 0x12345678);
	CTR_WriteU32LE(f.bytes + 0x81c, 0xff345678);
	Slot(&f, 0x888, 0x614);
	CTR_WriteU16LE(f.bytes + 0x880 + 2, 2);
	CTR_WriteU16LE(f.bytes + 0x880 + 12, (u16)-42);
	CTR_WriteU16LE(f.bytes + 0x900 + 22, 123);
	Slot(&f, 0x900 + 28, 0x500);
	CHECK(Decode(&f));
	u8 saved[4096];
	memcpy(saved, f.bytes, sizeof(saved));
	struct mesh_info mesh;
	CHECK(NativeResident_DecodeMesh(&c, 0, &mesh) == NATIVE_ASSET_OK && mesh.ptrQuadBlockArray == quads && mesh.ptrVertexArray == vertices &&
	      mesh.bspRoot == bsp);
	CHECK(NativeResident_DecodeQuad(&c, 0x200, &quads[0]) == NATIVE_ASSET_OK);
	CHECK(quads[0].blockID == -7 && quads[0].checkpointIndex == 83 && quads[0].triNormalVecDividend[0] == -1000);
	CHECK(quads[0].ptr_texture_mid[0] == &texture && (uintptr_t)quads[0].ptr_texture_mid[1] == ((uintptr_t)&animation | 1u));
	CHECK(NativeResident_DecodeBsp(&c, 0x300, &bsp[0]) == NATIVE_ASSET_OK && bsp[0].data.leaf.ptrQuadBlockArray == &quads[1]);
	CHECK(NativeResident_DecodeBsp(&c, 0x320, &bsp[1]) == NATIVE_ASSET_OK && bsp[1].data.branch.childID[0] == 0x4012);
	struct BSP hitbox;
	CHECK(NativeResident_DecodeBspHitbox(&c, 0x900, &hitbox) == NATIVE_ASSET_OK && hitbox.data.hitbox.instDef == &def && hitbox.data.hitbox.radius == 123);
	struct WaterVert water;
	CHECK(NativeResident_DecodeWater(&c, 0x800, &water) == NATIVE_ASSET_OK && water.v == &vertices[1] && water.w == &ocean);
	struct SCVert sc;
	CHECK(NativeResident_DecodeSC(&c, 0x810, &sc) == NATIVE_ASSET_OK && sc.v == vertices && (u32)sc.offset_color_rgba == 0xff345678 &&
	      sc.offset_pos_xy == 0x12345678);
	struct NavHeader header;
	CHECK(NativeResident_DecodeNavHeader(&c, 0x880, &header) == NATIVE_ASSET_OK && header.last == &nav[1] && header.numPoints == 2 &&
	      header.rampPhys1[0] == -42);
	CHECK(memcmp(f.bytes, saved, sizeof(saved)) == 0);
	struct QuadBlock unchanged = quads[0];
	bindings[7].kind = NR_WORDS;
	CHECK(NativeResident_DecodeQuad(&c, 0x200, &quads[0]) == NATIVE_ASSET_NOT_FOUND && memcmp(&quads[0], &unchanged, sizeof(unchanged)) == 0);
	return 0;
}
static int AuxiliaryAndErrors(void)
{
	struct Fixture f;
	Init(&f);
	CHECK(Decode(&f));
	struct NativeResidentContext c = {.map = &f.map, .bindings = NULL, .count = 0};
	struct ModelAnim anim;
	struct PVS pvs;
	struct Skybox sky;
	struct SpawnType2 spawn;
	CHECK(NativeResident_DecodeModelAnim(&c, 0, &anim) == NATIVE_ASSET_OK);
	CHECK(NativeResident_DecodePVS(&c, 0, &pvs) == NATIVE_ASSET_OK);
	CHECK(NativeResident_DecodeSkybox(&c, 0, &sky) == NATIVE_ASSET_OK);
	CHECK(NativeResident_DecodeSpawnPositions(&c, 0, &spawn) == NATIVE_ASSET_OK);
	CHECK(NativeResident_DecodeSpawnPosRot(&c, 0, &spawn) == NATIVE_ASSET_OK);
	struct ModelHeader header = {0};
	struct NativeResidentBinding b[2] = {{NR_MODEL_HEADER, 0, 1, &header}, {NR_MODEL_HEADER, 0, 1, &header}};
	// A listed target zero is an origin reference, not a null pointer.
	CTR_WriteU16LE(f.bytes + 0x100 + 18, 1);
	Slot(&f, 0x100 + 20, 0);
	CHECK(Decode(&f));
	c.bindings = b;
	c.count = 1;
	struct Model model;
	CHECK(NativeResident_DecodeModel(&c, 0x100, &model) == NATIVE_ASSET_OK && model.headers == &header);
	struct Model unchanged = model;
	c.count = 2;
	CHECK(NativeResident_DecodeModel(&c, 0x100, &model) == NATIVE_ASSET_INVALID_DATA && memcmp(&unchanged, &model, sizeof(model)) == 0);
	c.count = 1;
	b[0].resident = f.bytes;
	CHECK(NativeResident_DecodeModel(&c, 0x100, &model) == NATIVE_ASSET_INVALID_DATA);
	b[0].resident = &header;
	CTR_WriteU32LE(f.bytes + 0x100 + 20, 4090);
	CHECK(Decode(&f));
	CHECK(NativeResident_DecodeModel(&c, 0x100, &model) == NATIVE_ASSET_INVALID_DATA);
	Init(&f);
	CTR_WriteU32LE(f.bytes + 0x100 + 20, 64);
	CHECK(Decode(&f));
	CHECK(NativeResident_DecodeModel(&c, 0x100, &model) == NATIVE_ASSET_INVALID_DATA); // nonzero unlisted slot.
	Init(&f);
	CHECK(Decode(&f));
	CTR_WriteU32LE(f.bytes, (u32)-1);
	struct mesh_info mesh;
	memset(&mesh, 0xa5, sizeof(mesh));
	struct mesh_info old = mesh;
	CHECK(NativeResident_DecodeMesh(&c, 0, &mesh) == NATIVE_ASSET_INVALID_DATA && memcmp(&old, &mesh, sizeof(mesh)) == 0);
	CHECK(NativeResident_DecodeModel(NULL, 0, &model) == NATIVE_ASSET_INVALID_ARGUMENT);
	return 0;
}
int main(void)
{
	if (LevelAndModels() || Geometry() || AuxiliaryAndErrors())
		return 1;
	puts("Resident records: LEV/MPK scalars, typed arrays/interior references, tagged textures, BSP/hitboxes, immutable wire bytes and transactional rejection "
	     "OK");
	return 0;
}
