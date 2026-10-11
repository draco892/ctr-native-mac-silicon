#include <platform/native_lng.h>
#include <namespace_Mempack.h>
#include <platform.h>

#include <stdio.h>
#include <string.h>

static struct Mempack lngTestPools[4];
static struct Mempack *lngTestActive = &lngTestPools[0];

struct Mempack **Platform_GetActiveMempackSlot(void) { return &lngTestActive; }
struct Mempack *Platform_GetMempackPools(void) { return lngTestPools; }
void NativeCheckpoint_OnMempackArenaReset(void) {}
void CTR_ErrorScreen(u8 r, u8 g, u8 b) { (void)r; (void)g; (void)b; }

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "failed: %s at line %d\n", #condition, __LINE__); \
			return 1; \
		} \
	} while (0)

static void MakeHeader(u8 *file, u32 count, u32 tableOffset)
{
	CTR_WriteU32LE(file, count);
	CTR_WriteU32LE(file + 4, tableOffset);
}

static int TestValidFiles(void)
{
	u8 storage[34];
	u8 *file = storage + 1; // Header and offset table are intentionally unaligned.
	u8 original[32];
	char *strings[4];
	u32 count = 99;

	memset(storage, 0xcc, sizeof(storage));
	MakeHeader(file, 4, 8);
	CTR_WriteU32LE(file + 8, 24);
	CTR_WriteU32LE(file + 12, 24); // Shared strings are legal.
	CTR_WriteU32LE(file + 16, 26); // Shared suffixes are legal.
	CTR_WriteU32LE(file + 20, 31); // Empty string at the last file byte.
	memcpy(file + 24, "Hello\r\0\0", 8);
	memcpy(original, file, sizeof(original));
	CHECK(NativeLng_Decode(file, 32, strings, 4, &count) == NATIVE_LNG_OK);
	CHECK(count == 4 && strings[0] == (char *)file + 24);
	CHECK(strings[1] == strings[0]);
	CHECK(strcmp(strings[2], "llo\r") == 0);
	CHECK(strcmp(strings[3], "") == 0);
	CHECK(memcmp(original, file, sizeof(original)) == 0);
	CHECK(NativeLng_Decode(file, 32, strings, 4, &count) == NATIVE_LNG_OK);
	CHECK(memcmp(original, file, sizeof(original)) == 0); // No double-patching.

	// Retail-shaped arrangement: text precedes the offset table.
	MakeHeader(file, 2, 20);
	memcpy(file + 8, "Crash\0Coco\0", 11);
	CTR_WriteU32LE(file + 20, 8);
	CTR_WriteU32LE(file + 24, 14);
	CHECK(NativeLng_Decode(file, 28, strings, 4, &count) == NATIVE_LNG_OK);
	CHECK(count == 2 && strcmp(strings[0], "Crash") == 0 && strcmp(strings[1], "Coco") == 0);
	MakeHeader(file, 1, 8);
	CTR_WriteU32LE(file + 8, 12);
	file[12] = 0x82;
	file[13] = 0x99;
	file[14] = 0;
	CHECK(NativeLng_Decode(file, 15, strings, 4, &count) == NATIVE_LNG_OK);
	CHECK((u8)strings[0][0] == 0x82 && (u8)strings[0][1] == 0x99);

	MakeHeader(file, 0, 8);
	CHECK(NativeLng_Decode(file, 8, NULL, 0, &count) == NATIVE_LNG_OK);
	CHECK(count == 0);
	return 0;
}

static int TestMaximumCount(void)
{
	u8 file[NATIVE_LNG_HEADER_SIZE + NATIVE_LNG_MAX_STRINGS * NATIVE_LNG_OFFSET_SIZE + 1];
	char *strings[NATIVE_LNG_MAX_STRINGS];
	u32 count;
	MakeHeader(file, NATIVE_LNG_MAX_STRINGS, NATIVE_LNG_HEADER_SIZE);
	for (u32 i = 0; i < NATIVE_LNG_MAX_STRINGS; i++)
		CTR_WriteU32LE(file + NATIVE_LNG_HEADER_SIZE + i * NATIVE_LNG_OFFSET_SIZE, (u32)sizeof(file) - 1);
	file[sizeof(file) - 1] = 0;
	CHECK(NativeLng_Decode(file, sizeof(file), strings, len(strings), &count) == NATIVE_LNG_OK);
	CHECK(count == NATIVE_LNG_MAX_STRINGS);
	CHECK(strings[0] == (char *)file + sizeof(file) - 1);
	CHECK(strings[NATIVE_LNG_MAX_STRINGS - 1] == strings[0]);
	return 0;
}

static int TestInvalidFiles(void)
{
	u8 file[24];
	char marker[] = "unchanged";
	char *strings[2] = {marker, marker};
	u32 count = 99;

	memset(file, 0x55, sizeof(file));
	CHECK(NativeLng_Decode(NULL, 0, strings, 2, &count) == NATIVE_LNG_INVALID_ARGUMENT && count == 0);
	CHECK(NativeLng_Decode(file, 8, strings, 2, NULL) == NATIVE_LNG_INVALID_ARGUMENT);
	for (size_t size = 0; size < 8; size++)
		CHECK(NativeLng_Decode(file, size, strings, 2, &count) == NATIVE_LNG_TRUNCATED_HEADER && count == 0);
	MakeHeader(file, UINT32_MAX, 8);
	CHECK(NativeLng_Decode(file, sizeof(file), strings, 2, &count) == NATIVE_LNG_INVALID_COUNT);
	MakeHeader(file, NATIVE_LNG_MAX_STRINGS + 1, 8);
	CHECK(NativeLng_Decode(file, sizeof(file), strings, 2, &count) == NATIVE_LNG_INVALID_COUNT);
	MakeHeader(file, 2, UINT32_MAX);
	CHECK(NativeLng_Decode(file, sizeof(file), strings, 2, &count) == NATIVE_LNG_INVALID_TABLE);
	MakeHeader(file, 2, 4);
	CHECK(NativeLng_Decode(file, sizeof(file), strings, 2, &count) == NATIVE_LNG_INVALID_TABLE);
	MakeHeader(file, 2, 20);
	CHECK(NativeLng_Decode(file, sizeof(file), strings, 2, &count) == NATIVE_LNG_INVALID_TABLE);
	MakeHeader(file, 2, 8);
	CTR_WriteU32LE(file + 8, 16);
	CTR_WriteU32LE(file + 12, 22);
	memcpy(file + 16, "valid\0x\0", 8);
	CHECK(NativeLng_Decode(file, sizeof(file), strings, 1, &count) == NATIVE_LNG_TABLE_TOO_SMALL);
	CHECK(NativeLng_Decode(file, sizeof(file), NULL, 2, &count) == NATIVE_LNG_INVALID_ARGUMENT);

	// A bad second entry must not publish the already-valid first pointer.
	const u32 badOffsets[] = {0, 7, 8, 12, 15, 24, UINT32_MAX};
	for (size_t i = 0; i < len(badOffsets); i++)
	{
		CTR_WriteU32LE(file + 12, badOffsets[i]);
		count = 99;
		CHECK(NativeLng_Decode(file, sizeof(file), strings, 2, &count) == NATIVE_LNG_INVALID_STRING);
		CHECK(count == 0 && strings[0] == marker && strings[1] == marker);
	}
	CTR_WriteU32LE(file + 12, 22);
	file[23] = 'x';
	CHECK(NativeLng_Decode(file, sizeof(file), strings, 2, &count) == NATIVE_LNG_INVALID_STRING);
	CHECK(strings[0] == marker && strings[1] == marker);

	// Zeros inside the offset table cannot terminate text preceding the table.
	MakeHeader(file, 1, 16);
	memset(file + 8, 'a', 8);
	CTR_WriteU32LE(file + 16, 8);
	CHECK(NativeLng_Decode(file, sizeof(file), strings, 2, &count) == NATIVE_LNG_INVALID_STRING);

	// A terminator in sector padding cannot complete a truncated asset.
	MakeHeader(file, 1, 8);
	CTR_WriteU32LE(file + 8, 12);
	memcpy(file + 12, "end\0", 4);
	CHECK(NativeLng_Decode(file, 15, strings, 2, &count) == NATIVE_LNG_INVALID_STRING);
	CHECK(NativeLng_Decode(file, 16, strings, 2, &count) == NATIVE_LNG_OK);
	return 0;
}

static int TestStorageAndReload(void)
{
	struct NativeLngStorageLayout layout;
	u8 *file;
	char **strings;
	u32 count;
	s32 freeBytes;
	s32 bookmark;

	CHECK(NativeLng_GetStorageLayout(7, &layout) == NATIVE_LNG_INVALID_ARGUMENT);
	CHECK(NativeLng_GetStorageLayout(32, NULL) == NATIVE_LNG_INVALID_ARGUMENT);
	CHECK(NativeLng_GetStorageLayout(SIZE_MAX, &layout) == NATIVE_LNG_STORAGE_TOO_LARGE);
	CHECK(NativeLng_GetStorageLayout(SIZE_MAX - 7, &layout) == NATIVE_LNG_STORAGE_TOO_LARGE);
	CHECK(NativeLng_GetStorageLayout(33, &layout) == NATIVE_LNG_OK);
	CHECK(layout.tableOffset >= 33 && layout.tableOffset % _Alignof(char *) == 0);
	CHECK(layout.allocationSize == layout.tableOffset + layout.stringCapacity * sizeof(char *));
	CHECK(NativeLng_GetStorageLayout(0x4000, &layout) == NATIVE_LNG_OK);
	CHECK(layout.fileCapacity == 0x4000 && layout.tableOffset == 0x4000);
	CHECK(layout.stringCapacity <= NATIVE_LNG_MAX_STRINGS);
	MEMPACK_Init(0x200000);
	bookmark = MEMPACK_PushState();
	file = MEMPACK_AllocMem((s32)layout.allocationSize, "LNG storage");
	strings = (char **)(file + layout.tableOffset);
	freeBytes = MEMPACK_GetFreeBytes();
#if defined(__APPLE__) && defined(__aarch64__)
	CHECK((uintptr_t)file > UINT32_MAX && (uintptr_t)strings > UINT32_MAX);
#endif
	CHECK((uintptr_t)strings % _Alignof(char *) == 0);
	for (int reload = 0; reload < 100; reload++)
	{
		// Simulate the CD reader's full padded sector, then decode actual length.
		memset(file, 0, 0x800);
		memset(strings, 0, layout.stringCapacity * sizeof(*strings));
		MakeHeader(file, 2, 8);
		CTR_WriteU32LE(file + 8, 16);
		CTR_WriteU32LE(file + 12, 22);
		memcpy(file + 16, reload % 2 == 0 ? "Crash\0Coco\0" : "Pista\0Gara\0", 11);
		CHECK(NativeLng_Decode(file, 27, strings, layout.stringCapacity, &count) == NATIVE_LNG_OK);
		CHECK(count == 2 && strcmp(strings[0], reload % 2 == 0 ? "Crash" : "Pista") == 0);
		CHECK(strings[2] == NULL);
		CHECK(CTR_ReadU32LE(file + 8) == 16); // Disk offsets remain untouched.
		CHECK(MEMPACK_GetFreeBytes() == freeBytes);
	}
	MEMPACK_PopToState(bookmark);
	CHECK(MEMPACK_GetFreeBytes() == Platform_GetMempackArena()->size);
	// Storage is caller-owned: discard borrowed pointers on release/reset.
	file = NULL;
	strings = NULL;
	MEMPACK_Init(0x200000);
	file = MEMPACK_AllocMem((s32)layout.allocationSize, "LNG storage after reset");
	strings = (char **)(file + layout.tableOffset);
	MakeHeader(file, 1, 8);
	CTR_WriteU32LE(file + 8, 12);
	memcpy(file + 12, "reset\0", 6);
	CHECK(NativeLng_Decode(file, 18, strings, layout.stringCapacity, &count) == NATIVE_LNG_OK);
	CHECK(count == 1 && strcmp(strings[0], "reset") == 0);
	return 0;
}

static int TestMalformedCorpus(void)
{
	u8 file[128];
	char *strings[8];
	u32 count;
	u32 random = 0x12345678;
	for (int trial = 0; trial < 2000; trial++)
	{
		for (size_t i = 0; i < sizeof(file); i++)
		{
			random = random * 1664525u + 1013904223u;
			file[i] = (u8)(random >> 24);
		}
		MakeHeader(file, (random >> 8) % 9, random % 160);
		enum NativeLngResult result = NativeLng_Decode(file, sizeof(file), strings, len(strings), &count);
		if (result == NATIVE_LNG_OK)
		{
			CHECK(count <= len(strings));
			for (u32 i = 0; i < count; i++)
			{
				CHECK(strings[i] >= (char *)file + 8 && strings[i] < (char *)file + sizeof(file));
				CHECK(memchr(strings[i], 0, sizeof(file) - (size_t)(strings[i] - (char *)file)) != NULL);
			}
		}
		else
			CHECK(count == 0);
	}
	return 0;
}

int main(void)
{
	CHECK(TestValidFiles() == 0);
	CHECK(TestMaximumCount() == 0);
	CHECK(TestInvalidFiles() == 0);
	CHECK(TestStorageAndReload() == 0);
	CHECK(TestMalformedCorpus() == 0);
	puts("LNG tests passed: binary offsets, host pointers, malformed files and reloads");
	return 0;
}
