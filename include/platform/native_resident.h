#ifndef PLATFORM_NATIVE_RESIDENT_H
#define PLATFORM_NATIVE_RESIDENT_H

// Actual game declarations, without the full game's unresolved layout guard.
#include <psx/psx_prelude.h>
#include <ctr_math.h>
#include <ctr_scratchpad.h>
#include <namespace_Coll.h>
#include <namespace_Decal.h>
#include <namespace_List.h>
#include <namespace_Bots.h>
#include <namespace_Instance.h>
#include <namespace_Level.h>
#include <namespace_PushBuffer.h>
#include <namespace_Camera.h>
#include <namespace_Vehicle.h>
#include <platform/native_asset_readers.h>

// Pointer-bearing retail records are byte sequences, never resident arrays.
#define CTR_WIRE_RECORD(name, size) \
	struct name                     \
	{                               \
		u8 bytes[size];             \
	};                              \
	CTR_STATIC_ASSERT(sizeof(struct name) == (size))
CTR_WIRE_RECORD(CtrWireLevel, NATIVE_LEVEL_BYTES);
CTR_WIRE_RECORD(CtrWireModel, NATIVE_MODEL_BYTES);
CTR_WIRE_RECORD(CtrWireModelHeader, NATIVE_MODEL_HEADER_BYTES);
CTR_WIRE_RECORD(CtrWireInstDef, NATIVE_INSTANCE_DEF_BYTES);
CTR_WIRE_RECORD(CtrWireMesh, NATIVE_MESH_BYTES);
CTR_WIRE_RECORD(CtrWireQuad, NATIVE_QUAD_BYTES);
CTR_WIRE_RECORD(CtrWireBsp, NATIVE_BSP_BYTES);
CTR_WIRE_RECORD(CtrWireModelAnim, 0x18);
CTR_WIRE_RECORD(CtrWireNavHeader, 0x4c);
CTR_WIRE_RECORD(CtrWireSkybox, 0x38);
CTR_WIRE_RECORD(CtrWireWaterVert, 0x8);
CTR_WIRE_RECORD(CtrWireSCVert, 0x10);
CTR_WIRE_RECORD(CtrWirePVS, 0x10);
CTR_WIRE_RECORD(CtrWireSpawnType2, 0x8);
CTR_WIRE_RECORD(CtrWireSpawnType1, 0x4);
CTR_WIRE_RECORD(CtrWireIconGroup, 0x14);
#undef CTR_WIRE_RECORD

enum NativeResidentKind
{
	NR_BYTES,
	NR_WORDS,
	NR_MODEL,
	NR_MODEL_HEADER,
	NR_MODEL_ANIM,
	NR_INSTDEF,
	NR_MESH,
	NR_QUAD,
	NR_BSP,
	NR_VERTEX,
	NR_WATER,
	NR_SC,
	NR_SKYBOX,
	NR_NAV,
	NR_ICONGROUP4,
	NR_TEXTURE_LAYOUT,
	NR_ICON,
	NR_SPAWN1,
	NR_NAVFRAME,
	NR_FRAME,
	NR_SHORT_VERTEX,
	NR_FACE,
	NR_OVERT,
	NR_POS,
	NR_POSROT,
	NR_PVS,
	NR_SPAWN2,
	NR_CHECKPOINT,
	NR_ANIMTEX,
	NR_VISMEM,
	NR_TEXLOOKUP,
	NR_MODELS,
	NR_INSTDEFS,
	NR_ANIMATIONS,
	NR_TEXTURES,
	NR_NAVS,
	NR_KIND_COUNT
};

// A binding describes an already allocated, correctly typed resident array.
// The caller must keep it alive and finish decoding nested objects before use.
// Wire and resident strides are inferred from kind. Interior array references
// are mapped by element index, never by adding a wire byte offset to a host ptr.
// Variable-length records with trailing data need individual header bindings;
// the fixed header stride does not describe their next variable-length record.
struct NativeResidentBinding
{
	enum NativeResidentKind kind;
	u32 offset;
	size_t count;
	void *resident;
};
struct NativeResidentContext
{
	const struct NativePtrMapView *map;
	const struct NativeResidentBinding *bindings;
	size_t count;
};

// Header/record materialization only: no allocations, publications or PTR
// patching. Little-endian scalar fields are decoded explicitly. Typed nested
// references must have a matching binding; raw byte/word spans may borrow the
// immutable asset. Missing nonzero PTR slots, bad spans and ambiguous bindings
// fail. Listed target zero remains origin; an unlisted zero slot means NULL.
// Outputs remain unchanged on error. Output, bindings and asset must be disjoint.
#define NR_DECODE_DECL(type, name) enum NativeAssetResult NativeResident_Decode##name(const struct NativeResidentContext *, u32 offset, struct type *out)
NR_DECODE_DECL(Level, Level);
NR_DECODE_DECL(Model, Model);
NR_DECODE_DECL(ModelHeader, ModelHeader);
NR_DECODE_DECL(ModelAnim, ModelAnim);
NR_DECODE_DECL(InstDef, InstDef);
NR_DECODE_DECL(mesh_info, Mesh);
NR_DECODE_DECL(QuadBlock, Quad);
NR_DECODE_DECL(BSP, Bsp);
NR_DECODE_DECL(BSP, BspHitbox);
NR_DECODE_DECL(WaterVert, Water);
NR_DECODE_DECL(SCVert, SC);
NR_DECODE_DECL(PVS, PVS);
NR_DECODE_DECL(NavHeader, NavHeader);
NR_DECODE_DECL(Skybox, Skybox);
NR_DECODE_DECL(SpawnType2, SpawnPositions);
NR_DECODE_DECL(SpawnType2, SpawnPosRot);
#undef NR_DECODE_DECL
#endif
