#include <platform/native_asset_loading.h>

#include <stdlib.h>
#include <string.h>

void NativeAssetLoad_Reset(struct NativeAssetLoad *load)
{
	if (load != NULL)
		memset(load, 0, sizeof(*load));
}

enum NativePtrMapResult NativeAssetLoad_InspectDram(void *file, size_t fileBytes,
    struct NativeDramLayout *out)
{
	struct NativeDramLayout result = {0};
	u32 offset;
	enum NativePtrMapResult status;
	if (out == NULL)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (file == NULL || (uintptr_t)file > UINTPTR_MAX - fileBytes)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	if (fileBytes < 4 || fileBytes - 4 > UINT32_MAX)
		return NATIVE_PTRMAP_INVALID_MAP;
	offset = CTR_ReadU32LE(file);
	result.payload = (u8 *)file + 4;
	if (offset > INT32_MAX)
	{
		if (fileBytes == 4)
			return NATIVE_PTRMAP_INVALID_MAP;
		result.payloadBytes = fileBytes - 4;
	}
	else
	{
		if (fileBytes < 8 || offset > fileBytes - 8)
			return NATIVE_PTRMAP_INVALID_MAP;
		result.payloadBytes = offset;
		result.ptr = result.payload + offset;
		result.ptrBytes = fileBytes - 4 - offset;
		status = NativePtrMap_GetCount(result.ptr, result.ptrBytes, &result.relocationCount);
		if (status != NATIVE_PTRMAP_OK)
			return status;
	}
	*out = result;
	return NATIVE_PTRMAP_OK;
}

enum NativePtrMapResult NativeAssetLoad_BeginRaw(void *file, size_t fileBytes,
    struct NativeAssetLoad *out)
{
	if (out == NULL)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	NativeAssetLoad_Reset(out);
	if (file == NULL || fileBytes == 0 || fileBytes > UINT32_MAX ||
	    (uintptr_t)file > UINTPTR_MAX - fileBytes)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	out->payload = file;
	out->payloadBytes = fileBytes;
	out->state = NATIVE_ASSET_LOAD_WAITING_PTR;
	return NATIVE_PTRMAP_OK;
}

enum NativePtrMapResult NativeAssetLoad_CompleteDram(void *file, size_t fileBytes,
    struct NativePtrMapEntry *entries, size_t capacity, struct NativeAssetLoad *out)
{
	struct NativeDramLayout layout;
	enum NativePtrMapResult status;
	if (out == NULL)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	NativeAssetLoad_Reset(out);
	status = NativeAssetLoad_InspectDram(file, fileBytes, &layout);
	if (status != NATIVE_PTRMAP_OK)
		return status;
	if (layout.ptr == NULL)
		return NativeAssetLoad_BeginRaw(layout.payload, layout.payloadBytes, out);
	status = NativePtrMap_Decode(layout.payload, layout.payloadBytes, layout.ptr,
	    layout.ptrBytes, entries, capacity, &out->pointers);
	if (status != NATIVE_PTRMAP_OK)
		return status;
	out->payload = layout.payload;
	out->payloadBytes = layout.payloadBytes;
	out->state = NATIVE_ASSET_LOAD_READY;
	return NATIVE_PTRMAP_OK;
}

enum NativePtrMapResult NativeAssetLoad_CompletePtr(struct NativeAssetLoad *load,
    const void *ptr, size_t ptrBytes, struct NativePtrMapEntry *entries, size_t capacity)
{
	enum NativePtrMapResult status;
	if (load == NULL || load->state != NATIVE_ASSET_LOAD_WAITING_PTR)
		return NATIVE_PTRMAP_INVALID_ARGUMENT;
	status = NativePtrMap_Decode(load->payload, load->payloadBytes, ptr, ptrBytes,
	    entries, capacity, &load->pointers);
	if (status == NATIVE_PTRMAP_OK)
		load->state = NATIVE_ASSET_LOAD_READY;
	return status;
}

int NativeAssetLoad_CheckDram(void *file, size_t fileBytes, struct NativeDramLayout *out)
{
	struct NativeDramLayout layout;
	struct NativePtrMapEntry *entries = NULL;
	struct NativeAssetLoad load;
	enum NativePtrMapResult status;
	if (out == NULL)
		return 0;
	memset(out, 0, sizeof(*out));
	status = NativeAssetLoad_InspectDram(file, fileBytes, &layout);
	if (status != NATIVE_PTRMAP_OK || layout.relocationCount > SIZE_MAX / sizeof(*entries))
		return 0;
	if (layout.relocationCount != 0)
	{
		entries = malloc(layout.relocationCount * sizeof(*entries));
		if (entries == NULL)
			return 0;
	}
	status = NativeAssetLoad_CompleteDram(file, fileBytes, entries, layout.relocationCount, &load);
	free(entries);
	if (status != NATIVE_PTRMAP_OK)
		return 0;
	*out = layout;
	return 1;
}
