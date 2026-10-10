#include <platform/native_resident_graph.h>
#include <platform/native_model_vertices.h>
#include <platform/native_model_commands.h>
#include <stdlib.h>
#include <string.h>
// Bounded work queue: allocation precedes conversion, so shared references and
// graph cycles resolve without recursive calls or temporary wire publications.
enum
{
	RG_MAX_BYTES = 64 * 1024 * 1024,
	RG_MAX_OBJECTS = 65536
};
struct RGObject
{
	struct NativeResidentBinding binding;
	size_t bytes;
	int owned, decoded;
};
struct NativeResidentGraph
{
	struct NativePtrMapView map;
	struct NativePtrMapEntry *entries;
	struct NativeResidentContext context;
	struct RGObject *objects;
	struct NativeResidentBinding *bindings;
	size_t count, capacity, allocated;
	void *root;
	void **slots;
	size_t slotCount, slotCapacity;
	int slotFailure;
	enum NativeResidentKind rootKind;
	u32 rootOffset;
	struct NativeResidentGraphError error;
};
static void RGSlot(void *owner, void *slot, size_t width)
{
	struct NativeResidentGraph *g = owner;
	if (width != sizeof(void *))
	{
		g->slotFailure = 1;
		return;
	}
	for (size_t i = 0; i < g->slotCount; i++)
		if (g->slots[i] == slot)
			return;
	if (g->slotCount == RG_MAX_OBJECTS)
	{
		g->slotFailure = 1;
		return;
	}
	if (g->slotCount == g->slotCapacity)
	{
		size_t cap = g->slotCapacity ? g->slotCapacity * 2 : 64;
		void **slots = realloc(g->slots, cap * sizeof(*slots));
		if (!slots)
		{
			g->slotFailure = 1;
			return;
		}
		g->slots = slots;
		g->slotCapacity = cap;
	}
	g->slots[g->slotCount++] = slot;
}
static int RGRange(const struct NativeResidentGraph *g, u32 at, size_t bytes)
{
	return at <= g->map.originSize && bytes <= g->map.originSize - at;
}
static enum NativeAssetResult RGAdd(struct NativeResidentGraph *g, enum NativeResidentKind kind, u32 at, size_t count, void *resident, size_t bytes, int owned)
{
	if (g->count == RG_MAX_OBJECTS)
		return NATIVE_ASSET_OUTPUT_TOO_SMALL;
	if (g->count == g->capacity)
	{
		size_t cap = g->capacity ? g->capacity * 2 : 64;
		struct RGObject *objects = malloc(cap * sizeof(*objects));
		struct NativeResidentBinding *bindings = malloc(cap * sizeof(*bindings));
		if (!objects || !bindings)
		{
			free(objects);
			free(bindings);
			return NATIVE_ASSET_OUTPUT_TOO_SMALL;
		}
		if (g->count)
		{
			memcpy(objects, g->objects, g->count * sizeof(*objects));
			memcpy(bindings, g->bindings, g->count * sizeof(*bindings));
		}
		free(g->objects);
		free(g->bindings);
		g->objects = objects;
		g->bindings = bindings;
		g->capacity = cap;
	}
	struct NativeResidentBinding b = {kind, at, count, resident};
	g->objects[g->count] = (struct RGObject){b, bytes, owned, 0};
	g->bindings[g->count++] = b;
	g->context.bindings = g->bindings;
	g->context.count = g->count;
	return NATIVE_ASSET_OK;
}
static enum NativeAssetResult RGTableCount(struct NativeResidentGraph *g, u32 at, size_t *count)
{
	size_t n = 0;
	for (; n < RG_MAX_OBJECTS && RGRange(g, at, 4); n++, at += 4)
	{
		void *target = NULL;
		enum NativePtrMapResult r = NativePtrMap_Resolve(&g->map, at, 0, &target);
		if (r == NATIVE_PTRMAP_SLOT_NOT_FOUND && CTR_ReadU32LE(g->map.origin + at) == 0)
		{
			*count = n + 1;
			return NATIVE_ASSET_OK;
		}
		if (r != NATIVE_PTRMAP_OK)
			return NATIVE_ASSET_INVALID_DATA;
	}
	return NATIVE_ASSET_INVALID_DATA;
}
static enum NativeAssetResult RGAnimTex(struct NativeResidentGraph *, u32, int, void **);
static enum NativeAssetResult RGMaterializeInner(void *owner, u32 at, enum NativeResidentKind kind, size_t count, void **out)
{
	struct NativeResidentGraph *g = owner;
	size_t wire, host, align;
	if (!NativeResident_Layout(kind, &wire, &host, &align) || count > SIZE_MAX / wire || !RGRange(g, at, count * wire))
		return NATIVE_ASSET_INVALID_DATA;
	(void)align;
	// Pointer-free records and opaque streams retain their offsets in our own
	// mutable copy. Original asset bytes remain unchanged.
	switch (kind)
	{
	case NR_BYTES:
	case NR_WORDS:
	case NR_VERTEX:
	case NR_ICONGROUP4:
	case NR_TEXTURE_LAYOUT:
	case NR_ICON:
	case NR_NAVFRAME:
	case NR_FRAME:
	case NR_SHORT_VERTEX:
	case NR_FACE:
	case NR_OVERT:
	case NR_POS:
	case NR_POSROT:
	case NR_CHECKPOINT:
		if (wire != host)
			return NATIVE_ASSET_INVALID_DATA;
		if ((uintptr_t)(g->map.origin + at) % align)
			return NATIVE_ASSET_INVALID_DATA;
		*out = g->map.origin + at;
		return NATIVE_ASSET_OK;
	default:
		break;
	}
	// Headers with trailing inline data need one allocation per record.
	size_t extra = 0;
	if (kind == NR_MPK)
	{
		if (at || count != 1)
			return NATIVE_ASSET_INVALID_DATA;
		struct NativeMpkView mpk;
		enum NativeAssetResult r = NativeMpk_Open(&g->map, &mpk);
		if (r != NATIVE_ASSET_OK)
			return r;
		extra = ((size_t)mpk.modelCount + 1) * sizeof(void *);
	}
	else if (kind == NR_NAV)
	{
		if (count != 1 || !RGRange(g, at, 76))
			return NATIVE_ASSET_INVALID_DATA;
		s16 n = (s16)CTR_ReadU16LE(g->map.origin + at + 2);
		if (n < 0 || !RGRange(g, at + 76, (size_t)n * sizeof(struct NavFrame)))
			return NATIVE_ASSET_INVALID_DATA;
		extra = (size_t)n * sizeof(struct NavFrame);
	}
	else if (kind == NR_MODEL_ANIM)
	{
		if (count != 1 || !RGRange(g, at, 24))
			return NATIVE_ASSET_INVALID_DATA;
		u16 n = CTR_ReadU16LE(g->map.origin + at + 16), stride = CTR_ReadU16LE(g->map.origin + at + 18);
		size_t stored = (n & 0x8000) ? (n & 0x7fff) / 2u + 1 : n;
		if (!stored || stride < 28 || !RGRange(g, at + 24, stored * stride))
			return NATIVE_ASSET_INVALID_DATA;
		extra = stored * stride;
		for (size_t i = 0; i < stored; i++)
		{
			u32 v = CTR_ReadU32LE(g->map.origin + at + 24 + i * stride + 24);
			if (v < 28 || v > stride)
				return NATIVE_ASSET_INVALID_DATA;
		}
	}
	else if (kind == NR_SPAWN1 || kind == NR_ICONGROUP)
	{
		if (count != 1 || !RGRange(g, at, wire))
			return NATIVE_ASSET_INVALID_DATA;
		s32 n = kind == NR_SPAWN1 ? (s32)CTR_ReadU32LE(g->map.origin + at) : (s16)CTR_ReadU16LE(g->map.origin + at + 18);
		if (n < 0 || !RGRange(g, at + (u32)wire, (size_t)n * 4))
			return NATIVE_ASSET_INVALID_DATA;
		extra = (size_t)n * sizeof(void *);
	}
	else if (kind == NR_NAVS)
	{
		count = 3;
		if (!RGRange(g, at, count * 4))
			return NATIVE_ASSET_INVALID_DATA;
	}
	else if (kind == NR_INSTDEFS)
	{
		enum NativeAssetResult r = RGTableCount(g, at, &count);
		if (r != NATIVE_ASSET_OK)
			return r;
	}
	else if (kind == NR_HITBOX)
	{
		count = 0;
		u32 cursor = at;
		do
		{
			if (!RGRange(g, cursor, 4) || count == RG_MAX_OBJECTS)
				return NATIVE_ASSET_INVALID_DATA;
			if (!CTR_ReadU16LE(g->map.origin + cursor))
			{
				extra = host;
				break;
			}
			if (!RGRange(g, cursor, 32))
				return NATIVE_ASSET_INVALID_DATA;
			count++;
			cursor += 32;
		} while (1);
	}
	else if (kind == NR_ANIMTEX)
	{
		// The variable-length list is allocated by a dedicated routine below.
		return RGAnimTex(g, at, 0, out);
	}
	if (count > SIZE_MAX / host || extra > SIZE_MAX - count * host || count * host + extra > RG_MAX_BYTES - g->allocated)
		return NATIVE_ASSET_OUTPUT_TOO_SMALL;
	// Reject incompatible overlap rather than leaving earlier references stale.
	for (size_t i = 0; i < g->count; i++)
	{
		struct NativeResidentBinding *b = &g->bindings[i];
		size_t bw, bh, ba;
		NativeResident_Layout(b->kind, &bw, &bh, &ba);
		if (b->kind == kind && at < (size_t)b->offset + b->count * bw && b->offset < (size_t)at + count * wire)
			return NATIVE_ASSET_INVALID_DATA;
	}
	size_t bytes = count * host + extra;
	if (!bytes)
		bytes = host;
	void *p = calloc(1, bytes);
	if (!p)
		return NATIVE_ASSET_OUTPUT_TOO_SMALL;
	enum NativeAssetResult r = RGAdd(g, kind, at, count, p, bytes, 1);
	if (r != NATIVE_ASSET_OK)
	{
		free(p);
		return r;
	}
	g->allocated += bytes;
	if (kind == NR_NAV && extra)
	{
		memcpy((u8 *)p + host, g->map.origin + at + wire, extra);
		r = RGAdd(g, NR_NAVFRAME, at + (u32)wire, extra / sizeof(struct NavFrame), (u8 *)p + host, extra, 0);
		if (r != NATIVE_ASSET_OK)
			return r;
		g->objects[g->count - 1].decoded = 1;
	}
	if (kind == NR_MODEL_ANIM && extra)
		memcpy((u8 *)p + host, g->map.origin + at + wire, extra);
	*out = p;
	return NATIVE_ASSET_OK;
}
static enum NativeAssetResult RGMaterialize(void *owner, u32 at, enum NativeResidentKind kind, size_t count, void **out)
{
	struct NativeResidentGraph *g = owner;
	enum NativeAssetResult r = RGMaterializeInner(owner, at, kind, count, out);
	if (r != NATIVE_ASSET_OK)
		g->error = (struct NativeResidentGraphError){kind, at, count, r};
	return r;
}
static enum NativeAssetResult RGRef(struct NativeResidentGraph *g, u32 slot, enum NativeResidentKind kind, size_t n, void **out)
{
	return NativeResident_Resolve(&g->context, slot, kind, n, out);
}
#define RG_TRY(expr)                              \
	do                                            \
	{                                             \
		enum NativeAssetResult rgResult = (expr); \
		if (rgResult != NATIVE_ASSET_OK)          \
			return rgResult;                      \
	} while (0)
static enum NativeAssetResult RGAnimTex(struct NativeResidentGraph *g, u32 first, int model, void **out)
{
	size_t nodes = 0, bytes = sizeof(void *);
	u32 at = first;
	for (;;)
	{
		void *marker = NULL;
		if (!RGRange(g, at, 4))
			return NATIVE_ASSET_INVALID_DATA;
		enum NativePtrMapResult r = NativePtrMap_Resolve(&g->map, at, 0, &marker);
		if (r == NATIVE_PTRMAP_OK && marker == g->map.origin + first)
			break;
		if (!RGRange(g, at, 12) || nodes == RG_MAX_OBJECTS)
			return NATIVE_ASSET_INVALID_DATA;
		s16 n = (s16)CTR_ReadU16LE(g->map.origin + at + 4), skip = (s16)CTR_ReadU16LE(g->map.origin + at + 8);
		if (n <= 0 || skip < 0 || skip > 31 || !RGRange(g, at + 12, (size_t)n * 4))
			return NATIVE_ASSET_INVALID_DATA;
		bytes += sizeof(struct AnimTex) + (size_t)n * sizeof(void *);
		nodes++;
		at += 12 + (u32)n * 4;
	}
	if (!nodes || bytes > RG_MAX_BYTES - g->allocated)
		return NATIVE_ASSET_INVALID_DATA;
	u8 *list = calloc(1, bytes);
	if (!list)
		return NATIVE_ASSET_OUTPUT_TOO_SMALL;
	at = first;
	u8 *cursor = list;
	for (size_t i = 0; i < nodes; i++)
	{
		struct AnimTex *anim = (void *)cursor;
		anim->numFrames = (s16)CTR_ReadU16LE(g->map.origin + at + 4);
		anim->frameOffset = (s16)CTR_ReadU16LE(g->map.origin + at + 6);
		anim->frameSkip = (s16)CTR_ReadU16LE(g->map.origin + at + 8);
		anim->frameCurr = (s16)CTR_ReadU16LE(g->map.origin + at + 10);
		if (anim->frameCurr < 0 || anim->frameCurr >= anim->numFrames)
		{
			if (!i)
				free(list);
			return NATIVE_ASSET_INVALID_DATA;
		}
		enum NativeAssetResult result = RGAdd(g, NR_ANIMTEX, at, 1, anim, i ? 0 : bytes, i == 0);
		if (result != NATIVE_ASSET_OK)
		{
			if (!i)
				free(list);
			return result;
		}
		g->objects[g->count - 1].decoded = 1;
		for (s16 j = 0; j < anim->numFrames; j++)
		{
			RG_TRY(RGRef(g, at + 12 + (u32)j * 4, model ? NR_TEXTURE_LAYOUT : NR_ICONGROUP4, 1, (void **)&ANIMTEX_GETARRAY(anim)[j]));
			RGSlot(g, &ANIMTEX_GETARRAY(anim)[j], sizeof(void *));
		}
		RGSlot(g, &anim->ptrActiveTex, sizeof(void *));
		RG_TRY(RGRef(g, at, model ? NR_TEXTURES : NR_ICONGROUP4, 1, &anim->ptrActiveTex));
		if (!anim->ptrActiveTex && !model)
			anim->ptrActiveTex = ANIMTEX_GETARRAY(anim)[anim->frameCurr];
		if (!anim->ptrActiveTex)
			return NATIVE_ASSET_INVALID_DATA;
		cursor += sizeof(*anim) + (size_t)anim->numFrames * sizeof(void *);
		at += 12 + (u32)anim->numFrames * 4;
	}
	memcpy(cursor, &list, sizeof(list));
	RGSlot(g, cursor, sizeof(void *));
	g->allocated += bytes;
	*out = list;
	return NATIVE_ASSET_OK;
}
static enum NativeAssetResult RGValidateModel(struct NativeResidentGraph *g, u32 at)
{
	struct NativeModelView model;
	RG_TRY(NativeModel_Open(&g->map, at, &model));
	for (u32 h = 0; h < model.headerCount; h++)
	{
		struct NativeModelHeaderView header;
		RG_TRY(NativeModel_GetHeader(&model, h, &header));
		u32 offset = (u32)(header.wire - g->map.origin), vertices = 0;
		enum NativeAssetResult r = NativeModel_GetVertexCount(&model, h, &vertices);
		if (r == NATIVE_ASSET_NOT_FOUND)
			continue;
		if (r != NATIVE_ASSET_OK || vertices > RG_MAX_OBJECTS)
			return NATIVE_ASSET_INVALID_DATA;
		struct NativeModelCommands commands;
		RG_TRY(NativeModelCommands_Open(&model, h, &commands));
		struct NativeModelTriangle triangle;
		size_t textures = 0;
		while ((r = NativeModelCommands_Next(&commands, &triangle)) == NATIVE_ASSET_OK)
		{
			size_t index = triangle.command & 0x1ffu;
			if (index > textures)
				textures = index;
		}
		if (r != NATIVE_ASSET_NOT_FOUND)
			return r;
		if (textures)
		{
			void *table;
			RG_TRY(RGRef(g, offset + 40, NR_TEXTURES, textures, &table));
			if (!table)
				return NATIVE_ASSET_INVALID_DATA;
		}
		struct NativeModelVertex *decoded = malloc((vertices ? vertices : 1) * sizeof(*decoded));
		if (!decoded)
			return NATIVE_ASSET_OUTPUT_TOO_SMALL;
		u32 actual = 0;
		if (header.animationCount == 0)
			r = NativeModel_DecodeStaticVertices(&model, h, decoded, vertices, &actual);
		else
		{
			for (u32 a = 0; a < header.animationCount; a++)
			{
				struct NativeAnimationView animation;
				r = NativeModel_GetAnimation(&model, h, a, &animation);
				if (r != NATIVE_ASSET_OK)
					break;
				for (u32 f = 0; f < animation.storedFrameCount; f++)
				{
					r = NativeModel_DecodeAnimationVertices(&model, h, a, f, decoded, vertices, &actual);
					if (r != NATIVE_ASSET_OK)
						break;
				}
				if (r != NATIVE_ASSET_OK)
					break;
			}
		}
		free(decoded);
		if (r != NATIVE_ASSET_OK)
			return r;
		void *wireAnim = NULL;
		enum NativePtrMapResult ptr = NativePtrMap_Resolve(&g->map, offset + 60, 12, &wireAnim);
		if (ptr == NATIVE_PTRMAP_OK)
		{
			// Seed model chains before the generic record resolver sees them.
			size_t existing = g->count;
			void *resident = NULL;
			for (size_t i = 0; i < existing; i++)
				if (g->bindings[i].kind == NR_ANIMTEX && g->bindings[i].offset == (u32)((u8 *)wireAnim - g->map.origin))
					resident = g->bindings[i].resident;
			if (!resident)
				RG_TRY(RGAnimTex(g, (u32)((u8 *)wireAnim - g->map.origin), 1, &resident));
		}
		else if (ptr != NATIVE_PTRMAP_SLOT_NOT_FOUND || CTR_ReadU32LE(g->map.origin + offset + 60) != 0)
			return NATIVE_ASSET_INVALID_DATA;
	}
	return NATIVE_ASSET_OK;
}
static enum NativeAssetResult RGDecode(struct NativeResidentGraph *g, struct RGObject object)
{
	u32 at = object.binding.offset;
	size_t n = object.binding.count;
	void *p = object.binding.resident;
	enum NativeResidentKind kind = object.binding.kind;
	size_t wire, host, align;
	NativeResident_Layout(kind, &wire, &host, &align);
	for (size_t i = 0; i < n; i++, at += (u32)wire, p = (u8 *)p + host)
	{
		switch (kind)
		{
#define RG_CASE(k, fn)                                         \
	case k:                                                    \
		RG_TRY(NativeResident_Decode##fn(&g->context, at, p)); \
		break
		case NR_MPK:
		{
			struct NativeMpkView mpk;
			RG_TRY(NativeMpk_Open(&g->map, &mpk));
			struct NativeModelPack *pack = p;
			RG_TRY(RGRef(g, 0, NR_TEXLOOKUP, 1, (void **)&pack->icons));
			RGSlot(g, &pack->icons, sizeof(void *));
			for (u32 j = 0; j <= mpk.modelCount; j++)
			{
				RG_TRY(RGRef(g, 4 + j * 4, NR_MODEL, 1, (void **)&pack->models[j]));
				RGSlot(g, &pack->models[j], sizeof(void *));
			}
			break;
		}
			RG_CASE(NR_LEVEL, Level);
		case NR_MODEL:
			RG_TRY(RGValidateModel(g, at));
			RG_TRY(NativeResident_DecodeModel(&g->context, at, p));
			break;
			RG_CASE(NR_MODEL_HEADER, ModelHeader);
		case NR_INSTDEF:
			RG_TRY(NativeResident_DecodeInstDef(&g->context, at, p));
			RGSlot(g, &((struct InstDef *)p)->ptrInstance, sizeof(void *));
#ifdef CTR_NATIVE
			RGSlot(g, &((struct InstDef *)p)->residentPeer, sizeof(void *));
#endif
			break;
			RG_CASE(NR_MESH, Mesh);
			RG_CASE(NR_QUAD, Quad);
			RG_CASE(NR_BSP, Bsp);
			RG_CASE(NR_HITBOX, BspHitbox);
			RG_CASE(NR_WATER, Water);
			RG_CASE(NR_SC, SC);
			RG_CASE(NR_PVS, PVS);
			RG_CASE(NR_SKYBOX, Skybox);
		case NR_NAV:
			RG_TRY(NativeResident_DecodeNavHeader(&g->context, at, p));
			RGSlot(g, &((struct NavHeader *)p)->last, sizeof(void *));
			((struct NavHeader *)p)->last = NAVHEADER_GETFRAME((struct NavHeader *)p) + ((struct NavHeader *)p)->numPoints;
			break;
			RG_CASE(NR_MODEL_ANIM, ModelAnim);
			RG_CASE(NR_SPAWN2, SpawnPositions);
			RG_CASE(NR_SPAWN2_ROT, SpawnPosRot);
#undef RG_CASE
		case NR_MODELS:
		case NR_INSTDEFS:
		case NR_ANIMATIONS:
		case NR_TEXTURES:
		case NR_NAVS:
		case NR_ICONGROUPS:
		{
			enum NativeResidentKind target = kind == NR_MODELS       ? NR_MODEL
			                                 : kind == NR_INSTDEFS   ? NR_INSTDEF
			                                 : kind == NR_ANIMATIONS ? NR_MODEL_ANIM
			                                 : kind == NR_TEXTURES   ? NR_TEXTURE_LAYOUT
			                                 : kind == NR_NAVS       ? NR_NAV
			                                                         : NR_ICONGROUP;
			void *value;
			RG_TRY(RGRef(g, at, target, 1, &value));
			memcpy(p, &value, sizeof(value));
			RGSlot(g, p, sizeof(value));
			break;
		}
		case NR_SPAWN1:
		{
			struct SpawnType1 *spawn = p;
			spawn->count = (s32)CTR_ReadU32LE(g->map.origin + at);
			for (s32 j = 0; j < spawn->count; j++)
			{
				RG_TRY(RGRef(g, at + 4 + (u32)j * 4, NR_BYTES, 1, &spawn->pointers[j]));
				RGSlot(g, &spawn->pointers[j], sizeof(void *));
			}
			break;
		}
		case NR_ICONGROUP:
		{
			struct IconGroup *icons = p;
			memcpy(icons->name, g->map.origin + at, 16);
			icons->groupID = (s16)CTR_ReadU16LE(g->map.origin + at + 16);
			icons->numIcons = (s16)CTR_ReadU16LE(g->map.origin + at + 18);
			for (s16 j = 0; j < icons->numIcons; j++)
			{
				RG_TRY(RGRef(g, at + 20 + (u32)j * 4, NR_ICON, 1, (void **)&icons->icons[j]));
				RGSlot(g, &icons->icons[j], sizeof(void *));
			}
			break;
		}
		case NR_TEXLOOKUP:
		{
			struct LevTexLookup *lookup = p;
			lookup->numIcon = (s32)CTR_ReadU32LE(g->map.origin + at);
			lookup->numIconGroup = (s32)CTR_ReadU32LE(g->map.origin + at + 8);
			if (lookup->numIcon < 0 || lookup->numIconGroup < 0)
				return NATIVE_ASSET_INVALID_DATA;
			RG_TRY(RGRef(g, at + 4, NR_ICON, (size_t)lookup->numIcon, (void **)&lookup->firstIcon));
			RGSlot(g, &lookup->firstIcon, sizeof(void *));
			RG_TRY(RGRef(g, at + 12, NR_ICONGROUPS, (size_t)lookup->numIconGroup, (void **)&lookup->firstIconGroupPtr));
			RGSlot(g, &lookup->firstIconGroupPtr, sizeof(void *));
			break;
		}
		case NR_VISMEM:
		{
			struct VisMem *vis = p;
			// Pointer arrays are all explicitly declared in the resident ABI.
			for (unsigned j = 0; j < 32; j++)
			{
				void *value;
				RG_TRY(RGRef(g, at + j * 4, j < 16 ? NR_WORDS : NR_BYTES, 1, &value));
				void *slot = (u8 *)vis + (j / 4) * sizeof(vis->visLeafList) + (j % 4) * sizeof(void *);
				memcpy(slot, &value, sizeof(value));
				RGSlot(g, slot, sizeof(void *));
			}
			for (unsigned j = 0; j < 4; j++)
			{
				RG_TRY(RGRef(g, at + 128 + j * 4, NR_BSPLINK, 1, (void **)&vis->bspList[j]));
				RGSlot(g, &vis->bspList[j], sizeof(void *));
			}
			break;
		}
		case NR_BSPLINK:
		{
			struct VisMemBspListNode *link = p;
			RG_TRY(RGRef(g, at, NR_BSPLINK, 1, (void **)&link->next));
			RGSlot(g, &link->next, sizeof(void *));
			RG_TRY(RGRef(g, at + 4, NR_BSP, 1, (void **)&link->bsp));
			RGSlot(g, &link->bsp, sizeof(void *));
			break;
		}
		default:
			return NATIVE_ASSET_NOT_FOUND;
		}
	}
	return NATIVE_ASSET_OK;
}
void NativeResidentGraph_Free(struct NativeResidentGraph *g)
{
	if (!g)
		return;
	for (size_t i = 0; i < g->count; i++)
		if (g->objects[i].owned)
			free(g->objects[i].binding.resident);
	free(g->slots);
	free(g->objects);
	free(g->bindings);
	free(g->entries);
	free(g->map.origin);
	free(g);
}
enum NativeAssetResult NativeResidentGraph_Build(const struct NativePtrMapView *map, enum NativeResidentKind kind, u32 at, struct NativeResidentGraph **out,
                                                 struct NativeResidentGraphError *error)
{
	if ((kind != NR_LEVEL && kind != NR_MPK && kind != NR_MODEL) || !map || !map->origin || !out || map->originSize > RG_MAX_BYTES ||
	    map->count > RG_MAX_OBJECTS || (map->count && !map->entries))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct NativeResidentGraph *g = calloc(1, sizeof(*g));
	if (!g)
		return NATIVE_ASSET_OUTPUT_TOO_SMALL;
	g->map = *map;
	g->map.origin = malloc(map->originSize);
	g->entries = map->count ? malloc(map->count * sizeof(*g->entries)) : NULL;
	if (!g->map.origin || (map->count && !g->entries))
	{
		NativeResidentGraph_Free(g);
		return NATIVE_ASSET_OUTPUT_TOO_SMALL;
	}
	memcpy(g->map.origin, map->origin, map->originSize);
	if (map->count)
		memcpy(g->entries, map->entries, map->count * sizeof(*g->entries));
	g->map.entries = g->entries;
	g->context = (struct NativeResidentContext){.map = &g->map, .materialize = RGMaterialize, .owner = g, .pointerSlot = RGSlot};
	g->rootKind = kind;
	g->rootOffset = at;
	enum NativeAssetResult result = RGMaterialize(g, at, kind, 1, &g->root);
	for (size_t i = 0; result == NATIVE_ASSET_OK && i < g->count; i++)
	{
		struct RGObject object = g->objects[i];
		if (object.decoded)
			continue;
		result = RGDecode(g, object);
		if (result != NATIVE_ASSET_OK && !g->error.result)
			g->error = (struct NativeResidentGraphError){object.binding.kind, object.binding.offset, object.binding.count, result};
	}
	if (result == NATIVE_ASSET_OK && g->slotFailure)
		result = NATIVE_ASSET_OUTPUT_TOO_SMALL;
	if (result != NATIVE_ASSET_OK)
	{
		if (error)
			*error = g->error;
		NativeResidentGraph_Free(g);
		return result;
	}
	*out = g;
	if (error)
		memset(error, 0, sizeof(*error));
	return NATIVE_ASSET_OK;
}
void *NativeResidentGraph_Root(const struct NativeResidentGraph *g)
{
	return g ? g->root : NULL;
}
size_t NativeResidentGraph_ObjectCount(const struct NativeResidentGraph *g)
{
	return g ? g->count : 0;
}
const struct NativePtrMapView *NativeResidentGraph_Map(const struct NativeResidentGraph *g)
{
	return g ? &g->map : NULL;
}
int NativeResidentGraph_WireOffset(const struct NativeResidentGraph *g, const void *ptr, enum NativeResidentKind kind, u32 *out)
{
	if (!g || !ptr || !out)
		return 0;
	uintptr_t address = (uintptr_t)ptr;
	for (size_t i = 0; i < g->count; i++)
	{
		struct NativeResidentBinding b = g->objects[i].binding;
		size_t wire, host, align;
		NativeResident_Layout(b.kind, &wire, &host, &align);
		if (b.kind == kind && address >= (uintptr_t)b.resident && address - (uintptr_t)b.resident < b.count * host)
		{
			size_t delta = address - (uintptr_t)b.resident;
			if (delta % host)
				return 0;
			*out = b.offset + (u32)(delta / host * wire);
			return 1;
		}
	}
	return 0;
}
// Host ABI checkpoint. Every pointer slot is declared during materialization;
// serialization encodes owner/offset pairs, never searches scalar payloads for
// pointer-shaped values. Restoring builds and validates a pending graph first.
enum
{
	RGCP_HEADER = 48,
	RGCP_OBJECT = 24,
	RGCP_SLOT = 24,
	RGCP_NULL = UINT32_MAX,
	RGCP_EXTERNAL = UINT32_MAX - 1
};
static u32 RGAbi(void)
{
	u32 hash = 2166136261u;
	for (unsigned i = 0; i < NR_KIND_COUNT; i++)
	{
		size_t w, h, a;
		NativeResident_Layout((enum NativeResidentKind)i, &w, &h, &a);
		hash = (hash ^ (u32)w) * 16777619u;
		hash = (hash ^ (u32)h) * 16777619u;
		hash = (hash ^ (u32)a) * 16777619u;
	}
	return hash;
}
static void RGWrite64(u8 *out, u64 n)
{
	CTR_WriteU32LE(out, (u32)n);
	CTR_WriteU32LE(out + 4, (u32)(n >> 32));
}
static u64 RGRead64(const u8 *p)
{
	return (u64)CTR_ReadU32LE(p) | ((u64)CTR_ReadU32LE(p + 4) << 32);
}
static int RGOwner(const struct NativeResidentGraph *g, const void *p, size_t bytes, u32 *owner, u32 *offset)
{
	uintptr_t address = (uintptr_t)p;
	// Prefer an interior address over the end of an adjacent allocation.
	// The second pass permits genuine one-past sentinels.
	for (unsigned pass = 0; pass < (bytes ? 1u : 2u); pass++)
		for (size_t i = 0; i <= g->count; i++)
		{
			struct RGObject object = {0};
			if (i)
				object = g->objects[i - 1];
			if (i && !object.owned)
				continue;
			uintptr_t base = i ? (uintptr_t)object.binding.resident : (uintptr_t)g->map.origin;
			size_t size = i ? object.bytes : g->map.originSize;
			if (address >= base && address - base <= size && bytes <= size - (address - base) && (pass || address - base < size))
			{
				*owner = (u32)i;
				*offset = (u32)(address - base);
				return 1;
			}
		}
	return 0;
}
static void *RGAddress(struct NativeResidentGraph *g, u32 owner, u32 offset, size_t bytes)
{
	if (owner == 0)
		return offset <= g->map.originSize && bytes <= g->map.originSize - offset ? g->map.origin + offset : NULL;
	if (owner > g->count)
		return NULL;
	struct RGObject object = g->objects[owner - 1];
	return object.owned && offset <= object.bytes && bytes <= object.bytes - offset ? (u8 *)object.binding.resident + offset : NULL;
}
size_t NativeResidentGraph_CheckpointSize(const struct NativeResidentGraph *g)
{
	if (!g || g->slotFailure)
		return 0;
	size_t bytes = RGCP_HEADER + g->map.originSize + 4 + g->map.count * 4 + g->count * RGCP_OBJECT + g->slotCount * RGCP_SLOT;
	for (size_t i = 0; i < g->count; i++)
		if (g->objects[i].owned)
			bytes += g->objects[i].bytes;
	return bytes <= UINT32_MAX ? bytes : 0;
}
int NativeResidentGraph_CaptureCheckpoint(const struct NativeResidentGraph *g, void *buffer, size_t capacity)
{
	size_t size = NativeResidentGraph_CheckpointSize(g);
	if (!size || !buffer || capacity < size)
		return 0;
	// Reject overlap with every graph-owned buffer before writing.
	uintptr_t b = (uintptr_t)buffer;
	if (b > UINTPTR_MAX - size)
		return 0;
	for (size_t i = 0; i <= g->count; i++)
	{
		uintptr_t p = i ? (uintptr_t)g->objects[i - 1].binding.resident : (uintptr_t)g->map.origin;
		size_t n = i ? (g->objects[i - 1].owned ? g->objects[i - 1].bytes : 0) : g->map.originSize;
		if (n && b < p + n && p < b + size)
			return 0;
	}
	u8 *out = buffer;
	memset(out, 0, RGCP_HEADER);
	memcpy(out, "RGCP", 4);
	CTR_WriteU32LE(out + 4, 1);
	CTR_WriteU32LE(out + 8, (u32)size);
	CTR_WriteU32LE(out + 12, RGAbi());
	CTR_WriteU32LE(out + 16, sizeof(void *));
	CTR_WriteU32LE(out + 20, (u32)g->map.originSize);
	CTR_WriteU32LE(out + 24, (u32)g->count);
	CTR_WriteU32LE(out + 28, (u32)g->slotCount);
	CTR_WriteU32LE(out + 32, g->rootKind);
	CTR_WriteU32LE(out + 36, g->rootOffset);
	RGWrite64(out + 40, (u64)(uintptr_t)g->map.origin);
	out += RGCP_HEADER;
	memcpy(out, g->map.origin, g->map.originSize);
	out += g->map.originSize;
	CTR_WriteU32LE(out, (u32)g->map.count * 4);
	out += 4;
	for (size_t i = 0; i < g->map.count; i++, out += 4)
		CTR_WriteU32LE(out, g->map.entries[i].slotOffset);
	for (size_t i = 0; i < g->count; i++)
	{
		struct RGObject o = g->objects[i];
		u32 bytes = o.owned ? (u32)o.bytes : 0;
		CTR_WriteU32LE(out, o.binding.kind);
		CTR_WriteU32LE(out + 4, o.binding.offset);
		CTR_WriteU32LE(out + 8, (u32)o.binding.count);
		CTR_WriteU32LE(out + 12, bytes);
		RGWrite64(out + 16, o.owned ? (u64)(uintptr_t)o.binding.resident : 0);
		out += RGCP_OBJECT;
		if (bytes)
		{
			memcpy(out, o.binding.resident, bytes);
			out += bytes;
		}
	}
	for (size_t i = 0; i < g->slotCount; i++, out += RGCP_SLOT)
	{
		u32 owner, offset, target = RGCP_NULL, targetOffset = 0;
		void *value = NULL;
		u64 external = 0;
		if (!RGOwner(g, g->slots[i], sizeof(void *), &owner, &offset))
			return 0;
		memcpy(&value, g->slots[i], sizeof(value));
		int navEnd = 0;
		for (size_t j = 0; j < g->count; j++)
			if (g->objects[j].binding.kind == NR_NAV && g->slots[i] == (void *)&((struct NavHeader *)g->objects[j].binding.resident)->last)
			{
				struct RGObject nav = g->objects[j];
				if (value != (u8 *)nav.binding.resident + nav.bytes)
					return 0;
				target = (u32)j + 1;
				targetOffset = (u32)nav.bytes;
				navEnd = 1;
				break;
			}
		if (value && !navEnd && !RGOwner(g, value, 1, &target, &targetOffset))
		{
			target = RGCP_EXTERNAL;
			external = (u64)(uintptr_t)value;
		}
		CTR_WriteU32LE(out, owner);
		CTR_WriteU32LE(out + 4, offset);
		CTR_WriteU32LE(out + 8, target);
		CTR_WriteU32LE(out + 12, targetOffset);
		RGWrite64(out + 16, external);
	}
	return (size_t)(out - (u8 *)buffer) == size;
}
int NativeResidentGraph_PrepareCheckpoint(const void *buffer, size_t bytes, struct NativeResidentGraph **out)
{
	if (!buffer || !out || bytes < RGCP_HEADER)
		return 0;
	const u8 *p = buffer, *end = p + bytes;
	if (memcmp(p, "RGCP", 4) || CTR_ReadU32LE(p + 4) != 1 || CTR_ReadU32LE(p + 8) != bytes || CTR_ReadU32LE(p + 12) != RGAbi() ||
	    CTR_ReadU32LE(p + 16) != sizeof(void *))
		return 0;
	u32 wireBytes = CTR_ReadU32LE(p + 20), objectCount = CTR_ReadU32LE(p + 24), slotCount = CTR_ReadU32LE(p + 28), kind = CTR_ReadU32LE(p + 32),
	    at = CTR_ReadU32LE(p + 36);
	p += RGCP_HEADER;
	if (wireBytes > RG_MAX_BYTES || objectCount > RG_MAX_OBJECTS || slotCount > RG_MAX_OBJECTS || (size_t)(end - p) < (size_t)wireBytes + 4)
		return 0;
	const u8 *wire = p;
	p += wireBytes;
	u32 ptrBytes = CTR_ReadU32LE(p);
	if (ptrBytes % 4 || ptrBytes / 4 > RG_MAX_OBJECTS || ptrBytes > (size_t)(end - p) - 4)
		return 0;
	struct NativePtrMapEntry *entries = ptrBytes ? malloc(ptrBytes / 4 * sizeof(*entries)) : NULL;
	if (ptrBytes && !entries)
		return 0;
	struct NativePtrMapView map;
	if (NativePtrMap_Decode((void *)wire, wireBytes, p, (size_t)ptrBytes + 4, entries, ptrBytes / 4, &map) != NATIVE_PTRMAP_OK)
	{
		free(entries);
		return 0;
	}
	struct NativeResidentGraph *g = NULL;
	enum NativeAssetResult r = NativeResidentGraph_Build(&map, (enum NativeResidentKind)kind, at, &g, NULL);
	free(entries);
	if (r != NATIVE_ASSET_OK)
		return 0;
	if (g->count != objectCount || g->slotCount != slotCount)
	{
		NativeResidentGraph_Free(g);
		return 0;
	}
	*out = g;
	return 1;
}
static int RGShapeValid(struct RGObject o, const u8 *saved)
{
	// Counts/strides define the allocations prepared from validated wire.
	// Mutable coordinates, colors and animation phase remain snapshot state.
	size_t w, h, a;
	NativeResident_Layout(o.binding.kind, &w, &h, &a);
	(void)w;
	(void)a;
	for (size_t i = 0; i < o.binding.count; i++)
	{
		const u8 *live = (const u8 *)o.binding.resident + i * h, *p = saved + i * h;
#define RG_SAME(type, field)                                                                                              \
	if (memcmp(live + offsetof(struct type, field), p + offsetof(struct type, field), sizeof(((struct type *)0)->field))) \
	return 0
		switch (o.binding.kind)
		{
		case NR_LEVEL:
			RG_SAME(Level, numInstances);
			RG_SAME(Level, numModels);
			RG_SAME(Level, numWaterVertices);
			RG_SAME(Level, numSpawnType2);
			RG_SAME(Level, numSpawnType2_PosRot);
			RG_SAME(Level, numSCVert);
			break;
		case NR_MODEL:
			RG_SAME(Model, numHeaders);
			break;
		case NR_MODEL_HEADER:
			RG_SAME(ModelHeader, numAnimations);
			break;
		case NR_MESH:
			RG_SAME(mesh_info, numQuadBlock);
			RG_SAME(mesh_info, numVertex);
			RG_SAME(mesh_info, numBspNodes);
			break;
		case NR_NAV:
			RG_SAME(NavHeader, numPoints);
			break;
		case NR_ANIMTEX:
			RG_SAME(AnimTex, numFrames);
			RG_SAME(AnimTex, frameSkip);
			break;
		case NR_ICONGROUP:
			RG_SAME(IconGroup, numIcons);
			break;
		case NR_TEXLOOKUP:
			RG_SAME(LevTexLookup, numIcon);
			RG_SAME(LevTexLookup, numIconGroup);
			break;
		case NR_SPAWN1:
			RG_SAME(SpawnType1, count);
			break;
		case NR_SPAWN2:
		case NR_SPAWN2_ROT:
			RG_SAME(SpawnType2, numCoords);
			break;
		case NR_SKYBOX:
			RG_SAME(Skybox, numVertex);
			RG_SAME(Skybox, numFaces);
			break;
		case NR_MODEL_ANIM:
			RG_SAME(ModelAnim, numFrames);
			RG_SAME(ModelAnim, frameSize);
			break;
		default:
			break;
		}
#undef RG_SAME
	}
	return 1;
}
int NativeResidentGraph_ApplyCheckpoint(const void *buffer, size_t bytes, struct NativeResidentGraph *g, NativeResidentGraphRebaseExternal rebase, void *user)
{
	if (!buffer || !g || bytes < RGCP_HEADER)
		return 0;
	const u8 *p = buffer, *end = p + bytes;
	if (memcmp(p, "RGCP", 4) || CTR_ReadU32LE(p + 4) != 1 || CTR_ReadU32LE(p + 8) != bytes || CTR_ReadU32LE(p + 12) != RGAbi() ||
	    CTR_ReadU32LE(p + 16) != sizeof(void *))
		return 0;
	u32 wireBytes = CTR_ReadU32LE(p + 20), objectCount = CTR_ReadU32LE(p + 24), slotCount = CTR_ReadU32LE(p + 28);
	p += RGCP_HEADER;
	if (wireBytes != g->map.originSize || (size_t)(end - p) < (size_t)wireBytes + 4)
		return 0;
	p += wireBytes;
	u32 ptrBytes = CTR_ReadU32LE(p);
	if (ptrBytes > (size_t)(end - p) - 4)
		return 0;
	p += ptrBytes + 4;
	int ok = g->count == objectCount && g->slotCount == slotCount;
	for (size_t i = 0; ok && i < g->count; i++)
	{
		if ((size_t)(end - p) < RGCP_OBJECT)
		{
			ok = 0;
			break;
		}
		struct RGObject o = g->objects[i];
		u32 savedBytes = CTR_ReadU32LE(p + 12);
		ok = CTR_ReadU32LE(p) == (u32)o.binding.kind && CTR_ReadU32LE(p + 4) == o.binding.offset && CTR_ReadU32LE(p + 8) == o.binding.count &&
		     savedBytes == (o.owned ? o.bytes : 0);
		p += RGCP_OBJECT;
		if (savedBytes > (size_t)(end - p))
		{
			ok = 0;
			break;
		}
		if (ok && savedBytes)
		{
			ok = RGShapeValid(o, p);
			for (size_t j = 0; ok && j < g->count; j++)
			{
				struct RGObject alias = g->objects[j];
				uintptr_t start = (uintptr_t)o.binding.resident, address = (uintptr_t)alias.binding.resident;
				if (!alias.owned && address >= start && address - start < savedBytes)
					ok = RGShapeValid(alias, p + (address - start));
			}
			if (ok)
				memcpy(o.binding.resident, p, savedBytes);
		}
		p += savedBytes;
	}
	if (ok && (size_t)(end - p) != (size_t)slotCount * RGCP_SLOT)
		ok = 0;
	for (size_t i = 0; ok && i < slotCount; i++, p += RGCP_SLOT)
	{
		u32 owner, offset;
		if (!RGOwner(g, g->slots[i], sizeof(void *), &owner, &offset) || CTR_ReadU32LE(p) != owner || CTR_ReadU32LE(p + 4) != offset)
		{
			ok = 0;
			break;
		}
		u32 target = CTR_ReadU32LE(p + 8), targetOffset = CTR_ReadU32LE(p + 12);
		u64 external = RGRead64(p + 16);
		void *value = NULL;
		if (target == RGCP_NULL)
			ok = targetOffset == 0 && external == 0;
		else if (target == RGCP_EXTERNAL)
		{
			ok = targetOffset == 0 && external && rebase && rebase(user, (uintptr_t)external, &value) && value;
		}
		else
		{
			value = RGAddress(g, target, targetOffset, 0);
			ok = value != NULL && external == 0;
		}
		if (ok)
			memcpy(g->slots[i], &value, sizeof(value));
	}
	return ok;
}
int NativeResidentGraph_RestoreCheckpoint(const void *buffer, size_t bytes, struct NativeResidentGraph **out, NativeResidentGraphRebaseExternal rebase,
                                          void *user)
{
	struct NativeResidentGraph *g = NULL;
	if (!out || !NativeResidentGraph_PrepareCheckpoint(buffer, bytes, &g))
		return 0;
	if (!NativeResidentGraph_ApplyCheckpoint(buffer, bytes, g, rebase, user))
	{
		NativeResidentGraph_Free(g);
		return 0;
	}
	*out = g;
	return 1;
}
int NativeResidentGraph_RebaseSavedRange(const void *buffer, size_t bytes, const struct NativeResidentGraph *g, uintptr_t saved, size_t width, void **live)
{
	if (!buffer || !g || !saved || !live || bytes < RGCP_HEADER)
		return 0;
	const u8 *p = buffer, *end = p + bytes;
	if (memcmp(p, "RGCP", 4) || CTR_ReadU32LE(p + 4) != 1 || CTR_ReadU32LE(p + 8) != bytes || CTR_ReadU32LE(p + 12) != RGAbi() ||
	    CTR_ReadU32LE(p + 16) != sizeof(void *) || CTR_ReadU32LE(p + 20) != g->map.originSize || CTR_ReadU32LE(p + 24) != g->count)
		return 0;
	u32 rawBytes = CTR_ReadU32LE(p + 20);
	u64 rawBase = RGRead64(p + 40);
	if (rawBytes > bytes - RGCP_HEADER)
		return 0;
	if ((u64)saved >= rawBase && (u64)saved - rawBase <= rawBytes && width <= rawBytes - ((u64)saved - rawBase))
	{
		*live = g->map.origin + (size_t)((u64)saved - rawBase);
		return 1;
	}
	p += RGCP_HEADER;
	if (rawBytes > (size_t)(end - p))
		return 0;
	p += rawBytes;
	if ((size_t)(end - p) < 4)
		return 0;
	u32 ptrBytes = CTR_ReadU32LE(p);
	if (ptrBytes > (size_t)(end - p) - 4)
		return 0;
	p += 4 + ptrBytes;
	for (size_t i = 0; i < g->count; i++)
	{
		if ((size_t)(end - p) < RGCP_OBJECT)
			return 0;
		u32 size = CTR_ReadU32LE(p + 12);
		u64 base = RGRead64(p + 16);
		p += RGCP_OBJECT;
		if (size > (size_t)(end - p) || size != (g->objects[i].owned ? g->objects[i].bytes : 0))
			return 0;
		if (base && (u64)saved >= base && (u64)saved - base <= size && width <= size - ((u64)saved - base))
		{
			*live = (u8 *)g->objects[i].binding.resident + (size_t)((u64)saved - base);
			return 1;
		}
		p += size;
	}
	return 0;
}

int NativeResidentGraph_Contains(const struct NativeResidentGraph *g, const void *p, size_t bytes)
{
	u32 owner, offset;
	return g && p && RGOwner(g, p, bytes, &owner, &offset);
}

int NativeResidentGraph_RebaseSavedPointer(const void *buffer, size_t bytes, const struct NativeResidentGraph *g, uintptr_t saved, void **live)
{
	return NativeResidentGraph_RebaseSavedRange(buffer, bytes, g, saved, 1, live);
}
