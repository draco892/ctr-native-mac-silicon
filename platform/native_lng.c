#include <platform/native_lng.h>

#include <string.h>

enum NativeLngResult NativeLng_GetStorageLayout(size_t fileCapacity, struct NativeLngStorageLayout *layout)
{
	struct NativeLngStorageLayout result;
	size_t padding;
	size_t tableSize;

	if (layout == NULL || fileCapacity < NATIVE_LNG_HEADER_SIZE)
		return NATIVE_LNG_INVALID_ARGUMENT;

	result.fileCapacity = fileCapacity;
	result.stringCapacity = (fileCapacity - NATIVE_LNG_HEADER_SIZE) / NATIVE_LNG_OFFSET_SIZE;
	if (result.stringCapacity > NATIVE_LNG_MAX_STRINGS)
		result.stringCapacity = NATIVE_LNG_MAX_STRINGS;
	padding = (_Alignof(char *) - fileCapacity % _Alignof(char *)) % _Alignof(char *);
	if (fileCapacity > SIZE_MAX - padding)
		return NATIVE_LNG_STORAGE_TOO_LARGE;
	result.tableOffset = fileCapacity + padding;
	tableSize = result.stringCapacity * sizeof(char *);
	if (result.tableOffset > SIZE_MAX - tableSize)
		return NATIVE_LNG_STORAGE_TOO_LARGE;
	result.allocationSize = result.tableOffset + tableSize;
	*layout = result;
	return NATIVE_LNG_OK;
}

enum NativeLngResult NativeLng_Decode(void *file, size_t fileSize, char **strings, size_t stringCapacity, u32 *stringCount)
{
	u8 *bytes = file;
	u32 count;
	size_t tableStart;
	size_t tableEnd;

	if (stringCount == NULL)
		return NATIVE_LNG_INVALID_ARGUMENT;
	*stringCount = 0;
	if (file == NULL)
		return NATIVE_LNG_INVALID_ARGUMENT;
	if (fileSize < NATIVE_LNG_HEADER_SIZE)
		return NATIVE_LNG_TRUNCATED_HEADER;
	count = CTR_ReadU32LE(bytes);
	tableStart = CTR_ReadU32LE(bytes + 4);
	if (count > NATIVE_LNG_MAX_STRINGS)
		return NATIVE_LNG_INVALID_COUNT;
	if (tableStart < NATIVE_LNG_HEADER_SIZE || tableStart > fileSize ||
	    count > (fileSize - tableStart) / NATIVE_LNG_OFFSET_SIZE)
		return NATIVE_LNG_INVALID_TABLE;
	if (count > stringCapacity)
		return NATIVE_LNG_TABLE_TOO_SMALL;
	if (count != 0 && strings == NULL)
		return NATIVE_LNG_INVALID_ARGUMENT;
	tableEnd = tableStart + (size_t)count * NATIVE_LNG_OFFSET_SIZE;

	// Validate every string before publishing any pointer. Strings can precede
	// or follow the offset table, but a terminator in that table is not text.
	for (u32 i = 0; i < count; i++)
	{
		size_t offset = CTR_ReadU32LE(bytes + tableStart + (size_t)i * NATIVE_LNG_OFFSET_SIZE);
		size_t stringEnd;
		if (offset < NATIVE_LNG_HEADER_SIZE || offset >= fileSize ||
		    (offset >= tableStart && offset < tableEnd))
			return NATIVE_LNG_INVALID_STRING;
		stringEnd = offset < tableStart ? tableStart : fileSize;
		if (memchr(bytes + offset, 0, stringEnd - offset) == NULL)
			return NATIVE_LNG_INVALID_STRING;
	}
	for (u32 i = 0; i < count; i++)
	{
		u32 offset = CTR_ReadU32LE(bytes + tableStart + (size_t)i * NATIVE_LNG_OFFSET_SIZE);
		strings[i] = (char *)(bytes + offset);
	}
	*stringCount = count;
	return NATIVE_LNG_OK;
}

const char *NativeLng_ResultString(enum NativeLngResult result)
{
	switch (result)
	{
	case NATIVE_LNG_OK: return "success";
	case NATIVE_LNG_INVALID_ARGUMENT: return "invalid decoder argument or buffer capacity";
	case NATIVE_LNG_TRUNCATED_HEADER: return "truncated LNG header";
	case NATIVE_LNG_INVALID_COUNT: return "unsupported LNG string count";
	case NATIVE_LNG_INVALID_TABLE: return "LNG offset table outside file bounds";
	case NATIVE_LNG_INVALID_STRING: return "invalid or unterminated LNG string";
	case NATIVE_LNG_TABLE_TOO_SMALL: return "host string table too small";
	case NATIVE_LNG_STORAGE_TOO_LARGE: return "LNG storage size overflow";
	}
	return "unknown LNG error";
}
