#ifndef PLATFORM_NATIVE_MODEL_LIBRARY_H
#define PLATFORM_NATIVE_MODEL_LIBRARY_H

#include <platform/native_asset_readers.h>

enum { NATIVE_MODEL_LIBRARY_SLOTS = 0xe3, NATIVE_MODEL_LIBRARY_CLEAR_COUNT = 0xe2 };

struct NativeModelReference
{
	const struct NativePtrMapView *owner;
	u32 offset;
};

struct NativeModelLibrary
{
	struct NativeModelReference slots[NATIVE_MODEL_LIBRARY_SLOTS];
};

// Runtime-only state, initialized by Reset. Asset bytes are never patched.
// ID -1 is skipped, 0..226 accepted; other IDs rejected before indexing.
// Later models/sources replace earlier IDs, as in LibraryOfModels_Store.
// Each Store is transactional: any malformed model leaves the library intact.
// Owner map, entries and asset must outlive their references. DropOwner BEFORE
// re-decoding/overwriting/freeing an owner. Rebind of identical bytes preserves
// refs: lookup reopens the model from owner+offset without caching host addresses.
void NativeModelLibrary_Reset(struct NativeModelLibrary *library);
// Retail Clear intentionally preserves slot 226. Reset clears all 227 slots.
void NativeModelLibrary_Clear(struct NativeModelLibrary *library);
void NativeModelLibrary_DropOwner(struct NativeModelLibrary *library, const struct NativePtrMapView *owner);
enum NativeAssetResult NativeModelLibrary_StoreModel(struct NativeModelLibrary *library, const struct NativeModelView *model);
enum NativeAssetResult NativeModelLibrary_StoreMpk(struct NativeModelLibrary *library, const struct NativeMpkView *mpk);
enum NativeAssetResult NativeModelLibrary_StoreLevel(struct NativeModelLibrary *library, const struct NativeLevelView *level);
enum NativeAssetResult NativeModelLibrary_Get(const struct NativeModelLibrary *library, s32 id, struct NativeModelView *out);

#endif
