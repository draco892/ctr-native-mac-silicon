#include <platform/native_model_vertices.h>

#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s at line %d\n", #c, __LINE__); return 1; } } while (0)

static void Put16(u8 *p, u16 n) { p[0] = (u8)n; p[1] = (u8)(n >> 8); }
static void PutBits(u8 *p, size_t *position, s32 value, unsigned bits)
{
	for (unsigned n = 0; n < bits; n++)
	{
		size_t b = (*position)++;
		u8 bit = (u8)(((u32)value >> (bits - n - 1)) & 1);
		p[(b / 32) * 4 + 3 - (b % 32) / 8] |= bit << (7 - b % 8);
	}
}

static int TestGoldenAndTruncation(void)
{
	u8 storage[9] = {0}; u8 *data = storage + 1;
	size_t bit = 0;
	struct NativeVertexDecoder decoder, before;
	struct NativeModelVertex vertex;
	PutBits(data, &bit, -1, 8); PutBits(data, &bit, 127, 8); PutBits(data, &bit, -128, 8);
	PutBits(data, &bit, 1, 2); PutBits(data, &bit, 1, 2); PutBits(data, &bit, -1, 2);
	PutBits(data, &bit, 5, 8); PutBits(data, &bit, -6, 8); PutBits(data, &bit, 7, 8);
	u32 relative = (1u << 25) | (1u << 17) | (255u << 9) | (1u << 6) | (1u << 3) | 1u;
	CHECK(NativeVertexDecoder_Init(data, 8, 1, &decoder) == NATIVE_ASSET_OK);
	CHECK(NativeVertexDecoder_Next(&decoder, 0x1ff, &vertex) == NATIVE_ASSET_OK);
	CHECK(vertex.x == 255 && vertex.y == 128 && vertex.z == 127);
	CHECK(NativeVertexDecoder_Next(&decoder, relative, &vertex) == NATIVE_ASSET_OK);
	CHECK(vertex.x == 2 && vertex.y == 126 && vertex.z == 129);
	CHECK(NativeVertexDecoder_Next(&decoder, 0x1ff, &vertex) == NATIVE_ASSET_OK);
	CHECK(vertex.x == 5 && vertex.y == 7 && vertex.z == 250 && decoder.bitPosition == 54);
	CHECK(NativeVertexDecoder_Init(data, 4, 1, &decoder) == NATIVE_ASSET_OK);
	CHECK(NativeVertexDecoder_Next(&decoder, 0x1ff, &vertex) == NATIVE_ASSET_OK);
	CHECK(NativeVertexDecoder_Next(&decoder, relative, &vertex) == NATIVE_ASSET_OK);
	before = decoder;
	CHECK(NativeVertexDecoder_Next(&decoder, 0x1ff, &vertex) == NATIVE_ASSET_INVALID_DATA);
	CHECK(memcmp(&before, &decoder, sizeof(decoder)) == 0 && vertex.x == 0 && vertex.y == 0 && vertex.z == 0);
	u8 raw[] = {1, 255, 128};
	CHECK(NativeVertexDecoder_Init(raw, 3, 0, &decoder) == NATIVE_ASSET_OK);
	CHECK(NativeVertexDecoder_Next(&decoder, 0, &vertex) == NATIVE_ASSET_OK && vertex.y == 255 && vertex.z == 128);
	before = decoder;
	CHECK(NativeVertexDecoder_Next(&decoder, 0, &vertex) == NATIVE_ASSET_INVALID_DATA && memcmp(&before, &decoder, sizeof(decoder)) == 0);
	CHECK(NativeVertexDecoder_Init(NULL, 0, 1, &decoder) == NATIVE_ASSET_OK);
	CHECK(NativeVertexDecoder_Next(&decoder, 0, &vertex) == NATIVE_ASSET_INVALID_DATA);
	CHECK(NativeVertexDecoder_Init(NULL, 1, 1, &decoder) == NATIVE_ASSET_INVALID_ARGUMENT);
	return 0;
}

// Independent bit-by-bit oracle: no packed shifts, no production helpers.
static int ReferenceBits(const u8 *data, size_t bytes, size_t *position, unsigned bits, int *value)
{
	unsigned n = 0;
	for (unsigned i = 0; i < bits; i++)
	{
		size_t b = *position + i;
		if (b / 32 >= bytes / 4) return 0;
		size_t address = b / 32 * 4 + 3 - b % 32 / 8;
		n = n * 2 + ((data[address] >> (7 - b % 8)) & 1u);
	}
	*position += bits;
	*value = n < (1u << (bits - 1)) ? (int)n : (int)n - (int)(1u << bits);
	return 1;
}

static int Signed(unsigned value, unsigned range) { return value < range / 2 ? (int)value : (int)value - (int)range; }

static int TestDifferential(void)
{
	u8 storage[65], *data = storage + 1;
	u32 random = 0x510e527f;
	for (u32 trial = 0; trial < 2000; trial++)
	{
		for (u32 i = 0; i < 64; i++) { random = random * 1664525u + 1013904223u; data[i] = (u8)(random >> 24); }
		size_t bytes = trial % 65, position = 0;
		int x = 0, y = 0, z = 0;
		struct NativeVertexDecoder decoder, before;
		struct NativeModelVertex vertex;
		CHECK(NativeVertexDecoder_Init(data, bytes, 1, &decoder) == NATIVE_ASSET_OK);
		for (u32 i = 0; i < 16; i++)
		{
			random = random * 1664525u + 1013904223u;
			unsigned widths[3] = {((random >> 6) & 7) + 1, ((random >> 3) & 7) + 1, (random & 7) + 1};
			int bases[3] = {Signed((random >> 25) & 127, 128) * 2, Signed((random >> 17) & 255, 256), Signed((random >> 9) & 255, 256)};
			int values[3]; size_t pending = position;
			int valid = ReferenceBits(data, bytes, &pending, widths[0], &values[0]) &&
			    ReferenceBits(data, bytes, &pending, widths[1], &values[1]) && ReferenceBits(data, bytes, &pending, widths[2], &values[2]);
			before = decoder;
			enum NativeAssetResult status = NativeVertexDecoder_Next(&decoder, random, &vertex);
			if (!valid) { CHECK(status == NATIVE_ASSET_INVALID_DATA && memcmp(&decoder, &before, sizeof(decoder)) == 0); break; }
			int *accumulators[] = {&x, &z, &y};
			for (u32 axis = 0; axis < 3; axis++)
			{
				int v = widths[axis] == 8 ? values[axis] : *accumulators[axis] + values[axis] + bases[axis];
				*accumulators[axis] = Signed((unsigned)v & 255, 256);
			}
			position = pending;
			CHECK(status == NATIVE_ASSET_OK && vertex.x == (u8)x && vertex.y == (u8)y && vertex.z == (u8)z);
			CHECK(decoder.bitPosition == position);
		}
	}
	return 0;
}

static int TestAssetIntegration(void)
{
	u8 storage[321], *a = storage + 1, ptr[32];
	const u32 slots[] = {20, 56, 60, 80, 168};
	struct NativePtrMapEntry entries[7];
	struct NativePtrMapView map;
	struct NativeModelView model;
	struct NativeModelVertex output[2];
	u32 count;
	memset(a, 0, 320);
	Put16(a + 18, 1); CTR_WriteU32LE(a + 20, 24);
	CTR_WriteU32LE(a + 56, 88); CTR_WriteU32LE(a + 60, 128);
	CTR_WriteU32LE(a + 76, 1); CTR_WriteU32LE(a + 80, 168); CTR_WriteU32LE(a + 168, 172);
	CTR_WriteU32LE(a + 88, 1);
	CTR_WriteU32LE(a + 92, 0x80000000); CTR_WriteU32LE(a + 96, 0x04000000);
	CTR_WriteU32LE(a + 100, 0x1234); CTR_WriteU32LE(a + 104, 0x00010000); CTR_WriteU32LE(a + 108, UINT32_MAX);
	CTR_WriteU32LE(a + 152, 28); memcpy(a + 156, "\1\2\3\376\377\0", 6);
	Put16(a + 188, 2); Put16(a + 190, 36);
	CTR_WriteU32LE(a + 220, 28); CTR_WriteU32LE(a + 256, 28);
	memcpy(a + 224, "\1\2\3\376\377\0", 6); memcpy(a + 260, "\4\5\6\7\10\11", 6);
	CTR_WriteU32LE(ptr, 20);
	for (u32 i = 0; i < 5; i++) CTR_WriteU32LE(ptr + 4 + i * 4, slots[i]);
	CHECK(NativePtrMap_Decode(a, 320, ptr, 24, entries, 5, &map) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&map, 0, &model) == NATIVE_ASSET_OK);
	CHECK(NativeModel_GetVertexCount(&model, 0, &count) == NATIVE_ASSET_OK && count == 2);
	CHECK(NativeModel_DecodeStaticVertices(&model, 0, output, 2, &count) == NATIVE_ASSET_OK && count == 2);
	CHECK(output[0].x == 1 && output[1].x == 254 && output[1].y == 255 && output[1].z == 0);
	CHECK(NativeModel_DecodeAnimationVertices(&model, 0, 0, 1, output, 2, &count) == NATIVE_ASSET_OK && output[0].x == 4 && output[1].z == 9);
	CHECK(NativeModel_DecodeAnimationVertices(&model, 0, 0, 2, output, 2, &count) == NATIVE_ASSET_INDEX_OUT_OF_RANGE && count == 0);
	CHECK(NativeModel_DecodeStaticVertices(&model, 0, output, 1, &count) == NATIVE_ASSET_OUTPUT_TOO_SMALL && count == 0);
	CHECK(NativeModel_DecodeStaticVertices(&model, 0, NULL, 2, &count) == NATIVE_ASSET_INVALID_ARGUMENT);
	// Convert both sources to compressed data, with two absolute XYZ triples.
	const u32 compressedSlots[] = {20, 56, 60, 72, 80, 168, 192};
	CTR_WriteU32LE(a + 72, 280); CTR_WriteU32LE(a + 192, 280);
	CTR_WriteU32LE(a + 280, 0x1ff); CTR_WriteU32LE(a + 284, 0x1ff);
	CTR_WriteU32LE(ptr, 28);
	for (u32 i = 0; i < 7; i++) CTR_WriteU32LE(ptr + 4 + i * 4, compressedSlots[i]);
	memset(a + 156, 0, 8);
	size_t bit = 0;
	PutBits(a + 156, &bit, -1, 8); PutBits(a + 156, &bit, 127, 8); PutBits(a + 156, &bit, -128, 8);
	PutBits(a + 156, &bit, 5, 8); PutBits(a + 156, &bit, -6, 8); PutBits(a + 156, &bit, 7, 8);
	memcpy(a + 224, a + 156, 8); memcpy(a + 260, a + 156, 8);
	CHECK(NativePtrMap_Decode(a, 320, ptr, 32, entries, 7, &map) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_DecodeStaticVertices(&model, 0, output, 2, &count) == NATIVE_ASSET_OK && count == 2);
	CHECK(output[0].x == 255 && output[0].y == 128 && output[0].z == 127);
	CHECK(output[1].x == 5 && output[1].y == 7 && output[1].z == 250);
	CHECK(NativeModel_DecodeAnimationVertices(&model, 0, 0, 1, output, 2, &count) == NATIVE_ASSET_OK && count == 2);
	CHECK(output[1].x == 5 && output[1].y == 7 && output[1].z == 250);
	Put16(a + 190, 32); // Only one complete stream word remains in this frame.
	CHECK(NativeModel_DecodeAnimationVertices(&model, 0, 0, 0, output, 2, &count) == NATIVE_ASSET_INVALID_DATA && count == 0);
	Put16(a + 190, 36);
	CTR_WriteU32LE(a + 72, 316); CTR_WriteU32LE(a + 192, 316);
	CTR_WriteU32LE(a + 316, 0x1ff); // First delta valid, second outside the asset.
	CHECK(NativePtrMap_Decode(a, 320, ptr, 32, entries, 7, &map) == NATIVE_PTRMAP_OK);
	CHECK(NativeModel_DecodeStaticVertices(&model, 0, output, 2, &count) == NATIVE_ASSET_INVALID_DATA && count == 0);
	CHECK(NativeModel_DecodeAnimationVertices(&model, 0, 0, 0, output, 2, &count) == NATIVE_ASSET_INVALID_DATA && count == 0);
	CTR_WriteU32LE(a + 108, 0); // No command terminator anywhere in this asset.
	CHECK(NativeModel_GetVertexCount(&model, 0, &count) == NATIVE_ASSET_INVALID_DATA && count == 0);
	return 0;
}

int main(void)
{
	if (TestGoldenAndTruncation() || TestDifferential() || TestAssetIntegration()) return 1;
	puts("Native model vertices: raw XYZ, compressed golden vectors, 2000 differential streams, command counts and bounded asset decoding passed.");
	return 0;
}
