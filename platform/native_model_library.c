#include <platform/native_model_library.h>

#include <string.h>

void NativeModelLibrary_Reset(struct NativeModelLibrary *library)
{
	if (library != NULL)
		memset(library, 0, sizeof(*library));
}

void NativeModelLibrary_Clear(struct NativeModelLibrary *library)
{
	if (library != NULL)
		memset(library->slots, 0, NATIVE_MODEL_LIBRARY_CLEAR_COUNT * sizeof(library->slots[0]));
}

void NativeModelLibrary_DropOwner(struct NativeModelLibrary *library, const struct NativePtrMapView *owner)
{
	if (library == NULL || owner == NULL)
		return;
	for (size_t i = 0; i < NATIVE_MODEL_LIBRARY_SLOTS; i++)
		if (library->slots[i].owner == owner)
			memset(&library->slots[i], 0, sizeof(library->slots[i]));
}

static enum NativeAssetResult NativeModelLibrary_Insert(struct NativeModelLibrary *library,
    const struct NativeModelView *model)
{
	if (model->id == -1)
		return NATIVE_ASSET_OK;
	if (model->id < 0 || model->id >= NATIVE_MODEL_LIBRARY_SLOTS)
		return NATIVE_ASSET_INVALID_DATA;
	library->slots[model->id].owner = model->map;
	library->slots[model->id].offset = model->offset;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeModelLibrary_StoreModel(struct NativeModelLibrary *library,
    const struct NativeModelView *model)
{
	struct NativeModelView checked;
	enum NativeAssetResult result;
	if (library == NULL || model == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	result = NativeModel_Open(model->map, model->offset, &checked);
	return result == NATIVE_ASSET_OK ? NativeModelLibrary_Insert(library, &checked) : result;
}

static enum NativeAssetResult NativeModelLibrary_Store(struct NativeModelLibrary *library,
    const struct NativeMpkView *mpk, const struct NativeLevelView *level)
{
	struct NativeModelLibrary pending;
	u32 count;
	if (library == NULL || (mpk == NULL && level == NULL) ||
	    (mpk != NULL && mpk->map == NULL) || (level != NULL && level->map == NULL))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	pending = *library;
	count = mpk != NULL ? mpk->modelCount : level->modelCount;
	for (u32 i = 0; i < count; i++)
	{
		struct NativeModelView model;
		enum NativeAssetResult result = mpk != NULL ? NativeMpk_GetModel(mpk, i, &model) : NativeLevel_GetModel(level, i, &model);
		if (result != NATIVE_ASSET_OK)
			return result;
		result = NativeModelLibrary_Insert(&pending, &model);
		if (result != NATIVE_ASSET_OK)
			return result;
	}
	*library = pending;
	return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeModelLibrary_StoreMpk(struct NativeModelLibrary *library, const struct NativeMpkView *mpk)
{
	if (mpk == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	return NativeModelLibrary_Store(library, mpk, NULL);
}

enum NativeAssetResult NativeModelLibrary_StoreLevel(struct NativeModelLibrary *library, const struct NativeLevelView *level)
{
	if (level == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	return NativeModelLibrary_Store(library, NULL, level);
}

enum NativeAssetResult NativeModelLibrary_Get(const struct NativeModelLibrary *library,
    s32 id, struct NativeModelView *out)
{
	enum NativeAssetResult result;
	if (out == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (library == NULL)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	if (id < 0 || id >= NATIVE_MODEL_LIBRARY_SLOTS)
		return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
	if (library->slots[id].owner == NULL)
		return NATIVE_ASSET_NOT_FOUND;
	result = NativeModel_Open(library->slots[id].owner, library->slots[id].offset, out);
	if (result == NATIVE_ASSET_OK && out->id != id)
	{
		memset(out, 0, sizeof(*out));
		return NATIVE_ASSET_INVALID_DATA;
	}
	return result;
}
