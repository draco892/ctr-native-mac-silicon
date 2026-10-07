#include <platform/native_asset_loading.h>
#include <platform/native_disc_image.h>
#include <platform/native_model_library.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ValidatorInput { u8 *bytes; size_t size; };

static int Validator_ReadDisc(u32 index, struct ValidatorInput *out)
{
	struct NativeDiscImageFile big;
	u8 sector[0x800];
	memset(out, 0, sizeof(*out));
	if (!NativeDiscImage_FindFile("BIGFILE.BIG", &big) || big.size < 8 ||
	    !NativeDiscImage_ReadDataSectors(&big, 0, 1, sector))
		return 0;
	u32 count = CTR_ReadU32LE(sector + 4);
	u64 tableEnd = 8 + (u64)count * 8;
	u64 entryOffset = 8 + (u64)index * 8;
	if (index >= count || tableEnd > big.size ||
	    !NativeDiscImage_ReadDataSectors(&big, (u32)(entryOffset / 0x800), 1, sector))
		return 0;
	const u8 *entry = sector + entryOffset % 0x800;
	u32 firstSector = CTR_ReadU32LE(entry), bytes = CTR_ReadU32LE(entry + 4);
	u64 offset = (u64)firstSector * 0x800;
	if (bytes == 0 || bytes > INT32_MAX || firstSector > INT32_MAX ||
	    offset < tableEnd || offset > big.size || bytes > big.size - offset)
		return 0;
	u32 sectors = (bytes + 0x7ffu) / 0x800;
	u64 allocation = (u64)sectors * 0x800;
	if (allocation > SIZE_MAX)
		return 0;
	u8 *buffer = malloc((size_t)allocation);
	if (buffer == NULL)
		return 0;
	if (!NativeDiscImage_ReadDataSectors(&big, firstSector, sectors, buffer))
	{
		free(buffer);
		return 0;
	}
	out->bytes = buffer;
	out->size = bytes; // Never publish the last sector's padding as asset data.
	return 1;
}

// BIGFILE entries store a sector offset and exact byte size. Read just the
// requested entry, preserving its actual length rather than CD padding.
static int Validator_Read(const char *path, int indexed, u32 index, struct ValidatorInput *out)
{
	FILE *file = fopen(path, "rb");
	u8 header[8];
	long length;
	u64 offset = 0, bytes;
	u8 *buffer = NULL;
	int success = 0;
	memset(out, 0, sizeof(*out));
	if (file == NULL)
	{
		fprintf(stderr, "Cannot open %s\n", path);
		return 0;
	}
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0)
		goto done;
	bytes = (u64)length;
	if (indexed)
	{
		if (fseek(file, 0, SEEK_SET) != 0 || fread(header, 1, 8, file) != 8)
			goto done;
		u32 count = CTR_ReadU32LE(header + 4);
		u64 tableEnd = 8 + (u64)count * 8;
		if (index >= count || tableEnd > (u64)length ||
		    fseek(file, (long)(8 + (u64)index * 8), SEEK_SET) != 0 || fread(header, 1, 8, file) != 8)
			goto done;
		u32 sector = CTR_ReadU32LE(header), entryBytes = CTR_ReadU32LE(header + 4);
		if (sector > INT32_MAX || entryBytes > INT32_MAX)
			goto done;
		offset = (u64)sector * 0x800;
		bytes = entryBytes;
		if (offset < tableEnd || offset > (u64)length || bytes > (u64)length - offset)
			goto done;
	}
	if (bytes == 0 || bytes > UINT32_MAX || bytes > SIZE_MAX || offset > LONG_MAX)
		goto done;
	buffer = malloc((size_t)bytes);
	if (buffer == NULL || fseek(file, (long)offset, SEEK_SET) != 0 || fread(buffer, 1, (size_t)bytes, file) != (size_t)bytes)
		goto done;
	out->bytes = buffer;
	out->size = (size_t)bytes;
	buffer = NULL;
	success = 1;
done:
	free(buffer);
	fclose(file);
	if (!success)
		fprintf(stderr, "Invalid, truncated or unreadable input: %s\n", path);
	return success;
}

static int Validator_Index(const char *text, u32 *out)
{
	char *end;
	if (text[0] < '0' || text[0] > '9')
		return 0;
	errno = 0;
	unsigned long long value = strtoull(text, &end, 10);
	if (errno != 0 || *end != '\0' || value > UINT32_MAX)
		return 0;
	*out = (u32)value;
	return 1;
}

static int Validator_Models(const struct NativeMpkView *mpk, const struct NativeLevelView *level)
{
	struct NativeModelLibrary library;
	NativeModelLibrary_Reset(&library);
	enum NativeAssetResult stored = mpk != NULL ? NativeModelLibrary_StoreMpk(&library, mpk) :
	    NativeModelLibrary_StoreLevel(&library, level);
	if (stored != NATIVE_ASSET_OK)
	{
		fprintf(stderr, "Model library rejected invalid model data or ID.\n");
		return 0;
	}
	u32 count = mpk != NULL ? mpk->modelCount : level->modelCount;
	for (u32 i = 0; i < count; i++)
	{
		struct NativeModelView model;
		struct NativeModelHeaderView header;
		enum NativeAssetResult result = mpk != NULL ? NativeMpk_GetModel(mpk, i, &model) : NativeLevel_GetModel(level, i, &model);
		if (result != NATIVE_ASSET_OK)
		{
			fprintf(stderr, "Invalid model at index %u\n", i);
			return 0;
		}
		for (u32 j = 0; j < model.headerCount; j++)
			if (NativeModel_GetHeader(&model, j, &header) != NATIVE_ASSET_OK)
				return 0;
	}
	u32 registered = 0;
	for (s32 id = 0; id < NATIVE_MODEL_LIBRARY_SLOTS; id++)
	{
		struct NativeModelView model;
		enum NativeAssetResult result = NativeModelLibrary_Get(&library, id, &model);
		if (result == NATIVE_ASSET_NOT_FOUND) continue;
		if (result != NATIVE_ASSET_OK || model.id != id) return 0;
		registered++;
	}
	if (level != NULL)
		for (u32 i = 0; i < level->instanceCount; i++)
		{
			struct NativeInstanceDefView instance;
			if (NativeLevel_GetInstance(level, i, &instance) != NATIVE_ASSET_OK)
			{
				fprintf(stderr, "Invalid instance/model reference at index %u\n", i);
				return 0;
			}
		}
	printf("Model library OK: %u registered IDs; %u instance definitions decoded\n",
	    registered, level != NULL ? level->instanceCount : 0);
	return 1;
}

int main(int argc, char **argv)
{
	struct ValidatorInput asset = {0}, ptr = {0};
	struct NativeAssetLoad load = {0};
	struct NativePtrMapEntry *entries = NULL;
	struct NativeDramLayout layout;
	size_t count = 0;
	u32 assetIndex = 0, ptrIndex = 0;
	int indexed = 0, disc = 0, separate = 0, externalDram = 0, mpk = 0, result = 1;
	enum NativePtrMapResult status;
	if (argc < 3)
		goto usage;
	if (strcmp(argv[1], "mpk") == 0 && argc == 3) mpk = 1;
	else if (strcmp(argv[1], "lev-dram") == 0 && argc == 3) { }
	else if (strcmp(argv[1], "lev") == 0 && argc == 4) separate = 1;
	else if (strcmp(argv[1], "lev-external") == 0 && argc == 4) { separate = 1; externalDram = 1; }
	else if (strcmp(argv[1], "big-mpk") == 0 && argc == 4) { indexed = 1; mpk = 1; }
	else if (strcmp(argv[1], "big-lev") == 0 && argc == 4) indexed = 1;
	else if (strcmp(argv[1], "big-lev-ptr") == 0 && argc == 5) { indexed = 1; separate = 1; externalDram = 1; }
	else if (strcmp(argv[1], "disc-mpk") == 0 && argc == 4) { indexed = 1; disc = 1; mpk = 1; }
	else if (strcmp(argv[1], "disc-lev") == 0 && argc == 4) { indexed = 1; disc = 1; }
	else if (strcmp(argv[1], "disc-lev-ptr") == 0 && argc == 5) { indexed = 1; disc = 1; separate = 1; externalDram = 1; }
	else goto usage;
	if (indexed && (!Validator_Index(argv[3], &assetIndex) || (separate && !Validator_Index(argv[4], &ptrIndex))))
		goto usage;
	if (disc && !NativeDiscImage_Init(argv[2]))
	{
		fprintf(stderr, "Cannot read MODE2/2352 ctr-u.bin in %s\n", argv[2]);
		goto done;
	}
	if (!(disc ? Validator_ReadDisc(assetIndex, &asset) : Validator_Read(argv[2], indexed, assetIndex, &asset)))
	{
		if (disc) fprintf(stderr, "Cannot read BIGFILE asset index %u from disc.\n", assetIndex);
		goto done;
	}
	if (separate)
	{
		if (!(disc ? Validator_ReadDisc(ptrIndex, &ptr) : Validator_Read(indexed ? argv[2] : argv[3], indexed, ptrIndex, &ptr)))
		{
			if (disc) fprintf(stderr, "Cannot read BIGFILE PTR index %u from disc.\n", ptrIndex);
			goto done;
		}
		status = NativePtrMap_GetCount(ptr.bytes, ptr.size, &count);
	}
	else
	{
		status = NativeAssetLoad_InspectDram(asset.bytes, asset.size, &layout);
		if (status == NATIVE_PTRMAP_OK && layout.ptr == NULL)
		{
			fprintf(stderr, "This DRAM asset needs a separate PTR file.\n");
			goto done;
		}
		if (status == NATIVE_PTRMAP_OK) count = layout.relocationCount;
	}
	if (status != NATIVE_PTRMAP_OK || count > SIZE_MAX / sizeof(*entries))
		goto invalid;
	if (count != 0 && (entries = malloc(count * sizeof(*entries))) == NULL)
	{
		fprintf(stderr, "Cannot allocate relocation records.\n");
		goto done;
	}
	if (separate)
	{
		status = externalDram ? NativeAssetLoad_CompleteDram(asset.bytes, asset.size, entries, count, &load) :
		    NativeAssetLoad_BeginRaw(asset.bytes, asset.size, &load);
		if (status == NATIVE_PTRMAP_OK)
			status = NativeAssetLoad_CompletePtr(&load, ptr.bytes, ptr.size, entries, count);
	}
	else status = NativeAssetLoad_CompleteDram(asset.bytes, asset.size, entries, count, &load);
	if (status != NATIVE_PTRMAP_OK)
		goto invalid;
	if (mpk)
	{
		struct NativeMpkView view;
		if (NativeMpk_Open(&load.pointers, &view) != NATIVE_ASSET_OK || !Validator_Models(&view, NULL))
			goto invalid;
		printf("MPK OK: %u models, %zu payload bytes, %zu relocations, %zu-bit pointers\n",
		    view.modelCount, load.payloadBytes, load.pointers.count, sizeof(void *) * 8);
	}
	else
	{
		struct NativeLevelView view;
		struct NativeMeshView mesh;
		if (NativeLevel_Open(&load.pointers, &view) != NATIVE_ASSET_OK || !Validator_Models(NULL, &view) ||
		    NativeLevel_GetMesh(&view, &mesh) != NATIVE_ASSET_OK)
			goto invalid;
		printf("LEV OK: %u models, %u instances, %u quads, %u vertices, %u BSP nodes, %zu payload bytes, %zu relocations, %zu-bit pointers\n",
		    view.modelCount, view.instanceCount, mesh.quadCount, mesh.vertexCount, mesh.bspCount,
		    load.payloadBytes, load.pointers.count, sizeof(void *) * 8);
	}
	result = 0;
	printf("Validation covers roots, model headers and geometry spans; nested data and gameplay remain unverified.\n");
	goto done;
invalid:
	if (status != NATIVE_PTRMAP_OK)
		fprintf(stderr, "Asset relocation validation failed (status %d).\n", (int)status);
	else fprintf(stderr, "Asset root/model/mesh validation failed.\n");
	goto done;
usage:
	fprintf(stderr, "Usage:\n  ctr_native_asset_validate mpk FILE\n  ctr_native_asset_validate lev-dram FILE\n  ctr_native_asset_validate lev FILE PTR (unprefixed payload)\n  ctr_native_asset_validate lev-external FILE PTR (negative DRAM prefix)\n  ctr_native_asset_validate big-mpk BIGFILE INDEX\n  ctr_native_asset_validate big-lev BIGFILE INDEX\n  ctr_native_asset_validate big-lev-ptr BIGFILE LEV_INDEX PTR_INDEX\n  ctr_native_asset_validate disc-mpk ASSETS_DIR INDEX\n  ctr_native_asset_validate disc-lev ASSETS_DIR INDEX\n  ctr_native_asset_validate disc-lev-ptr ASSETS_DIR LEV_INDEX PTR_INDEX\n");
	result = 2;
done:
	NativeAssetLoad_Reset(&load);
	free(entries);
	free(ptr.bytes);
	free(asset.bytes);
	return result;
}
