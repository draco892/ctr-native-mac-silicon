#ifndef PLATFORM_NATIVE_SCENE_ASSETS_H
#define PLATFORM_NATIVE_SCENE_ASSETS_H
#include <platform/native_model_library.h>
#include <platform/native_resident_graph.h>
struct NativeSceneAssetOwner;
struct NativeSceneAssets { struct NativeSceneAssetOwner *owners; struct NativeModelLibrary library; };
// Host-only shadow ownership for legacy loader integration. Initialize to zero.
// Capture copies immutable wire bytes BEFORE legacy in-place relocation; source
// address is only an identity/range, never stored in a wire field. Maps/records
// are owned until ForgetRange/Reset. Callers must discard borrowed views then.
void NativeSceneAssets_Reset(struct NativeSceneAssets *assets);
enum NativePtrMapResult NativeSceneAssets_Capture(struct NativeSceneAssets *assets,const void *source,size_t bytes,const void *ptr,size_t ptrBytes);
enum NativePtrMapResult NativeSceneAssets_BeginRaw(struct NativeSceneAssets *assets,const void *source,size_t bytes);
enum NativePtrMapResult NativeSceneAssets_CompletePtr(struct NativeSceneAssets *assets,const void *source,const void *ptr,size_t ptrBytes);
void NativeSceneAssets_ForgetRange(struct NativeSceneAssets *assets,const void *begin,const void *end);
enum NativeAssetResult NativeSceneAssets_GetLevel(const struct NativeSceneAssets *assets,const void *source,struct NativeLevelView *out);
enum NativeAssetResult NativeSceneAssets_GetModel(const struct NativeSceneAssets *assets,const void *sourceModel,struct NativeModelView *out);
// Publish only after every reachable resident record has validated.
enum NativeAssetResult NativeSceneAssets_Materialize(struct NativeSceneAssets *, const void *source, enum NativeResidentKind, void **root);
int NativeSceneAssets_ResolveWireSlot(const struct NativeSceneAssets *, const void *slot, size_t targetBytes, void **target);
int NativeSceneAssets_Contains(const struct NativeSceneAssets *, const void *, size_t);
int NativeSceneAssets_RebaseSavedPointer(const void *, size_t, const struct NativeSceneAssets *, uintptr_t, void **);
// Versioned snapshot of immutable owners, pending maps and library references.
// Source identities are rebased by the caller; restore replaces state only after
// every owner/map/model has validated. No serialized host view is dereferenced.
typedef int (*NativeSceneSourceRebase)(void *user,u64 source,u32 bytes,uintptr_t *live);
size_t NativeSceneAssets_CheckpointSize(const struct NativeSceneAssets *assets);
int NativeSceneAssets_CaptureCheckpoint(const struct NativeSceneAssets *assets,void *dst,size_t bytes);
int NativeSceneAssets_RestoreCheckpoint(struct NativeSceneAssets *assets,const void *src,size_t bytes,NativeSceneSourceRebase rebase,void *user);
// Game-owned context; portable tests use explicit independent contexts above.
extern struct NativeSceneAssets gNativeSceneAssets;
#endif
