#ifndef PLATFORM_NATIVE_LNG_H
#define PLATFORM_NATIVE_LNG_H

#include <macros.h>

enum NativeLngConstants
{
	NATIVE_LNG_HEADER_SIZE = 8,
	NATIVE_LNG_OFFSET_SIZE = 4,
	NATIVE_LNG_MAX_STRINGS = 4096,
};

// Binary fields are little-endian offsets, never host pointers. Decoding reads
// bytes explicitly and does not require an aligned file address.
struct NativeLngFileHeader
{
	u32 numStrings;
	u32 offsetToPtrArr;
};
CTR_STATIC_ASSERT(sizeof(struct NativeLngFileHeader) == NATIVE_LNG_HEADER_SIZE);
CTR_STATIC_ASSERT(offsetof(struct NativeLngFileHeader, offsetToPtrArr) == 4);

enum NativeLngResult
{
	NATIVE_LNG_OK,
	NATIVE_LNG_INVALID_ARGUMENT,
	NATIVE_LNG_TRUNCATED_HEADER,
	NATIVE_LNG_INVALID_COUNT,
	NATIVE_LNG_INVALID_TABLE,
	NATIVE_LNG_INVALID_STRING,
	NATIVE_LNG_TABLE_TOO_SMALL,
	NATIVE_LNG_STORAGE_TOO_LARGE,
};

struct NativeLngStorageLayout
{
	size_t fileCapacity;
	size_t stringCapacity;
	size_t tableOffset;
	size_t allocationSize;
};

// One caller-owned, pointer-aligned allocation holds the original file followed
// by a separate host table. Reuse it for reloads; its lifetime follows MEMPACK.
enum NativeLngResult NativeLng_GetStorageLayout(size_t fileCapacity, struct NativeLngStorageLayout *layout);

// fileSize is the actual asset length, excluding CD sector padding. The file
// and output table must not overlap. No allocation or mutation of file bytes.
// On failure, *stringCount is zero and the output table remains unchanged.
// On success, table entries borrow strings from file and live only as long as
// those bytes remain valid. Call again after reloading or moving the file.
enum NativeLngResult NativeLng_Decode(void *file, size_t fileSize, char **strings, size_t stringCapacity, u32 *stringCount);
const char *NativeLng_ResultString(enum NativeLngResult result);

#endif
