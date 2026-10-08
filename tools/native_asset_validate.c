#include <platform/native_asset_loading.h>
#include <platform/native_disc_image.h>
#include <platform/native_model_library.h>
#include <platform/native_model_animation.h>
#include <platform/native_model_vertices.h>
#include <platform/native_model_transform.h>
#include <platform/native_model_matrix.h>
#include <platform/native_model_projection.h>
#include <platform/native_model_commands.h>

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

static int Validator_Transforms(const struct NativeModelView *model, u32 headerIndex, size_t *total, size_t *matrixTotal, size_t *projectionTotal)
{
	u32 capacity, decoded;
	struct NativeModelHeaderView header;
	struct NativeModelVertex *current = NULL, *next = NULL;
	struct NativePackedModelVertex *packed = NULL;
	struct NativeModelMatrix probes[2];
	struct NativeProjectionConfig cameras[2] = {0};
	struct NativeProjectionState projectionStates[2] = {0};
	const struct NativeModelMatrix identity = {.m = {{4096,0,0},{0,4096,0},{0,0,4096}}};
	const s16 instanceScale[3] = {4096,4096,4096};
	int success = 0;
	enum NativeAssetResult status = NativeModel_GetVertexCount(model, headerIndex, &capacity);
	if (status == NATIVE_ASSET_NOT_FOUND) return 1;
	if (status != NATIVE_ASSET_OK) return 0;
	size_t records = capacity;
	if (records > SIZE_MAX / sizeof(*packed) || records > SIZE_MAX / sizeof(*current)) return 0;
	if (records != 0)
	{
		current = malloc(records * sizeof(*current)); next = malloc(records * sizeof(*next));
		packed = malloc(records * sizeof(*packed));
		if (current == NULL || next == NULL || packed == NULL) goto done;
	}
	if (NativeModel_GetHeader(model, headerIndex, &header) != NATIVE_ASSET_OK) goto done;
	// Synthetic identity rotation/view/instance scale, actual wire model scale.
	// These exercise near/far Q12 paths without inventing a game camera state.
	for (unsigned probe = 0; probe < 2; probe++)
	{
		struct NativeModelMatrix modelMatrix;
		status = NativeModelMatrix_Build(&identity, header.scale, instanceScale,
		    probe ? 4096 : 0, 0, &modelMatrix);
		if (status != NATIVE_ASSET_OK) goto done;
		status = NativeModelMatrix_Compose(&identity, &modelMatrix, &probes[probe]);
		if (status != NATIVE_ASSET_OK) goto done;
		cameras[probe].rotation = probes[probe]; cameras[probe].h = 256;
		cameras[probe].offset[0] = 160 * 65536; cameras[probe].offset[1] = 120 * 65536;
		const s32 position[3] = {0,0,4096}, origin[3] = {0};
		s32 rawDepth;
		status = NativeModelProjection_ViewTranslation(&identity, position, origin, 0, 0,
		    cameras[probe].translation, &rawDepth);
		if (status != NATIVE_ASSET_OK) goto done;
	}
	if (header.animationCount == 0)
	{
		status = NativeModel_PackStaticVertices(model, headerIndex, current, packed, capacity, &decoded);
		if (status == NATIVE_ASSET_NOT_FOUND) { success = 1; goto done; }
		if (status != NATIVE_ASSET_OK || decoded != capacity) goto done;
		for (u32 i = 0; i < decoded; i++)
		{
			s16 position[3];
			if (NativeModel_UnpackPosition(&packed[i], position) != NATIVE_ASSET_OK) goto done;
			for (unsigned probe = 0; probe < 2; probe++)
			{
				struct NativeMatrixVector transformed;
				if (NativeModelMatrix_Apply(&probes[probe], &packed[i], &transformed) != NATIVE_ASSET_OK) goto done;
				(*matrixTotal)++;
				struct NativeProjectionResult projected;
				if (NativeModelProjection_Project(&cameras[probe], &packed[i], &projectionStates[probe], &projected) != NATIVE_ASSET_OK) goto done;
				(*projectionTotal)++;
			}
		}
		*total += decoded;
	}
	else for (u32 a = 0; a < header.animationCount; a++)
	{
		struct NativeAnimationView animation;
		status = NativeModel_GetAnimation(model, headerIndex, a, &animation);
		if (status == NATIVE_ASSET_NOT_FOUND) continue;
		if (status != NATIVE_ASSET_OK) goto done;
		for (u32 f = 0; f < animation.logicalFrameCount; f++)
		{
			status = NativeModel_PackAnimationVertices(model, headerIndex, a, f, current, next, packed, capacity, &decoded);
			if (status != NATIVE_ASSET_OK || decoded != capacity) goto done;
			for (u32 i = 0; i < decoded; i++)
			{
				s16 position[3];
				if (NativeModel_UnpackPosition(&packed[i], position) != NATIVE_ASSET_OK) goto done;
				for (unsigned probe = 0; probe < 2; probe++)
				{
					struct NativeMatrixVector transformed;
					if (NativeModelMatrix_Apply(&probes[probe], &packed[i], &transformed) != NATIVE_ASSET_OK) goto done;
					(*matrixTotal)++;
					struct NativeProjectionResult projected;
					if (NativeModelProjection_Project(&cameras[probe], &packed[i], &projectionStates[probe], &projected) != NATIVE_ASSET_OK) goto done;
					(*projectionTotal)++;
				}
			}
			*total += decoded;
		}
	}
	success = 1;
done:
	if (!success) fprintf(stderr, "Invalid model local transform: %s, header %u\n", model->name, headerIndex);
	free(current); free(next); free(packed);
	return success;
}

static int Validator_Vertices(const struct NativeModelView *model, u32 headerIndex, size_t *total)
{
	u32 capacity, decoded;
	struct NativeModelHeaderView header;
	struct NativeModelVertex *vertices = NULL;
	int success = 0;
	enum NativeAssetResult status = NativeModel_GetVertexCount(model, headerIndex, &capacity);
	if (status == NATIVE_ASSET_NOT_FOUND) return 1;
	if (status != NATIVE_ASSET_OK) return 0;
	size_t records = capacity;
	if (records > SIZE_MAX / sizeof(*vertices)) return 0;
	if (records != 0 && (vertices = malloc(records * sizeof(*vertices))) == NULL) return 0;
	if (NativeModel_GetHeader(model, headerIndex, &header) != NATIVE_ASSET_OK) goto done;
	if (header.animationCount == 0)
	{
		status = NativeModel_DecodeStaticVertices(model, headerIndex, vertices, capacity, &decoded);
		if (status == NATIVE_ASSET_NOT_FOUND) { success = 1; goto done; }
		if (status != NATIVE_ASSET_OK) goto done;
		*total += decoded;
	}
	else
	{
		for (u32 a = 0; a < header.animationCount; a++)
		{
			struct NativeAnimationView animation;
			status = NativeModel_GetAnimation(model, headerIndex, a, &animation);
			if (status == NATIVE_ASSET_NOT_FOUND) continue;
			if (status != NATIVE_ASSET_OK) goto done;
			for (u32 f = 0; f < animation.storedFrameCount; f++)
			{
				status = NativeModel_DecodeAnimationVertices(model, headerIndex, a, f, vertices, capacity, &decoded);
				if (status != NATIVE_ASSET_OK)
				{
					fprintf(stderr, "Vertex decode failed: model %s, header %u, animation %u, frame %u (status %d)\n",
					    model->name, headerIndex, a, f, status);
					goto done;
				}
				*total += decoded;
			}
		}
	}
	success = 1;
done:
	if (!success) fprintf(stderr, "Invalid model vertex stream: %s, header %u\n", model->name, headerIndex);
	free(vertices);
	return success;
}

static int Validator_Models(const struct NativeMpkView *mpk, const struct NativeLevelView *level)
{
	struct NativeModelLibrary library;
	size_t animations = 0, frames = 0, staticFrames = 0, interpolated = 0;
	size_t decodedVertices = 0, transformedVertices = 0, matrixVertices = 0, projectedVertices = 0, triangles = 0, textured = 0;
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
		{
			if (!Validator_Vertices(&model, j, &decodedVertices)) return 0;
			if (!Validator_Transforms(&model, j, &transformedVertices, &matrixVertices, &projectedVertices)) return 0;
			struct NativeModelCommands commands;
			struct NativeModelTriangle triangle;
			enum NativeAssetResult commandStatus = NativeModelCommands_Open(&model, j, &commands);
			if (commandStatus != NATIVE_ASSET_OK && commandStatus != NATIVE_ASSET_NOT_FOUND) return 0;
			if (commandStatus == NATIVE_ASSET_OK)
			{
				while ((commandStatus = NativeModelCommands_Next(&commands, &triangle)) == NATIVE_ASSET_OK)
				{ triangles++; textured += triangle.textured != 0; }
				if (commandStatus != NATIVE_ASSET_NOT_FOUND)
				{
					fprintf(stderr, "Invalid draw commands: %s, header %u, cursor %zu (status %d)\n", model.name, j, commands.cursor, commandStatus);
					return 0;
				}
			}
			if (NativeModel_GetHeader(&model, j, &header) != NATIVE_ASSET_OK)
				return 0;
			if (header.animationCount == 0)
			{
				struct NativeFrameView frame;
				result = NativeModel_GetStaticFrame(&model, j, 0, &frame);
				if (result != NATIVE_ASSET_OK && result != NATIVE_ASSET_NOT_FOUND) return 0;
				if (result == NATIVE_ASSET_OK)
				{
					u32 delta;
					result = NativeModel_ReadStaticDeltaWord(&model, j, 0, &delta);
					if (result != NATIVE_ASSET_OK && result != NATIVE_ASSET_NOT_FOUND) return 0;
					staticFrames++;
				}
			}
			for (u32 k = 0; k < header.animationCount; k++)
			{
				struct NativeAnimationView animation;
				result = NativeModel_GetAnimation(&model, j, k, &animation);
				if (result == NATIVE_ASSET_NOT_FOUND) continue;
				if (result != NATIVE_ASSET_OK)
				{
					fprintf(stderr, "Invalid animation: model %u, header %u, animation %u\n", i, j, k);
					return 0;
				}
				for (u32 n = 0; n < animation.storedFrameCount; n++)
				{
					struct NativeFrameView frame;
					if (NativeAnimation_GetStoredFrame(&animation, n, &frame) != NATIVE_ASSET_OK)
					{
						fprintf(stderr, "Invalid animation frame: model %u, header %u, animation %u, frame %u\n", i, j, k, n);
						return 0;
					}
				}
				// Verify final clamped selection, including an interpolation endpoint.
				struct NativeFrameSelection selected;
				if (NativeAnimation_SelectFrame(&animation, UINT32_MAX, &selected) != NATIVE_ASSET_OK) return 0;
				animations++; frames += animation.storedFrameCount;
				interpolated += animation.interpolated != 0;
			}
		}
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
	printf("Animation data OK: %zu animations (%zu interpolated), %zu stored frames, %zu static frames\n",
	    animations, interpolated, frames, staticFrames);
	printf("Projection probes OK: %zu projected vertex visits (synthetic camera)\n", projectedVertices);
	printf("Matrix probes OK: %zu linear vertex applications (synthetic identity camera/instance, actual model scale)\n", matrixVertices);
	printf("Local transforms OK: %zu packed vertices across logical frames\n", transformedVertices);
	printf("Draw commands OK: %zu triangles (%zu textured)\n", triangles, textured);
	printf("Vertex streams OK: %zu decoded vertices\n", decodedVertices);
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
	printf("Validation includes local vertex packing/interpolation, triangles, source colors and texture metadata; Q12 matrices and synthetic camera projection included; actual camera integration, VRAM pixels, rendering and gameplay remain unverified.\n");
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
