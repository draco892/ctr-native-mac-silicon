#ifndef PLATFORM_NATIVE_ASSET_LOADING_H
#define PLATFORM_NATIVE_ASSET_LOADING_H

#include <platform/native_asset_readers.h>

struct NativeDramLayout
{
	u8 *payload;
	size_t payloadBytes;
	const u8 *ptr;
	size_t ptrBytes;
	size_t relocationCount;
};

enum NativeAssetLoadState
{
	NATIVE_ASSET_LOAD_EMPTY,
	NATIVE_ASSET_LOAD_WAITING_PTR,
	NATIVE_ASSET_LOAD_READY,
};

struct NativeAssetLoad
{
	enum NativeAssetLoadState state;
	u8 *payload;
	size_t payloadBytes;
	struct NativePtrMapView pointers;
};

// fileBytes is the BIGFILE entry length, not its sector-rounded allocation.
// Negative DRAM prefixes select external PTR; payload then excludes only the
// four-byte prefix. Embedded PTR is excluded from payloadBytes. Cleared on error.
enum NativePtrMapResult NativeAssetLoad_InspectDram(void *file, size_t fileBytes,
    struct NativeDramLayout *out);

// Completion adapters, independent of resident game layouts and callbacks in
// integers. Caller owns asset and decoded records; PTR itself may be released
// once decoding succeeds. CompleteDram starts a new load and clears old state
// even on error. BeginRaw is the first completion of a raw LEV + separate PTR.
enum NativePtrMapResult NativeAssetLoad_CompleteDram(void *file, size_t fileBytes,
    struct NativePtrMapEntry *entries, size_t capacity, struct NativeAssetLoad *out);
enum NativePtrMapResult NativeAssetLoad_BeginRaw(void *file, size_t fileBytes,
    struct NativeAssetLoad *out);
// Requires WAITING_PTR. Failure preserves that pending payload for retry and
// never publishes a ready map. Success rejects a second PTR completion.
enum NativePtrMapResult NativeAssetLoad_CompletePtr(struct NativeAssetLoad *load,
    const void *ptr, size_t ptrBytes, struct NativePtrMapEntry *entries, size_t capacity);
void NativeAssetLoad_Reset(struct NativeAssetLoad *load);

// Legacy native DRAM callback preflight: fully validate relocation records,
// using temporary heap storage which is released before in-place 32-bit patching.
// Does not publish a borrowed map or mutate the file. Returns zero on any error.
int NativeAssetLoad_CheckDram(void *file, size_t fileBytes, struct NativeDramLayout *out);

#endif
