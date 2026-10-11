#include <platform/native_scene_assets.h>
#include <stdlib.h>
#include <string.h>
struct NativeSceneAssetOwner {
    struct NativeSceneAssetOwner *next;
    uintptr_t source; size_t bytes;
    u8 *wire; struct NativePtrMapEntry *entries; struct NativePtrMapView map;
    int ready;
	struct NativeResidentGraph *graph;
	const void *savedGraph;
	size_t savedGraphBytes;
};
struct NativeSceneAssets gNativeSceneAssets={0};
static void SceneAssets_Free(struct NativeSceneAssets *assets,struct NativeSceneAssetOwner *owner)
{
	NativeModelLibrary_DropOwner(&assets->library, &owner->map);
	NativeResidentGraph_Free(owner->graph);
	free(owner->entries);
	free(owner->wire);
	free(owner);
}
void NativeSceneAssets_Reset(struct NativeSceneAssets *assets)
{
    if(assets==NULL) return;
    while(assets->owners!=NULL) { struct NativeSceneAssetOwner *owner=assets->owners; assets->owners=owner->next; SceneAssets_Free(assets,owner); }
    NativeModelLibrary_Reset(&assets->library);
}
static struct NativeSceneAssetOwner *SceneAssets_Find(const struct NativeSceneAssets *assets,const void *source)
{
    if(assets==NULL) return NULL;
    for(struct NativeSceneAssetOwner *owner=assets->owners;owner!=NULL;owner=owner->next) if(owner->source==(uintptr_t)source) return owner;
    return NULL;
}
void NativeSceneAssets_ForgetRange(struct NativeSceneAssets *assets,const void *begin,const void *end)
{
    uintptr_t low=(uintptr_t)begin,high=(uintptr_t)end;
    if(assets==NULL || high<=low) return;
    struct NativeSceneAssetOwner **slot=&assets->owners;
    while(*slot!=NULL) {
        struct NativeSceneAssetOwner *owner=*slot;
        if(owner->source<high && low<owner->source+owner->bytes) { *slot=owner->next; SceneAssets_Free(assets,owner); }
        else slot=&owner->next;
    }
}
static struct NativeSceneAssetOwner *SceneAssets_Copy(const void *source,size_t bytes)
{
    if(source==NULL || bytes==0 || bytes>UINT32_MAX || (uintptr_t)source>UINTPTR_MAX-bytes) return NULL;
    struct NativeSceneAssetOwner *owner=calloc(1,sizeof(*owner)); if(owner==NULL) return NULL;
    owner->wire=malloc(bytes); if(owner->wire==NULL) { free(owner); return NULL; }
    memcpy(owner->wire,source,bytes); owner->source=(uintptr_t)source; owner->bytes=bytes; return owner;
}
static enum NativePtrMapResult SceneAssets_Decode(struct NativeSceneAssetOwner *owner,const void *ptr,size_t ptrBytes)
{
    size_t count; enum NativePtrMapResult status=NativePtrMap_GetCount(ptr,ptrBytes,&count);
    if(status!=NATIVE_PTRMAP_OK) return status;
    if(count>SIZE_MAX/sizeof(*owner->entries)) return NATIVE_PTRMAP_TABLE_TOO_SMALL;
    struct NativePtrMapEntry *entries=count ? malloc(count*sizeof(*entries)) : NULL;
    if(count && entries==NULL) return NATIVE_PTRMAP_TABLE_TOO_SMALL;
    struct NativePtrMapView map;
    status=NativePtrMap_Decode(owner->wire,owner->bytes,ptr,ptrBytes,entries,count,&map);
    if(status!=NATIVE_PTRMAP_OK) { free(entries); return status; }
    owner->entries=entries; owner->map=map; owner->ready=1; return NATIVE_PTRMAP_OK;
}
static void SceneAssets_Publish(struct NativeSceneAssets *assets,struct NativeSceneAssetOwner *owner)
{
    NativeSceneAssets_ForgetRange(assets,(const void *)owner->source,(const void *)(owner->source+owner->bytes));
    owner->next=assets->owners; assets->owners=owner;
}
enum NativePtrMapResult NativeSceneAssets_Capture(struct NativeSceneAssets *assets,const void *source,size_t bytes,const void *ptr,size_t ptrBytes)
{
    if(assets==NULL || source==NULL) return NATIVE_PTRMAP_INVALID_ARGUMENT;
    struct NativeSceneAssetOwner *owner=SceneAssets_Copy(source,bytes); if(owner==NULL) return NATIVE_PTRMAP_INVALID_ARGUMENT;
    enum NativePtrMapResult status=SceneAssets_Decode(owner,ptr,ptrBytes);
    if(status!=NATIVE_PTRMAP_OK) { SceneAssets_Free(assets,owner); return status; }
    SceneAssets_Publish(assets,owner); return NATIVE_PTRMAP_OK;
}
enum NativePtrMapResult NativeSceneAssets_BeginRaw(struct NativeSceneAssets *assets,const void *source,size_t bytes)
{
    if(assets==NULL || source==NULL) return NATIVE_PTRMAP_INVALID_ARGUMENT;
    struct NativeSceneAssetOwner *owner=SceneAssets_Copy(source,bytes); if(owner==NULL) return NATIVE_PTRMAP_INVALID_ARGUMENT;
    SceneAssets_Publish(assets,owner); return NATIVE_PTRMAP_OK;
}
enum NativePtrMapResult NativeSceneAssets_CompletePtr(struct NativeSceneAssets *assets,const void *source,const void *ptr,size_t ptrBytes)
{
    struct NativeSceneAssetOwner *owner=SceneAssets_Find(assets,source);
    if(owner==NULL || owner->ready) return NATIVE_PTRMAP_INVALID_ARGUMENT;
    return SceneAssets_Decode(owner,ptr,ptrBytes);
}
enum NativeAssetResult NativeSceneAssets_Materialize(struct NativeSceneAssets *assets, const void *source, enum NativeResidentKind kind, void **root)
{
	if (!root || (kind != NR_LEVEL && kind != NR_MPK && kind != NR_MODEL))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	struct NativeSceneAssetOwner *o = SceneAssets_Find(assets, source);
	if (!o || !o->ready)
		return NATIVE_ASSET_NOT_FOUND;
	if (o->graph)
	{
		u32 offset;
		if (!NativeResidentGraph_WireOffset(o->graph, NativeResidentGraph_Root(o->graph), kind, &offset) || offset)
			return NATIVE_ASSET_INVALID_ARGUMENT;
	}
	else
	{
		struct NativeResidentGraph *pending = NULL;
		enum NativeAssetResult r = NativeResidentGraph_Build(&o->map, kind, 0, &pending, NULL);
		if (r != NATIVE_ASSET_OK)
			return r;
		o->graph = pending;
	}
	*root = NativeResidentGraph_Root(o->graph);
	return NATIVE_ASSET_OK;
}
int NativeSceneAssets_Contains(const struct NativeSceneAssets *assets, const void *p, size_t bytes)
{
	if (!assets)
		return 0;
	for (const struct NativeSceneAssetOwner *o = assets->owners; o; o = o->next)
		if (NativeResidentGraph_Contains(o->graph, p, bytes))
			return 1;
	return 0;
}
enum NativeAssetResult NativeSceneAssets_GetLevel(const struct NativeSceneAssets *assets,const void *source,struct NativeLevelView *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    struct NativeSceneAssetOwner *owner=SceneAssets_Find(assets,source);
	if (!owner && assets)
		for (struct NativeSceneAssetOwner *o = assets->owners; o; o = o->next)
			if (o->graph && NativeResidentGraph_Root(o->graph) == source)
			{
				u32 offset;
				if (NativeResidentGraph_WireOffset(o->graph, source, NR_LEVEL, &offset))
				{
					owner = o;
					break;
				}
			}
	if (owner == NULL || !owner->ready)
		return NATIVE_ASSET_NOT_FOUND;
	return NativeLevel_Open(&owner->map,out);
}
enum NativeAssetResult NativeSceneAssets_GetModel(const struct NativeSceneAssets *assets,const void *sourceModel,struct NativeModelView *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    if(assets==NULL || sourceModel==NULL) return NATIVE_ASSET_NOT_FOUND;
    uintptr_t address=(uintptr_t)sourceModel;
    for(struct NativeSceneAssetOwner *owner=assets->owners;owner!=NULL;owner=owner->next) {
		u32 offset;
		if (owner->graph && NativeResidentGraph_WireOffset(owner->graph, sourceModel, NR_MODEL, &offset))
			return NativeModel_Open(&owner->map, offset, out);
		if (owner->ready && address >= owner->source && address - owner->source < owner->bytes)
			return NativeModel_Open(&owner->map,(u32)(address-owner->source),out);
    }
    return NATIVE_ASSET_NOT_FOUND;
}

// Header + stable library owner-index/offset pairs; owners use wire/PTR records.
enum { SCENE_CHECKPOINT_HEADER=16+NATIVE_MODEL_LIBRARY_SLOTS*8, SCENE_CHECKPOINT_OWNER_HEADER=24 };
static u64 SceneCheckpoint_Read64(const u8 *p) { return CTR_ReadU32LE(p)|((u64)CTR_ReadU32LE(p+4)<<32); }
static void SceneCheckpoint_Write64(u8 *p,u64 value) { CTR_WriteU32LE(p,(u32)value); CTR_WriteU32LE(p+4,(u32)(value>>32)); }
size_t NativeSceneAssets_CheckpointSize(const struct NativeSceneAssets *assets)
{
    if(assets==NULL) return 0;
    size_t bytes=SCENE_CHECKPOINT_HEADER;
    for(const struct NativeSceneAssetOwner *o=assets->owners;o!=NULL;o=o->next) {
        if(o->bytes>UINT32_MAX || o->map.count>UINT32_MAX/4) return 0;
		size_t graphBytes = o->graph ? NativeResidentGraph_CheckpointSize(o->graph) : 0;
		if (o->graph && !graphBytes)
			return 0;
		u64 extra = graphBytes + SCENE_CHECKPOINT_OWNER_HEADER + (u64)o->bytes + (u64)o->map.count * 4;
		if (extra > UINT32_MAX || bytes > UINT32_MAX - extra)
			return 0;
		bytes+=(size_t)extra;
    }
    return bytes;
}
int NativeSceneAssets_CaptureCheckpoint(const struct NativeSceneAssets *assets,void *dst,size_t bytes)
{
    size_t size=NativeSceneAssets_CheckpointSize(assets);
    if(!size || dst==NULL || bytes<size) return 0;
    u8 *out=dst; memset(out,0,size);
	CTR_WriteU32LE(out, 0x53414353u);
	CTR_WriteU32LE(out + 4, 2);
	CTR_WriteU32LE(out + 8, (u32)size);
	for (u32 i = 0; i < NATIVE_MODEL_LIBRARY_SLOTS; i++)
	{
		u32 owner=0; const struct NativeSceneAssetOwner *o=assets->owners;
        if(assets->library.slots[i].owner!=NULL) {
            for(;o!=NULL;o=o->next,owner++) if(&o->map==assets->library.slots[i].owner) break;
            if(o==NULL || !o->ready) return 0;
        } else owner=UINT32_MAX;
        CTR_WriteU32LE(out+16+i*8,owner); CTR_WriteU32LE(out+20+i*8,assets->library.slots[i].offset);
	}
	size_t at=SCENE_CHECKPOINT_HEADER; u32 count=0;
    for(const struct NativeSceneAssetOwner *o=assets->owners;o!=NULL;o=o->next) {
        SceneCheckpoint_Write64(out+at,o->source); CTR_WriteU32LE(out+at+8,(u32)o->bytes);
        CTR_WriteU32LE(out+at+12,(u32)o->map.count); CTR_WriteU32LE(out+at+16,(u32)o->ready);
		size_t graphBytes = o->graph ? NativeResidentGraph_CheckpointSize(o->graph) : 0;
		CTR_WriteU32LE(out + at + 20, (u32)graphBytes);
		at += SCENE_CHECKPOINT_OWNER_HEADER;
		memcpy(out + at, o->wire, o->bytes);
		at += o->bytes;
		for(size_t i=0;i<o->map.count;i++,at+=4) CTR_WriteU32LE(out+at,o->entries[i].slotOffset);
		if (graphBytes && !NativeResidentGraph_CaptureCheckpoint(o->graph, out + at, graphBytes))
			return 0;
		at += graphBytes;
		count++;
	}
	CTR_WriteU32LE(out+12,count); return 1;
}
struct SceneGraphRebase
{
	struct NativeSceneAssets *assets;
	NativeSceneSourceRebase source;
	void *user;
};
static int SceneAssets_RebaseGraph(void *user, uintptr_t saved, void **live)
{
	struct SceneGraphRebase *ctx = user;
	for (unsigned pass = 0; pass < 2; pass++)
		for (struct NativeSceneAssetOwner *o = ctx->assets->owners; o; o = o->next)
			if (o->savedGraph && NativeResidentGraph_RebaseSavedRange(o->savedGraph, o->savedGraphBytes, o->graph, saved, pass ? 0 : 1, live))
				return 1;
	uintptr_t address;
	if (!ctx->source(ctx->user, saved, 0, &address))
		return 0;
	*live = (void *)address;
	return 1;
}
int NativeSceneAssets_RestoreCheckpoint(struct NativeSceneAssets *assets,const void *src,size_t bytes,NativeSceneSourceRebase rebase,void *user)
{
    if(assets==NULL || src==NULL || rebase==NULL || bytes<SCENE_CHECKPOINT_HEADER) return 0;
    const u8 *wire=src;
	if (CTR_ReadU32LE(wire) != 0x53414353u || CTR_ReadU32LE(wire + 4) != 2 || CTR_ReadU32LE(wire + 8) != bytes)
		return 0;
	u32 count = CTR_ReadU32LE(wire + 12);
	if(count>(bytes-SCENE_CHECKPOINT_HEADER)/SCENE_CHECKPOINT_OWNER_HEADER) return 0;
    struct NativeSceneAssets staged={0};
    struct NativeSceneAssetOwner **owners=calloc(count ? count : 1,sizeof(*owners));
    if(owners==NULL) return 0;
    size_t at=SCENE_CHECKPOINT_HEADER; int success=0;
    struct NativeSceneAssetOwner **tail=&staged.owners;
    for(u32 index=0;index<count;index++) {
        if(at>bytes || bytes-at<SCENE_CHECKPOINT_OWNER_HEADER) goto done;
        u64 oldSource=SceneCheckpoint_Read64(wire+at); u32 payload=CTR_ReadU32LE(wire+at+8),slots=CTR_ReadU32LE(wire+at+12),ready=CTR_ReadU32LE(wire+at+16);
		u32 graphBytes = CTR_ReadU32LE(wire + at + 20);
		if (!payload || ready > 1 || (!ready && (slots || graphBytes)))
			goto done;
		uintptr_t live;
		if(!rebase(user,oldSource,payload,&live) || !live || payload>UINTPTR_MAX-live) goto done;
        at+=SCENE_CHECKPOINT_OWNER_HEADER;
        if(payload>bytes-at) goto done;
        struct NativeSceneAssetOwner *o=SceneAssets_Copy(wire+at,payload); if(o==NULL) goto done;
        at+=payload; o->source=live;
        // Identity ranges must remain disjoint; publication must not drop peers.
        for(u32 previous=0;previous<index;previous++) if(live<owners[previous]->source+owners[previous]->bytes && owners[previous]->source<live+payload) { SceneAssets_Free(&staged,o); goto done; }
        *tail=o; tail=&o->next; owners[index]=o;
        if(slots>(bytes-at)/4 || slots>UINT32_MAX/4) goto done;
#if SIZE_MAX <= UINT32_MAX
        if(slots>(SIZE_MAX-4)/4) goto done;
#endif
        if(ready) {
            size_t ptrBytes=4+(size_t)slots*4; u8 *ptr=malloc(ptrBytes); if(ptr==NULL) goto done;
            CTR_WriteU32LE(ptr,slots*4); memcpy(ptr+4,wire+at,(size_t)slots*4);
            enum NativePtrMapResult result=SceneAssets_Decode(o,ptr,ptrBytes); free(ptr);
            if(result!=NATIVE_PTRMAP_OK) goto done;
        }
        at+=(size_t)slots*4;
		if (graphBytes > bytes - at)
			goto done;
		if (graphBytes)
		{
			o->savedGraph = wire + at;
			o->savedGraphBytes = graphBytes;
			if (!NativeResidentGraph_PrepareCheckpoint(wire + at, graphBytes, &o->graph))
				goto done;
		}
		at += graphBytes;
	}
	if(at!=bytes) goto done;
	struct SceneGraphRebase graphRebase = {&staged, rebase, user};
	for (u32 i = 0; i < count; i++)
		if (owners[i]->graph &&
		    !NativeResidentGraph_ApplyCheckpoint(owners[i]->savedGraph, owners[i]->savedGraphBytes, owners[i]->graph, SceneAssets_RebaseGraph, &graphRebase))
			goto done;
	for (u32 i = 0; i < count; i++)
	{
		owners[i]->savedGraph = NULL;
		owners[i]->savedGraphBytes = 0;
	}
	for (u32 i = 0; i < NATIVE_MODEL_LIBRARY_SLOTS; i++)
	{
		u32 index=CTR_ReadU32LE(wire+16+i*8),offset=CTR_ReadU32LE(wire+20+i*8);
        if(index==UINT32_MAX) continue;
        if(index>=count || !owners[index]->ready) goto done;
        struct NativeModelView model;
        if(NativeModel_Open(&owners[index]->map,offset,&model)!=NATIVE_ASSET_OK || model.id!=(s32)i || NativeModelLibrary_StoreModel(&staged.library,&model)!=NATIVE_ASSET_OK) goto done;
	}
	NativeSceneAssets_Reset(assets); *assets=staged; memset(&staged,0,sizeof(staged)); success=1;
done:
    NativeSceneAssets_Reset(&staged); free(owners); return success;
}

int NativeSceneAssets_RebaseSavedPointer(const void *blob, size_t bytes, const struct NativeSceneAssets *assets, uintptr_t saved, void **live)
{
	if (!blob || !assets || !live || bytes < SCENE_CHECKPOINT_HEADER)
		return 0;
	const u8 *p = blob;
	if (CTR_ReadU32LE(p) != 0x53414353u || CTR_ReadU32LE(p + 4) != 2 || CTR_ReadU32LE(p + 8) != bytes)
		return 0;
	size_t at = SCENE_CHECKPOINT_HEADER;
	u32 count = CTR_ReadU32LE(p + 12);
	const struct NativeSceneAssetOwner *o = assets->owners;
	for (u32 i = 0; i < count; i++, o = o->next)
	{
		if (!o || at > bytes || bytes - at < SCENE_CHECKPOINT_OWNER_HEADER)
			return 0;
		u32 payload = CTR_ReadU32LE(p + at + 8), slots = CTR_ReadU32LE(p + at + 12), graphBytes = CTR_ReadU32LE(p + at + 20);
		at += SCENE_CHECKPOINT_OWNER_HEADER;
		if (payload > bytes - at)
			return 0;
		at += payload;
		if (slots > (bytes - at) / 4)
			return 0;
		at += (size_t)slots * 4;
		if (graphBytes > bytes - at)
			return 0;
		if (graphBytes && NativeResidentGraph_RebaseSavedPointer(p + at, graphBytes, o->graph, saved, live))
			return 1;
		at += graphBytes;
	}
	return 0;
}

int NativeSceneAssets_ResolveWireSlot(const struct NativeSceneAssets *assets, const void *slot, size_t targetBytes, void **target)
{
	if (!assets || !slot || !target)
		return 0;
	for (const struct NativeSceneAssetOwner *o = assets->owners; o; o = o->next)
		if (o->graph)
		{
			const struct NativePtrMapView *map = NativeResidentGraph_Map(o->graph);
			uintptr_t address = (uintptr_t)slot, base = (uintptr_t)map->origin;
			if (address >= base && address - base <= map->originSize && sizeof(u32) <= map->originSize - (address - base))
				return NativePtrMap_Resolve(map, (u32)(address - base), targetBytes, target) == NATIVE_PTRMAP_OK;
		}
	return 0;
}
