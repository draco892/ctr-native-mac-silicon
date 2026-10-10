#include <platform/native_scene_assets.h>
#include <stdlib.h>
#include <string.h>
struct NativeSceneAssetOwner {
    struct NativeSceneAssetOwner *next;
    uintptr_t source; size_t bytes;
    u8 *wire; struct NativePtrMapEntry *entries; struct NativePtrMapView map;
    int ready;
};
struct NativeSceneAssets gNativeSceneAssets={0};
static void SceneAssets_Free(struct NativeSceneAssets *assets,struct NativeSceneAssetOwner *owner)
{ NativeModelLibrary_DropOwner(&assets->library,&owner->map); free(owner->entries); free(owner->wire); free(owner); }
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
enum NativeAssetResult NativeSceneAssets_GetLevel(const struct NativeSceneAssets *assets,const void *source,struct NativeLevelView *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    struct NativeSceneAssetOwner *owner=SceneAssets_Find(assets,source);
    if(owner==NULL || !owner->ready) return NATIVE_ASSET_NOT_FOUND;
    return NativeLevel_Open(&owner->map,out);
}
enum NativeAssetResult NativeSceneAssets_GetModel(const struct NativeSceneAssets *assets,const void *sourceModel,struct NativeModelView *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    if(assets==NULL || sourceModel==NULL) return NATIVE_ASSET_NOT_FOUND;
    uintptr_t address=(uintptr_t)sourceModel;
    for(struct NativeSceneAssetOwner *owner=assets->owners;owner!=NULL;owner=owner->next) {
        if(owner->ready && address>=owner->source && address-owner->source<owner->bytes)
            return NativeModel_Open(&owner->map,(u32)(address-owner->source),out);
    }
    return NATIVE_ASSET_NOT_FOUND;
}
