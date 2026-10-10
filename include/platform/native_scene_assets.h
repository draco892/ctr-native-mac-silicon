#ifndef PLATFORM_NATIVE_SCENE_ASSETS_H
#define PLATFORM_NATIVE_SCENE_ASSETS_H
#include <platform/native_model_library.h>
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
// Game-owned context; portable tests use explicit independent contexts above.
extern struct NativeSceneAssets gNativeSceneAssets;
#endif
