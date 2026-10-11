#include <platform/native_resident.h>
#include <string.h>
#include <limits.h>

struct ResidentKindLayout
{
	size_t wire, host, alignment;
};
#define K(w, type) {w, sizeof(type), _Alignof(type)}
static const struct ResidentKindLayout Residentlayouts[NR_KIND_COUNT] = {
    [NR_BYTES] = K(1, u8),
    [NR_WORDS] = K(4, u32),
    [NR_MODEL] = K(0x18, struct Model),
    [NR_MODEL_HEADER] = K(0x40, struct ModelHeader),
    [NR_MODEL_ANIM] = K(0x18, struct ModelAnim),
    [NR_INSTDEF] = K(0x40, struct InstDef),
    [NR_MESH] = K(0x20, struct mesh_info),
    [NR_QUAD] = K(0x5c, struct QuadBlock),
    [NR_BSP] = K(0x20, struct BSP),
    [NR_VERTEX] = K(0x10, struct LevVertex),
    [NR_WATER] = K(8, struct WaterVert),
    [NR_SC] = K(0x10, struct SCVert),
    [NR_SKYBOX] = K(0x38, struct Skybox),
    [NR_NAV] = K(0x4c, struct NavHeader),
    [NR_ICONGROUP4] = K(48, struct IconGroup4),
    [NR_TEXTURE_LAYOUT] = K(12, struct TextureLayout),
    [NR_ICON] = K(32, struct Icon),
    [NR_SPAWN1] = K(4, struct SpawnType1),
    [NR_NAVFRAME] = K(20, struct NavFrame),
    [NR_FRAME] = K(0x1c, struct ModelFrame),
    [NR_SHORT_VERTEX] = K(12, struct ShortVertex),
    [NR_FACE] = K(8, struct SkyboxFace),
    [NR_OVERT] = K(4, struct OVert),
    [NR_POS] = K(6, SVec3),
    [NR_POSROT] = K(12, struct SpawnPosRot),
    [NR_PVS] = K(16, struct PVS),
    [NR_SPAWN2] = K(8, struct SpawnType2),
    [NR_CHECKPOINT] = K(12, struct CheckpointNode),
    [NR_ANIMTEX] = K(12, struct AnimTex),
    [NR_VISMEM] = K(0x90, struct VisMem),
    [NR_TEXLOOKUP] = K(16, struct LevTexLookup),
    [NR_MODELS] = K(4, struct Model *),
    [NR_INSTDEFS] = K(4, struct InstDef *),
    [NR_ANIMATIONS] = K(4, struct ModelAnim *),
    [NR_TEXTURES] = K(4, struct TextureLayout *),
    [NR_NAVS] = K(4, struct NavHeader *),
    [NR_MPK] = K(4, struct NativeModelPack),
    [NR_LEVEL] = K(500, struct Level),
    [NR_HITBOX] = K(32, struct BSP),
    [NR_SPAWN2_ROT] = K(8, struct SpawnType2),
    [NR_ICONGROUP] = K(20, struct IconGroup),
    [NR_ICONGROUPS] = K(4, struct IconGroup *),
    [NR_BSPLINK] = K(8, struct VisMemBspListNode),
};
#undef K
int NativeResident_Layout(enum NativeResidentKind kind, size_t *wire, size_t *host, size_t *alignment)
{
	if ((unsigned)kind >= NR_KIND_COUNT || !wire || !host || !alignment)
		return 0;
	*wire = Residentlayouts[kind].wire;
	*host = Residentlayouts[kind].host;
	*alignment = Residentlayouts[kind].alignment;
	return 1;
}
static int ResidentOverlap(const void *a, size_t na, const void *b, size_t nb)
{
	uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
	return na && nb && (x <= y ? y - x < na : x - y < nb);
}
static int ResidentRange(const struct NativeResidentContext *c, u32 offset, size_t bytes, void *out, size_t outBytes)
{
	if (!c || !c->map || !c->map->origin || c->map->originSize > UINT32_MAX || !out || (c->count && !c->bindings) || offset > c->map->originSize ||
	    bytes > c->map->originSize - offset || ResidentOverlap(out, outBytes, c->map->origin, c->map->originSize) ||
	    c->count > SIZE_MAX / sizeof(*c->bindings) || ResidentOverlap(out, outBytes, c->bindings, c->count * sizeof(*c->bindings)))
		return 0;
	return 1;
}
static enum NativeAssetResult ResidentResolveTarget(const struct NativeResidentContext *c, size_t target, enum NativeResidentKind kind, size_t count,
                                                    void **out)
{
	void *found = NULL;
	*out = NULL;
	if (kind >= NR_KIND_COUNT || target > c->map->originSize || count > (c->map->originSize - target) / Residentlayouts[kind].wire)
		return NATIVE_ASSET_INVALID_DATA;
	for (size_t i = 0; i < c->count; i++)
	{
		const struct NativeResidentBinding *b = &c->bindings[i];
		if (b->kind != kind || target < b->offset)
			continue;
		size_t delta = target - b->offset;
		if (delta % Residentlayouts[kind].wire)
			continue;
		size_t index = delta / Residentlayouts[kind].wire;
		if (index > b->count || count > b->count - index)
			continue;
		if (found || !b->resident || (uintptr_t)b->resident % Residentlayouts[kind].alignment || b->count > SIZE_MAX / Residentlayouts[kind].host ||
		    b->offset > c->map->originSize || b->count > (c->map->originSize - b->offset) / Residentlayouts[kind].wire ||
		    ResidentOverlap(b->resident, b->count * Residentlayouts[kind].host, c->map->origin, c->map->originSize))
			return NATIVE_ASSET_INVALID_DATA;
		found = (u8 *)b->resident + index * Residentlayouts[kind].host;
	}
	if (found)
	{
		*out = found;
		return NATIVE_ASSET_OK;
	}
	if (c->materialize)
		return c->materialize(c->owner, (u32)target, kind, count, out);
	if (kind == NR_BYTES || kind == NR_WORDS)
	{
		if ((uintptr_t)(c->map->origin + target) % Residentlayouts[kind].alignment)
			return NATIVE_ASSET_INVALID_DATA;
		*out = c->map->origin + target;
		return NATIVE_ASSET_OK;
	}
	return NATIVE_ASSET_NOT_FOUND;
}
enum NativeAssetResult NativeResident_Resolve(const struct NativeResidentContext *c, u32 slot, enum NativeResidentKind kind, size_t count, void **out)
{
	void *wire = NULL;
	*out = NULL;
	if (kind >= NR_KIND_COUNT || count > SIZE_MAX / Residentlayouts[kind].wire)
		return NATIVE_ASSET_INVALID_DATA;
	enum NativePtrMapResult r = NativePtrMap_Resolve(c->map, slot, count * Residentlayouts[kind].wire, &wire);
	if (r == NATIVE_PTRMAP_SLOT_NOT_FOUND && slot <= c->map->originSize && c->map->originSize - slot >= 4 && CTR_ReadU32LE(c->map->origin + slot) == 0)
		return NATIVE_ASSET_OK;
	if (r != NATIVE_PTRMAP_OK)
		return NATIVE_ASSET_INVALID_DATA;
	return ResidentResolveTarget(c, (size_t)((u8 *)wire - c->map->origin), kind, count, out);
}
static enum NativeAssetResult ResidentResolveTerrain(const struct NativeResidentContext *c, u32 slot, void **out)
{
	void *wire = NULL;
	*out = NULL;
	enum NativePtrMapResult r = NativePtrMap_Resolve(c->map, slot, 0, &wire);
	if (r == NATIVE_PTRMAP_SLOT_NOT_FOUND && CTR_ReadU32LE(c->map->origin + slot) == 0)
		return NATIVE_ASSET_OK;
	if (r != NATIVE_PTRMAP_OK)
		return NATIVE_ASSET_INVALID_DATA;
	size_t target = (size_t)((u8 *)wire - c->map->origin);
	int animated = (target & 1) != 0;
	enum NativeAssetResult result = ResidentResolveTarget(c, target - (size_t)animated, animated ? NR_ANIMTEX : NR_ICONGROUP4, 1, out);
	if (result == NATIVE_ASSET_OK && animated)
		*out = (void *)((uintptr_t)*out | 1u);
	return result;
}

struct ResidentScalar
{
	size_t wire, host, bytes, unit;
};
static void ResidentScalars(const u8 *wire, void *out, const struct ResidentScalar *s, size_t count)
{
	for (size_t i = 0; i < count; i++)
		for (size_t j = 0; j < s[i].bytes; j += s[i].unit)
		{
			const u8 *src = wire + s[i].wire + j;
			u8 *dst = (u8 *)out + s[i].host + j;
			if (s[i].unit == 1)
				*dst = *src;
			else if (s[i].unit == 2)
			{
				u16 value = CTR_ReadU16LE(src);
				memcpy(dst, &value, 2);
			}
			else
			{
				u32 value = CTR_ReadU32LE(src);
				memcpy(dst, &value, 4);
			}
		}
}
#define S(type, field, wire, unit) {wire, offsetof(struct type, field), sizeof(((struct type *)0)->field), unit}
#define REF(field, slot, kind, count)                                                                    \
	do                                                                                                   \
	{                                                                                                    \
		void *ptr;                                                                                       \
		enum NativeAssetResult result = NativeResident_Resolve(c, offset + (slot), kind, count, &ptr);   \
		if (result != NATIVE_ASSET_OK)                                                                   \
			return result;                                                                               \
		temp.field = ptr;                                                                                \
		if (c->pointerSlot)                                                                              \
			c->pointerSlot(c->owner, (u8 *)out + ((u8 *)&temp.field - (u8 *)&temp), sizeof(temp.field)); \
	} while (0)
#define TERRAIN(field, slot)                                                                             \
	do                                                                                                   \
	{                                                                                                    \
		void *ptr;                                                                                       \
		enum NativeAssetResult result = ResidentResolveTerrain(c, offset + (slot), &ptr);                \
		if (result != NATIVE_ASSET_OK)                                                                   \
			return result;                                                                               \
		temp.field = ptr;                                                                                \
		if (c->pointerSlot)                                                                              \
			c->pointerSlot(c->owner, (u8 *)out + ((u8 *)&temp.field - (u8 *)&temp), sizeof(temp.field)); \
	} while (0)
#define ADDR(field, slot, kind, count)                                                                   \
	do                                                                                                   \
	{                                                                                                    \
		void *ptr;                                                                                       \
		enum NativeAssetResult result = NativeResident_Resolve(c, offset + (slot), kind, count, &ptr);   \
		if (result != NATIVE_ASSET_OK)                                                                   \
			return result;                                                                               \
		temp.field = (CtrRuntimeAddress)(uintptr_t)ptr;                                                  \
		if (c->pointerSlot)                                                                              \
			c->pointerSlot(c->owner, (u8 *)out + ((u8 *)&temp.field - (u8 *)&temp), sizeof(temp.field)); \
	} while (0)

enum NativeAssetResult NativeResident_DecodeModel(const struct NativeResidentContext *c, u32 offset, struct Model *out)
{
	if (!ResidentRange(c, offset, 24, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct Model temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(Model, name, 0x0, 1),
	    S(Model, id, 0x10, 2),
	    S(Model, numHeaders, 0x12, 2),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	if (temp.numHeaders < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(headers, 0x14, NR_MODEL_HEADER, (u16)temp.numHeaders);
	if (temp.numHeaders && !temp.headers)
		return NATIVE_ASSET_INVALID_DATA;
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeModelHeader(const struct NativeResidentContext *c, u32 offset, struct ModelHeader *out)
{
	if (!ResidentRange(c, offset, 64, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct ModelHeader temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(ModelHeader, name, 0x0, 1),   S(ModelHeader, unk1, 0x10, 4),       S(ModelHeader, maxDistanceLOD, 0x14, 2), S(ModelHeader, flags, 0x16, 2),
	    S(ModelHeader, scale, 0x18, 2), S(ModelHeader, _pad_scale, 0x1e, 2), S(ModelHeader, numAnimations, 0x34, 4),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	ADDR(ptrCommandList, 0x20, NR_WORDS, 1);
	REF(ptrFrameData, 0x24, NR_FRAME, 1);
	REF(ptrTexLayout, 0x28, NR_TEXTURES, c->materialize ? 0 : 1);
	REF(ptrColors, 0x2c, NR_WORDS, 1);
	ADDR(unk3, 0x30, NR_WORDS, 1);
	REF(ptrAnimations, 0x38, NR_ANIMATIONS, temp.numAnimations);
	REF(animtex, 0x3c, NR_ANIMTEX, 1);
	if (temp.numAnimations && !temp.ptrAnimations)
		return NATIVE_ASSET_INVALID_DATA;
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeModelAnim(const struct NativeResidentContext *c, u32 offset, struct ModelAnim *out)
{
	if (!ResidentRange(c, offset, 24, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct ModelAnim temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(ModelAnim, name, 0x0, 1),
	    S(ModelAnim, numFrames, 0x10, 2),
	    S(ModelAnim, frameSize, 0x12, 2),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	if (temp.frameSize < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrDeltaArray, 0x14, NR_WORDS, 1);
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeInstDef(const struct NativeResidentContext *c, u32 offset, struct InstDef *out)
{
	if (!ResidentRange(c, offset, 64, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct InstDef temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(InstDef, name, 0x0, 1),   S(InstDef, scale, 0x14, 2), S(InstDef, _pad_scale, 0x1a, 2), S(InstDef, colorRGBA, 0x1c, 4), S(InstDef, flags, 0x20, 4),
	    S(InstDef, unk24, 0x24, 4), S(InstDef, unk28, 0x28, 4), S(InstDef, pos, 0x30, 2),        S(InstDef, rot, 0x36, 2),       S(InstDef, modelID, 0x3c, 4),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	REF(model, 0x10, NR_MODEL, 1);
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeMesh(const struct NativeResidentContext *c, u32 offset, struct mesh_info *out)
{
	if (!ResidentRange(c, offset, 32, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct mesh_info temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(mesh_info, numQuadBlock, 0x0, 4), S(mesh_info, numVertex, 0x4, 4),    S(mesh_info, unk1, 0x8, 4),
	    S(mesh_info, unk2, 0x14, 4),        S(mesh_info, numBspNodes, 0x1c, 4),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	if (temp.numQuadBlock < 0 || temp.numVertex < 0 || temp.numBspNodes < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrQuadBlockArray, 0xc, NR_QUAD, (u32)temp.numQuadBlock);
	REF(ptrVertexArray, 0x10, NR_VERTEX, (u32)temp.numVertex);
	REF(bspRoot, 0x18, NR_BSP, (u32)temp.numBspNodes);
	if (temp.numQuadBlock && !temp.ptrQuadBlockArray)
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numVertex && !temp.ptrVertexArray)
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numBspNodes && !temp.bspRoot)
		return NATIVE_ASSET_INVALID_DATA;
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeWater(const struct NativeResidentContext *c, u32 offset, struct WaterVert *out)
{
	if (!ResidentRange(c, offset, 8, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct WaterVert temp = {0};
	REF(v, 0x0, NR_VERTEX, 1);
	REF(w, 0x4, NR_OVERT, 1);
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeSC(const struct NativeResidentContext *c, u32 offset, struct SCVert *out)
{
	if (!ResidentRange(c, offset, 16, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct SCVert temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(SCVert, offset_pos_xy, 0x4, 4),
	    S(SCVert, offset_pos_zw, 0x8, 4),
	    S(SCVert, offset_color_rgba, 0xc, 4),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	REF(v, 0x0, NR_VERTEX, 1);
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodePVS(const struct NativeResidentContext *c, u32 offset, struct PVS *out)
{
	if (!ResidentRange(c, offset, 16, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct PVS temp = {0};
	REF(visLeafSrc, 0x0, NR_BYTES, 1);
	REF(visFaceSrc, 0x4, NR_BYTES, 1);
	REF(visInstSrc, 0x8, NR_INSTDEFS, 1);
	REF(visExtraSrc, 0xc, NR_BYTES, 1);
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeNavHeader(const struct NativeResidentContext *c, u32 offset, struct NavHeader *out)
{
	if (!ResidentRange(c, offset, 76, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct NavHeader temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(NavHeader, magicNumber, 0x0, 2), S(NavHeader, numPoints, 0x2, 2),  S(NavHeader, posY_firstNode, 0x4, 4),
	    S(NavHeader, rampPhys1, 0xc, 2),   S(NavHeader, rampPhys2, 0x2c, 2),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	if (temp.numPoints < 0)
		return NATIVE_ASSET_INVALID_DATA;
	// Retail nav 'last' is runtime scratch when absent from PTR, and can
	// contain a stale PS1 address. Only an explicit relocation is meaningful.
	void *wireLast = NULL;
	enum NativePtrMapResult lastResult = NativePtrMap_Resolve(c->map, offset + 8, 0, &wireLast);
	if (lastResult == NATIVE_PTRMAP_OK)
	{
		REF(last, 0x8, NR_NAVFRAME, 0);
	}
	else if (lastResult != NATIVE_PTRMAP_SLOT_NOT_FOUND)
		return NATIVE_ASSET_INVALID_DATA;
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeSkybox(const struct NativeResidentContext *c, u32 offset, struct Skybox *out)
{
	if (!ResidentRange(c, offset, 56, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct Skybox temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(Skybox, numVertex, 0x0, 4),
	    S(Skybox, numFaces, 0x8, 2),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	if (temp.numVertex < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrVertex, 0x4, NR_SHORT_VERTEX, (u32)temp.numVertex);
	if (temp.numFaces[0] < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrFaces[0], 0x18, NR_FACE, (u16)temp.numFaces[0]);
	if (temp.numFaces[1] < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrFaces[1], 0x1c, NR_FACE, (u16)temp.numFaces[1]);
	if (temp.numFaces[2] < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrFaces[2], 0x20, NR_FACE, (u16)temp.numFaces[2]);
	if (temp.numFaces[3] < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrFaces[3], 0x24, NR_FACE, (u16)temp.numFaces[3]);
	if (temp.numFaces[4] < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrFaces[4], 0x28, NR_FACE, (u16)temp.numFaces[4]);
	if (temp.numFaces[5] < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrFaces[5], 0x2c, NR_FACE, (u16)temp.numFaces[5]);
	if (temp.numFaces[6] < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrFaces[6], 0x30, NR_FACE, (u16)temp.numFaces[6]);
	if (temp.numFaces[7] < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptrFaces[7], 0x34, NR_FACE, (u16)temp.numFaces[7]);
	if (temp.numVertex && !temp.ptrVertex)
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numFaces[0] && !temp.ptrFaces[0])
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numFaces[1] && !temp.ptrFaces[1])
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numFaces[2] && !temp.ptrFaces[2])
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numFaces[3] && !temp.ptrFaces[3])
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numFaces[4] && !temp.ptrFaces[4])
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numFaces[5] && !temp.ptrFaces[5])
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numFaces[6] && !temp.ptrFaces[6])
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numFaces[7] && !temp.ptrFaces[7])
		return NATIVE_ASSET_INVALID_DATA;
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeSpawnPosRot(const struct NativeResidentContext *c, u32 offset, struct SpawnType2 *out)
{
	if (!ResidentRange(c, offset, 8, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct SpawnType2 temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(SpawnType2, numCoords, 0x0, 4),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	if (temp.numCoords < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(coords.posRot, 0x4, NR_POSROT, (u32)temp.numCoords);
	if (temp.numCoords && !temp.coords.positions)
		return NATIVE_ASSET_INVALID_DATA;
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeQuad(const struct NativeResidentContext *c, u32 offset, struct QuadBlock *out)
{
	if (!ResidentRange(c, offset, 92, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct QuadBlock temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(QuadBlock, index, 0x0, 2),
	    S(QuadBlock, quadFlags, 0x12, 2),
	    S(QuadBlock, draw_order_low, 0x14, 4),
	    S(QuadBlock, draw_order_high, 0x18, 4),
	    S(QuadBlock, bbox, 0x2c, 2),
	    S(QuadBlock, terrain_type, 0x38, 1),
	    S(QuadBlock, weather_intensity, 0x39, 1),
	    S(QuadBlock, weather_vanishRate, 0x3a, 1),
	    S(QuadBlock, mulNormVecY, 0x3b, 1),
	    S(QuadBlock, blockID, 0x3c, 2),
	    S(QuadBlock, checkpointIndex, 0x3e, 1),
	    S(QuadBlock, triNormalVecBitShift, 0x3f, 1),
	    S(QuadBlock, triNormalVecDividend, 0x48, 2),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	TERRAIN(ptr_texture_mid[0], 0x1c);
	TERRAIN(ptr_texture_mid[1], 0x20);
	TERRAIN(ptr_texture_mid[2], 0x24);
	TERRAIN(ptr_texture_mid[3], 0x28);
	TERRAIN(ptr_texture_low, 0x40);
	REF(pvs, 0x44, NR_PVS, 1);
	*out = temp;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeResident_DecodeLevel(const struct NativeResidentContext *c, u32 offset, struct Level *out)
{
	if (!ResidentRange(c, offset, 500, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct Level temp = {0};
	static const struct ResidentScalar fields[] = {
	    S(Level, numInstances, 0xc, 4),
	    S(Level, numModels, 0x14, 4),
	    S(Level, numWaterVertices, 0x34, 4),
	    S(Level, glowGradient[0].pointFrom, 0x48, 2),
	    S(Level, glowGradient[0].pointTo, 0x4a, 2),
	    S(Level, glowGradient[0].colorFrom, 0x4c, 4),
	    S(Level, glowGradient[0].colorTo, 0x50, 4),
	    S(Level, DriverSpawn, 0x6c, 2),
	    S(Level, clearColorRGBA, 0xd8, 4),
	    S(Level, configFlags, 0xdc, 4),
	    S(Level, unk_EC, 0xec, 1),
	    S(Level, rainBuffer.numParticles_curr, 0x104, 4),
	    S(Level, rainBuffer.numParticles_max, 0x108, 2),
	    S(Level, rainBuffer.vanishRate, 0x10a, 2),
	    S(Level, rainBuffer.unk_4, 0x10c, 1),
	    S(Level, rainBuffer.cameraPos, 0x11c, 2),
	    S(Level, rainBuffer.unk_22, 0x122, 2),
	    S(Level, rainBuffer.colorRGBA_top, 0x124, 4),
	    S(Level, rainBuffer.colorRGBA_bottom, 0x128, 4),
	    S(Level, rainBuffer.fillMode, 0x12c, 4),
	    S(Level, rainBuffer.offsetOT, 0x130, 4),
	    S(Level, numSpawnType2, 0x138, 4),
	    S(Level, numSpawnType2_PosRot, 0x140, 4),
	    S(Level, cnt_restart_points, 0x148, 4),
	    S(Level, unk_150, 0x150, 1),
	    S(Level, clearColor, 0x160, 1),
	    S(Level, unk_16C, 0x16c, 4),
	    S(Level, numSCVert, 0x174, 4),
	    S(Level, stars, 0x17c, 2),
	    S(Level, splitLines, 0x184, 2),
	    S(Level, jumpVerticalSpeedCap, 0x18c, 1),
	    S(Level, unk_18D, 0x18d, 1),
	    S(Level, unk_18E, 0x18e, 1),
	    S(Level, unk_18F, 0x18f, 1),
	    S(Level, footer, 0x194, 1),
	    S(Level, glowGradient[1].pointFrom, 0x54, 2),
	    S(Level, glowGradient[1].pointTo, 0x56, 2),
	    S(Level, glowGradient[1].colorFrom, 0x58, 4),
	    S(Level, glowGradient[1].colorTo, 0x5c, 4),
	    S(Level, glowGradient[2].pointFrom, 0x60, 2),
	    S(Level, glowGradient[2].pointTo, 0x62, 2),
	    S(Level, glowGradient[2].colorFrom, 0x64, 4),
	    S(Level, glowGradient[2].colorTo, 0x68, 4),
	};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	if (temp.numWaterVertices < 0 || temp.numSpawnType2 < 0 || temp.numSpawnType2_PosRot < 0 || temp.cnt_restart_points < 0 || temp.numSCVert < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(ptr_mesh_info, 0x0, NR_MESH, 1);
	REF(ptr_skybox, 0x4, NR_SKYBOX, 1);
	REF(ptr_anim_tex, 0x8, NR_ANIMTEX, 1);
	REF(ptrInstDefs, 0x10, NR_INSTDEF, temp.numInstances);
	REF(ptrModelsPtrArray, 0x18, NR_MODELS, temp.numModels);
	REF(unk3, 0x1c, NR_BYTES, 1);
	REF(unk4, 0x20, NR_BYTES, 1);
	REF(ptrInstDefPtrArray, 0x24, NR_INSTDEFS, 1);
	REF(visOVertSrc, 0x28, NR_BYTES, 1);
	REF(null1, 0x2c, NR_BYTES, 1);
	REF(null2, 0x30, NR_BYTES, 1);
	REF(ptr_water, 0x38, NR_WATER, (u32)temp.numWaterVertices);
	REF(levTexLookup, 0x3c, NR_TEXLOOKUP, 1);
	REF(ptr_named_tex_array, 0x40, NR_ICON, 1);
	REF(ptr_tex_waterEnvMap, 0x44, NR_TEXTURE_LAYOUT, 1);
	REF(unk_Lev_CC, 0xcc, NR_BYTES, 1);
	REF(unk_Lev_D0, 0xd0, NR_BYTES, 1);
	REF(ptrLowTexArray, 0xd4, NR_BYTES, 1);
	REF(build_start, 0xe0, NR_BYTES, 1);
	REF(build_end, 0xe4, NR_BYTES, 1);
	REF(build_type, 0xe8, NR_BYTES, 1);
	REF(ptrSpawnType1, 0x134, NR_SPAWN1, 1);
	REF(ptrSpawnType2, 0x13c, NR_SPAWN2, (u32)temp.numSpawnType2);
	REF(ptrSpawnType2_PosRot, 0x144, NR_SPAWN2_ROT, (u32)temp.numSpawnType2_PosRot);
	REF(ptr_restart_points, 0x14c, NR_CHECKPOINT, (u32)temp.cnt_restart_points);
	REF(visSCVertSrc, 0x170, NR_BYTES, 1);
	REF(ptrSCVert, 0x178, NR_SC, (u32)temp.numSCVert);
	REF(LevNavTable, 0x188, NR_NAVS, 1);
	REF(visMem, 0x190, NR_VISMEM, 1);
	if (temp.numInstances && !temp.ptrInstDefs)
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numModels && !temp.ptrModelsPtrArray)
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numWaterVertices && !temp.ptr_water)
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numSpawnType2 && !temp.ptrSpawnType2)
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numSpawnType2_PosRot && !temp.ptrSpawnType2_PosRot)
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.cnt_restart_points && !temp.ptr_restart_points)
		return NATIVE_ASSET_INVALID_DATA;
	if (temp.numSCVert && !temp.ptrSCVert)
		return NATIVE_ASSET_INVALID_DATA;
	*out = temp;
	return NATIVE_ASSET_OK;
}

// The two spawn arrays have the same wire header and different element types.
enum NativeAssetResult NativeResident_DecodeSpawnPositions(const struct NativeResidentContext *c, u32 offset, struct SpawnType2 *out)
{
	if (!ResidentRange(c, offset, 8, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct SpawnType2 temp = {0};
	temp.numCoords = (s32)CTR_ReadU32LE(c->map->origin + offset);
	if (temp.numCoords < 0)
		return NATIVE_ASSET_INVALID_DATA;
	REF(coords.positions, 4, NR_POS, (u32)temp.numCoords);
	if (temp.numCoords && !temp.coords.positions)
		return NATIVE_ASSET_INVALID_DATA;
	*out = temp;
	return NATIVE_ASSET_OK;
}
// Hitbox arrays are a separate interpretation: their flag bits are not a BSP
// branch/leaf discriminator. Caller chooses the decoder from its owning field.
enum NativeAssetResult NativeResident_DecodeBspHitbox(const struct NativeResidentContext *c, u32 offset, struct BSP *out)
{
	if (!ResidentRange(c, offset, 32, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct BSP temp = {0};
	static const struct ResidentScalar fields[] = {S(BSP, flag, 0, 2),
	                                               S(BSP, id, 2, 2),
	                                               S(BSP, box, 4, 2),
	                                               S(BSP, data.hitbox.center, 16, 2),
	                                               S(BSP, data.hitbox.radius, 22, 2),
	                                               S(BSP, data.hitbox.unk18, 24, 2),
	                                               S(BSP, data.hitbox.unk1A, 26, 2)};
	ResidentScalars(c->map->origin + offset, &temp, fields, sizeof(fields) / sizeof(*fields));
	REF(data.hitbox.instDef, 28, NR_INSTDEF, 1);
	*out = temp;
	return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeResident_DecodeBsp(const struct NativeResidentContext *c, u32 offset, struct BSP *out)
{
	if (!ResidentRange(c, offset, 32, out, sizeof(*out)))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct BSP temp = {0};
	static const struct ResidentScalar common[] = {S(BSP, flag, 0, 2), S(BSP, id, 2, 2), S(BSP, box, 4, 2)};
	ResidentScalars(c->map->origin + offset, &temp, common, sizeof(common) / sizeof(*common));
	if (!(temp.flag & BSP_NODE_FLAG_LEAF))
	{
		static const struct ResidentScalar fields[] = {S(BSP, data.branch.axis, 16, 2), S(BSP, data.branch.childID, 24, 2)};
		ResidentScalars(c->map->origin + offset, &temp, fields, 2);
	}
	else
	{
		temp.data.leaf.unk1 = (s32)CTR_ReadU32LE(c->map->origin + offset + 16);
		temp.data.leaf.numQuads = (s32)CTR_ReadU32LE(c->map->origin + offset + 24);
		if (temp.data.leaf.numQuads < 0)
			return NATIVE_ASSET_INVALID_DATA;
		REF(data.leaf.bspHitboxArray, 20, NR_HITBOX, c->materialize ? 0 : 1);
		REF(data.leaf.ptrQuadBlockArray, 28, NR_QUAD, (u32)temp.data.leaf.numQuads);
		if (temp.data.leaf.numQuads && !temp.data.leaf.ptrQuadBlockArray)
			return NATIVE_ASSET_INVALID_DATA;
	}
	*out = temp;
	return NATIVE_ASSET_OK;
}
