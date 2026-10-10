#include <platform/native_asset_loading.h>
#include <platform/native_disc_image.h>
#include <platform/native_model_library.h>
#include <platform/native_model_animation.h>
#include <platform/native_model_vertices.h>
#include <platform/native_model_transform.h>
#include <platform/native_model_matrix.h>
#include <platform/native_model_projection.h>
#include <platform/native_model_commands.h>
#include <platform/native_vram.h>
#include <platform/native_model_draw.h>
#include <platform/native_raster.h>
#include <platform/native_instance_transform.h>
#include <platform/native_mesh_geometry.h>
#include <platform/native_terrain_material.h>
#include <platform/native_scene_render.h>
#include <platform/native_vertex_animation.h>
#include <platform/native_resident_graph.h>

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

struct ValidatorDrawStats { size_t triangles, pixels, flagged, degenerate; };
static int Validator_Draw(const struct NativeModelView *model,u32 headerIndex,
    const struct NativeVramView *vram,const struct NativeInstanceDefView *instance,struct ValidatorDrawStats *stats)
{
	struct NativeModelHeaderView header;
	struct NativeModelDrawWorkspace workspace={0};
	struct NativeModelDraw draw;
	struct NativeDrawTriangle triangle;
	u32 vertices,animationIndex=UINT32_MAX,logicalIndex=0;
	int success=0;
	enum NativeAssetResult status=NativeModel_GetVertexCount(model,headerIndex,&vertices);
	if(status==NATIVE_ASSET_NOT_FOUND) return 1;
	if(status!=NATIVE_ASSET_OK || NativeModel_GetHeader(model,headerIndex,&header)!=NATIVE_ASSET_OK) return 0;
	if(header.animationCount!=0)
	{
		for(u32 i=0;i<header.animationCount;i++)
		{
			struct NativeAnimationView animation;
			status=NativeModel_GetAnimation(model,headerIndex,i,&animation);
			if(status==NATIVE_ASSET_NOT_FOUND) continue;
			if(status!=NATIVE_ASSET_OK) return 0;
			animationIndex=i;
			logicalIndex=animation.interpolated && animation.logicalFrameCount>1 ? 1 : 0;
			break;
		}
		if(animationIndex==UINT32_MAX) return 1;
	}
	size_t records=vertices;
	if(records>SIZE_MAX/sizeof(*workspace.packed) || records>SIZE_MAX/sizeof(*workspace.current)) return 0;
	if(records!=0)
	{
		workspace.current=malloc(records*sizeof(*workspace.current));
		workspace.next=malloc(records*sizeof(*workspace.next));
		workspace.packed=malloc(records*sizeof(*workspace.packed));
		if(workspace.current==NULL || workspace.next==NULL || workspace.packed==NULL) goto done;
	}
	workspace.capacity=records;
	const struct NativeModelMatrix identity={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};
	const s16 instanceScale[3]={4096,4096,4096};
	struct NativeProjectionConfig projection={.translation={0,0,4096},.offset={160*65536,120*65536},.h=256};
	if(instance!=NULL) {
		const struct NativeInstanceCamera camera={.view=identity,.position={0,0,-4096},.offset={160*65536,120*65536},.h=256}; s32 rawDepth;
		status=NativeInstance_Projection(instance,headerIndex,&camera,&projection,&rawDepth);
	} else status=NativeModelMatrix_Build(&identity,header.scale,instanceScale,4096,0,&projection.rotation);
	if(status!=NATIVE_ASSET_OK) goto done;
	status=NativeModelDraw_Open(model,headerIndex,animationIndex,logicalIndex,&projection,&workspace,vram,&draw);
	if(status==NATIVE_ASSET_NOT_FOUND) { success=1; goto done; }
	if(status!=NATIVE_ASSET_OK) goto done;
	while((status=NativeModelDraw_Next(&draw,&triangle))==NATIVE_ASSET_OK)
	{
		stats->triangles++;
		stats->pixels+=triangle.hasCornerPixels ? 3 : 0;
		stats->flagged+=triangle.projectionFlags!=0;
		stats->degenerate+=triangle.signedArea==0;
	}
	if(status!=NATIVE_ASSET_NOT_FOUND) goto done;
	success=1;
done:
	if(!success) fprintf(stderr,"Invalid connected draw pipeline: %s header %u (status %d)\n",model->name,headerIndex,status);
	free(workspace.current); free(workspace.next); free(workspace.packed);
	return success;
}

static int Validator_VertexAnimation(const struct NativeLevelView *level,const struct NativeMeshView *mesh)
{
    struct NativeVertexAnimationView animation;
    enum NativeAssetResult status=NativeVertexAnimation_Open(level,mesh,&animation);
    if(status!=NATIVE_ASSET_OK) { fprintf(stderr,"Invalid water/scenery animation metadata: %d\n",status); return 0; }
    if(!animation.count) { printf("Vertex animation OK: empty list\n"); return 1; }
    size_t bytes=(size_t)mesh->vertexCount*NATIVE_VERTEX_BYTES;
    u8 *buffer=malloc(bytes); if(buffer==NULL) return 0;
    const u32 ticks[]={0,7,8,223}; struct NativeMeshView animated;
    for(unsigned i=0;i<sizeof(ticks)/sizeof(*ticks);i++) {
        status=NativeVertexAnimation_Apply(&animation,ticks[i],NULL,NULL,0,buffer,bytes,&animated);
        if(status!=NATIVE_ASSET_OK) break;
    }
    free(buffer);
    if(status!=NATIVE_ASSET_OK) { fprintf(stderr,"Invalid water/scenery animation at runtime: %d\n",status); return 0; }
    printf("Vertex animation OK: %u %s records, 4 sample ticks, immutable source\n",animation.count,animation.scenery ? "SCVert" : "WaterVert");
    return 1;
}

// Materialize actual resident headers/definitions as a differential check.
// Nested model-header objects are allocated placeholders, never published to
// the game or dereferenced here; full graph loading remains a separate step.
static int Validator_ResidentInstance(const struct NativeLevelView *level, u32 index, const struct NativeInstanceDefView *view)
{
	struct Model model;
	struct InstDef definition;
	struct ModelHeader *headers = calloc(view->model.headerCount ? view->model.headerCount : 1, sizeof(*headers));
	if (headers == NULL)
		return 0;
	struct NativeResidentBinding bindings[] = {{NR_MODEL, view->model.offset, 1, &model},
	                                           {NR_MODEL_HEADER, view->model.headersOffset, view->model.headerCount, headers}};
	struct NativeResidentContext context = {.map = level->map, .bindings = bindings, .count = 2};
	int ok = NativeResident_DecodeModel(&context, view->model.offset, &model) == NATIVE_ASSET_OK &&
	         NativeResident_DecodeInstDef(&context, level->instancesOffset + index * NATIVE_INSTANCE_DEF_BYTES, &definition) == NATIVE_ASSET_OK;
	if (ok)
		ok = definition.model == &model && model.id == view->model.id && (u16)model.numHeaders == view->model.headerCount &&
		     (view->model.headerCount == 0 || model.headers == headers) && memcmp(model.name, view->model.name, sizeof(model.name)) == 0 &&
		     memcmp(definition.name, view->name, sizeof(definition.name)) == 0 && memcmp(&definition.scale, view->scale, sizeof(definition.scale)) == 0 &&
		     memcmp(&definition.pos, view->position, sizeof(definition.pos)) == 0 && memcmp(&definition.rot, view->rotation, sizeof(definition.rot)) == 0 &&
		     definition.colorRGBA == view->colorRGBA && definition.flags == view->flags && definition.unk24 == view->unk24 && definition.unk28 == view->unk28 &&
		     definition.modelID == view->modelID && definition.ptrInstance == NULL;
	free(headers);
	return ok;
}

static int Validator_ResidentGraph(const struct NativeLevelView *level, const struct NativeMpkView *mpk)
{
	struct NativeResidentGraph *graph = NULL;
	struct NativeResidentGraphError error = {0};
	enum NativeAssetResult result = NativeResidentGraph_Build(level ? level->map : mpk->map, level ? NR_LEVEL : NR_MPK, 0, &graph, &error);
	if (result != NATIVE_ASSET_OK)
	{
		fprintf(stderr, "Resident graph failed: kind %u offset 0x%x count %zu result %u\n", error.kind, error.offset, error.count, result);
		return 0;
	}
	struct Level *resident = NativeResidentGraph_Root(graph);
	size_t checkpointBytes = NativeResidentGraph_CheckpointSize(graph);
	void *checkpoint = malloc(checkpointBytes);
	struct NativeResidentGraph *restored = NULL;
	if (!checkpoint || !NativeResidentGraph_CaptureCheckpoint(graph, checkpoint, checkpointBytes) ||
	    !NativeResidentGraph_RestoreCheckpoint(checkpoint, checkpointBytes, &restored, NULL, NULL))
	{
		fprintf(stderr, "Resident graph checkpoint failed\n");
		free(checkpoint);
		NativeResidentGraph_Free(graph);
		return 0;
	}
	int valid = NativeResidentGraph_Root(restored) != NativeResidentGraph_Root(graph);
	if (level)
	{
		struct Level *copy = NativeResidentGraph_Root(restored);
		valid = valid && copy->numModels == resident->numModels && copy->numInstances == resident->numInstances;
		if (resident->numInstances)
			valid = valid && copy->ptrInstDefs != resident->ptrInstDefs && copy->ptrInstDefs[0].model != resident->ptrInstDefs[0].model;
	}
	printf("Resident graph OK: %zu objects, checkpoint %zu bytes, rebased references %s\n", NativeResidentGraph_ObjectCount(graph), checkpointBytes,
	       valid ? "OK" : "FAILED");
	free(checkpoint);
	NativeResidentGraph_Free(restored);
	if (!valid)
	{
		NativeResidentGraph_Free(graph);
		return 0;
	}
	NativeResidentGraph_Free(graph);
	return 1;
}

static int Validator_Models(const struct NativeMpkView *mpk, const struct NativeLevelView *level, const struct NativeVramView *vram)
{
	if (!Validator_ResidentGraph(level, mpk))
		return 0;
	struct NativeModelLibrary library;
	struct ValidatorDrawStats drawStats={0},instanceStats={0};
	size_t animations = 0, frames = 0, staticFrames = 0, interpolated = 0;
	size_t decodedVertices = 0, transformedVertices = 0, matrixVertices = 0, projectedVertices = 0, triangles = 0, textured = 0, pixels = 0;
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
			if (!Validator_Draw(&model,j,vram,NULL,&drawStats)) return 0;
			if (!Validator_Transforms(&model, j, &transformedVertices, &matrixVertices, &projectedVertices)) return 0;
			struct NativeModelCommands commands;
			struct NativeModelTriangle triangle;
			enum NativeAssetResult commandStatus = NativeModelCommands_Open(&model, j, &commands);
			if (commandStatus != NATIVE_ASSET_OK && commandStatus != NATIVE_ASSET_NOT_FOUND) return 0;
			if (commandStatus == NATIVE_ASSET_OK)
			{
				while ((commandStatus = NativeModelCommands_Next(&commands, &triangle)) == NATIVE_ASSET_OK)
				{
					triangles++; textured += triangle.textured != 0;
					if (vram != NULL && triangle.textured) for (unsigned corner = 0; corner < 3; corner++)
					{
						struct NativeTexturePixel pixel;
						if (NativeVram_Sample(vram, triangle.texture.tpage, triangle.texture.clut,
						    triangle.texture.u[corner], triangle.texture.v[corner], &pixel) != NATIVE_ASSET_OK)
						{
							fprintf(stderr, "Invalid texture pixel address: %s header %u\n", model.name, j);
							return 0;
						}
						pixels++;
					}
				}
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
			if (!Validator_ResidentInstance(level, i, &instance))
			{
				fprintf(stderr, "Resident instance conversion failed at index %u\n", i);
				return 0;
			}
			for (u32 h = 0; h < instance.model.headerCount; h++)
				if(!Validator_Draw(&instance.model,h,vram,&instance,&instanceStats)) return 0;
		}
	if (level != NULL)
		printf("Resident instance bridge OK: %u model/definition records (nested graph not published)\n", level->instanceCount);
	if(level!=NULL) printf("Instance draw OK: %u definitions, %zu projected triangles, %zu flagged, %zu degenerate (authored transforms, synthetic camera)\n",level->instanceCount,instanceStats.triangles,instanceStats.flagged,instanceStats.degenerate);
	printf("Model library OK: %u registered IDs; %u instance definitions decoded\n",
	    registered, level != NULL ? level->instanceCount : 0);
	printf("Animation data OK: %zu animations (%zu interpolated), %zu stored frames, %zu static frames\n",
	    animations, interpolated, frames, staticFrames);
	printf("Projection probes OK: %zu projected vertex visits (synthetic camera)\n", projectedVertices);
	printf("Matrix probes OK: %zu linear vertex applications (synthetic identity camera/instance, actual model scale)\n", matrixVertices);
	printf("Local transforms OK: %zu packed vertices across logical frames\n", transformedVertices);
	if (vram != NULL) printf("Texture pixels OK: %zu sampled triangle corners (unloaded VRAM starts zero)\n", pixels);
	printf("Draw pipeline OK: %zu projected triangles, %zu corner pixels, %zu flagged, %zu degenerate (one frame/header, synthetic camera, no culling)\n",
	    drawStats.triangles,drawStats.pixels,drawStats.flagged,drawStats.degenerate);
	printf("Draw commands OK: %zu triangles (%zu textured)\n", triangles, textured);
	printf("Vertex streams OK: %zu decoded vertices\n", decodedVertices);
	return 1;
}

// Ordered uploads mirror shared VRAM followed by level VRAM; later rectangles
// overwrite earlier ones. Limit CLI stacks without modifying source assets.
static int Validator_VramIndices(const char *text,u32 indices[16],size_t *count)
{
	*count=0;
	while(*text)
	{
		if(*count==16) return 0;
		u32 value=0; unsigned digits=0;
		while(*text>='0' && *text<='9')
		{
			u32 digit=(u32)(*text++-'0');
			if(value>(UINT32_MAX-digit)/10) return 0;
			value=value*10+digit; digits++;
		}
		if(!digits) return 0;
		indices[(*count)++]=value;
		if(!*text) return 1;
		if(*text++!=',' || !*text) return 0;
	}
	return 0;
}

static int Validator_PreviewView(const char *name,struct NativeModelMatrix *out)
{
	// Input is already GTE/model XYZ = stored X,Z,Y. Preserve its axes;
	// flip screen Y for the image coordinate convention, then rotate the view.
	if(strcmp(name,"front")==0) *out=(struct NativeModelMatrix){.m={{4096,0,0},{0,-4096,0},{0,0,4096}}};
	else if(strcmp(name,"side")==0) *out=(struct NativeModelMatrix){.m={{0,0,4096},{0,-4096,0},{-4096,0,0}}};
	else if(strcmp(name,"top")==0) *out=(struct NativeModelMatrix){.m={{4096,0,0},{0,0,4096},{0,4096,0}}};
	else if(strcmp(name,"iso")==0) *out=(struct NativeModelMatrix){.m={{2896,0,2896},{1448,-3547,-1448},{-2508,-2048,2508}}};
	else return 0;
	return 1;
}

#include "native_scene_preview.h"

static int Validator_Preview(const struct NativeMpkView *mpk,const struct NativeVramView *vram,
    u32 modelIndex,u32 headerIndex,const char *animationText,u32 frameIndex,u32 frameCount,int sequence,const char *path,const char *viewName)
{
	struct NativeModelView model; struct NativeModelHeaderView header;
	struct NativeModelDrawWorkspace workspace={0}; struct NativeModelDraw draw;
	struct NativeRasterView target; struct NativeDrawTriangle triangle;
	u8 *rgb=NULL; u32 *depth=NULL; FILE *file=NULL; int success=0;
	char outputPath[4096];
	u32 animationIndex=UINT32_MAX,count;
	enum NativeAssetResult status=NativeMpk_GetModel(mpk,modelIndex,&model);
	if(status!=NATIVE_ASSET_OK) goto done;
	status=NativeModel_GetHeader(&model,headerIndex,&header);
	if(status!=NATIVE_ASSET_OK) goto done;
	if(strcmp(animationText,"auto")==0)
	{
		for(u32 i=0;i<header.animationCount;i++)
		{
			struct NativeAnimationView animation;
			status=NativeModel_GetAnimation(&model,headerIndex,i,&animation);
			if(status==NATIVE_ASSET_NOT_FOUND) continue;
			if(status!=NATIVE_ASSET_OK) goto done;
			animationIndex=i; break;
		}
	}
	else if(strcmp(animationText,"static")!=0 && !Validator_Index(animationText,&animationIndex)) goto done;
	if(frameCount==0 || frameCount>256) goto done;
	if(animationIndex!=UINT32_MAX)
	{
		struct NativeAnimationView animation;
		status=NativeModel_GetAnimation(&model,headerIndex,animationIndex,&animation);
		if(status!=NATIVE_ASSET_OK) goto done;
		if(sequence && (frameIndex>=animation.logicalFrameCount || frameCount>animation.logicalFrameCount-frameIndex)) goto done;
		printf("Animation selected: index %u, name %s, %u logical frames, %u stored frames, interpolated=%d\n",
		    animationIndex,animation.name,animation.logicalFrameCount,animation.storedFrameCount,animation.interpolated);
	}
	else if(sequence) goto done; // Static previews have no playback sequence.
	status=NativeModel_GetVertexCount(&model,headerIndex,&count);
	if(status!=NATIVE_ASSET_OK || count==0) goto done;
	size_t records=count;
	if(records>SIZE_MAX/sizeof(*workspace.packed) || records>SIZE_MAX/sizeof(*workspace.current)) goto done;
	workspace.current=malloc(records*sizeof(*workspace.current)); workspace.next=malloc(records*sizeof(*workspace.next));
	workspace.packed=malloc(records*sizeof(*workspace.packed)); workspace.capacity=records;
	rgb=malloc(512*512*3); depth=malloc(512*512*sizeof(*depth));
	if(workspace.current==NULL || workspace.next==NULL || workspace.packed==NULL || rgb==NULL || depth==NULL) goto done;
	const struct NativeModelMatrix identity={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};
	struct NativeModelMatrix view;
	if(!Validator_PreviewView(viewName,&view)) goto done;
	const s16 instanceScale[3]={4096,4096,4096};
	struct NativeModelMatrix scaled;
	struct NativeProjectionConfig projection={.h=256,.offset={256*65536,256*65536}};
	status=NativeModelMatrix_Build(&identity,header.scale,instanceScale,0,0,&scaled);
	if(status!=NATIVE_ASSET_OK || NativeModelMatrix_Compose(&view,&scaled,&projection.rotation)!=NATIVE_ASSET_OK) goto done;
	// Fit once over the entire requested range: no per-frame camera drift.
	s32 minimum[3]={INT32_MAX,INT32_MAX,INT32_MAX},maximum[3]={INT32_MIN,INT32_MIN,INT32_MIN};
	for(u32 f=0;f<frameCount;f++)
	{
		status=NativeModelDraw_Open(&model,headerIndex,animationIndex,frameIndex+f,&projection,&workspace,NULL,&draw);
		if(status!=NATIVE_ASSET_OK) goto done;
		for(u32 i=0;i<count;i++)
		{
			struct NativeMatrixVector v;
			if(NativeModelMatrix_Apply(&projection.rotation,&workspace.packed[i],&v)!=NATIVE_ASSET_OK) goto done;
			for(unsigned axis=0;axis<3;axis++)
			{ if(v.mac[axis]<minimum[axis]) minimum[axis]=v.mac[axis]; if(v.mac[axis]>maximum[axis]) maximum[axis]=v.mac[axis]; }
		}
	}
	s32 span=maximum[0]-minimum[0]; if(maximum[1]-minimum[1]>span) span=maximum[1]-minimum[1];
	// Keep the fitted near depth above H/2, including very small models.
	s32 distance=span>256 ? span : 256;
	projection.translation[0]=-(minimum[0]+maximum[0])/2;
	projection.translation[1]=-(minimum[1]+maximum[1])/2;
	projection.translation[2]=distance-minimum[2];
	if(NativeRaster_Bind(rgb,512*512*3,depth,512*512,512,512,&target)!=NATIVE_ASSET_OK) goto done;
	// Sequence names are logical indices. Preflight existing files to preserve
	// prior exports; a single-image command retains its replacement behavior.
	if(sequence) for(u32 f=0;f<frameCount;f++)
	{
		int length=snprintf(outputPath,sizeof(outputPath),"%s-%06u.ppm",path,frameIndex+f);
		if(length<0 || (size_t)length>=sizeof(outputPath)) goto done;
		FILE *existing=fopen(outputPath,"rb");
		if(existing!=NULL) { fclose(existing); fprintf(stderr,"Sequence output already exists: %s\n",outputPath); goto done; }
	}
	printf("Preview camera: shared across %u frames, translation %d %d %d, view %s\n",
	    frameCount,projection.translation[0],projection.translation[1],projection.translation[2],viewName);
	for(u32 f=0;f<frameCount;f++)
	{
		status=NativeModelDraw_Open(&model,headerIndex,animationIndex,frameIndex+f,&projection,&workspace,vram,&draw);
		if(status!=NATIVE_ASSET_OK) goto done;
		const u8 background[3]={24,28,36}; NativeRaster_Clear(&target,background);
		size_t triangles=0,writes=0,blended=0;
		while((status=NativeModelDraw_Next(&draw,&triangle))==NATIVE_ASSET_OK)
		{
			struct NativeRasterStats stats;
			status=NativeRaster_Draw(&target,&triangle,vram,&stats);
			if(status!=NATIVE_ASSET_OK) goto done;
			triangles++; writes+=stats.written; blended+=stats.blended;
		}
		if(status!=NATIVE_ASSET_NOT_FOUND || writes==0) goto done;
		const char *output=path;
		if(sequence) { snprintf(outputPath,sizeof(outputPath),"%s-%06u.ppm",path,frameIndex+f); output=outputPath; }
		file=fopen(output,sequence ? "wbx" : "wb"); if(file==NULL) goto done;
		if(fprintf(file,"P6\n512 512\n255\n")<0 || fwrite(rgb,1,512*512*3,file)!=512*512*3) goto done;
		if(fclose(file)!=0) { file=NULL; goto done; } file=NULL;
		printf("Preview OK: model %s (index %u), header %u, animation %s, frame request %u, view %s, %zu triangles, %zu fragment writes (%zu blended) -> %s\n",
		    model.name,modelIndex,headerIndex,animationText,frameIndex+f,viewName,triangles,writes,blended,output);
	}
	printf("Diagnostic software preview with STP material blending, synthetic fitted camera, affine sampling/depth; full game/render parity remains unverified.\n");
	success=1;
done:
	if(!success) fprintf(stderr,"Preview failed (model index %u, status %d).\n",modelIndex,status);
	if(file!=NULL) fclose(file);
	free(workspace.current); free(workspace.next); free(workspace.packed); free(rgb); free(depth);
	return success;
}

int main(int argc, char **argv)
{
	struct ValidatorInput asset = {0}, ptr = {0}, vramFile = {0};
	struct NativeVramView vram = {0};
	u8 *vramStorage = NULL;
	struct NativeAssetLoad load = {0};
	struct NativePtrMapEntry *entries = NULL;
	struct NativeDramLayout layout;
	size_t count = 0;
	u32 vramIndices[16]; size_t vramCount=0;
	const char *previewView="front";
	u32 subdivisionDepth=0;
	u32 assetIndex = 0, ptrIndex = 0, previewModel = 0, previewHeader = 0, previewFrame = 0, previewFrames = 1, sceneTicks = 1;
	int indexed = 0, disc = 0, separate = 0, externalDram = 0, mpk = 0, result = 1, withVram = 0, onlyVram = 0, preview = 0, listModels = 0, sequence = 0, listAnimations = 0, scene = 0, sceneNearby = 0, sceneTerrain = 0;
	enum NativePtrMapResult status;
	if (argc < 3)
		goto usage;
    if(argc>3 && strncmp(argv[argc-1],"subdiv=",7)==0) {
        if(strstr(argv[1],"runtime")==NULL || !Validator_Index(argv[argc-1]+7,&subdivisionDepth) || subdivisionDepth>3) goto usage;
        argc--;
    }
	if ((strcmp(argv[1],"disc-preview")==0 && argc==11) || (strcmp(argv[1],"preview")==0 && argc==10) ||
	    (strcmp(argv[1],"disc-sequence")==0 && argc==12) || (strcmp(argv[1],"sequence")==0 && argc==11) ||
	    (strcmp(argv[1],"disc-scene")==0 && argc==9) || (strcmp(argv[1],"disc-scene-ptr")==0 && argc==10) ||
	    (strcmp(argv[1],"scene")==0 && argc==9) || (strcmp(argv[1],"scene-dram")==0 && argc==8) ||
	    (strcmp(argv[1],"disc-scene-runtime")==0 && argc==10) || (strcmp(argv[1],"disc-scene-ptr-runtime")==0 && argc==11) ||
	    (strcmp(argv[1],"scene-runtime")==0 && argc==10) || (strcmp(argv[1],"scene-dram-runtime")==0 && argc==9) ||
	    (strcmp(argv[1],"disc-scene-textured")==0 && argc==9) || (strcmp(argv[1],"disc-scene-ptr-textured")==0 && argc==10) ||
	    (strcmp(argv[1],"scene-textured")==0 && argc==9) || (strcmp(argv[1],"scene-dram-textured")==0 && argc==8) ||
	    (strcmp(argv[1],"disc-scene-terrain")==0 && argc==9) || (strcmp(argv[1],"disc-scene-ptr-terrain")==0 && argc==10) ||
	    (strcmp(argv[1],"scene-terrain")==0 && argc==9) || (strcmp(argv[1],"scene-dram-terrain")==0 && argc==8) ||
	    (strcmp(argv[1],"disc-scene-near")==0 && argc==9) || (strcmp(argv[1],"disc-scene-ptr-near")==0 && argc==10))
	{
		struct NativeModelMatrix checkedView;
		previewView=argv[--argc];
		if(!Validator_PreviewView(previewView,&checkedView)) goto usage;
	}
	if(strcmp(argv[1],"disc-scene-runtime")==0 && argc==9) { scene=1; sceneNearby=1; sceneTerrain=3; withVram=1; indexed=1; disc=1; }
	else if(strcmp(argv[1],"disc-scene-ptr-runtime")==0 && argc==10) { scene=1; sceneNearby=1; sceneTerrain=3; withVram=1; indexed=1; disc=1; separate=1; externalDram=1; }
	else if(strcmp(argv[1],"scene-runtime")==0 && argc==9) { scene=1; sceneNearby=1; sceneTerrain=3; withVram=1; separate=1; }
	else if(strcmp(argv[1],"scene-dram-runtime")==0 && argc==8) { scene=1; sceneNearby=1; sceneTerrain=3; withVram=1; }
	else if(strcmp(argv[1],"disc-scene-textured")==0 && argc==8) { scene=1; sceneNearby=1; sceneTerrain=2; withVram=1; indexed=1; disc=1; }
	else if(strcmp(argv[1],"disc-scene-ptr-textured")==0 && argc==9) { scene=1; sceneNearby=1; sceneTerrain=2; withVram=1; indexed=1; disc=1; separate=1; externalDram=1; }
	else if(strcmp(argv[1],"scene-textured")==0 && argc==8) { scene=1; sceneNearby=1; sceneTerrain=2; withVram=1; separate=1; }
	else if(strcmp(argv[1],"scene-dram-textured")==0 && argc==7) { scene=1; sceneNearby=1; sceneTerrain=2; withVram=1; }
	else if(strcmp(argv[1],"disc-scene-terrain")==0 && argc==8) { scene=1; sceneNearby=1; sceneTerrain=1; withVram=1; indexed=1; disc=1; }
	else if(strcmp(argv[1],"disc-scene-ptr-terrain")==0 && argc==9) { scene=1; sceneNearby=1; sceneTerrain=1; withVram=1; indexed=1; disc=1; separate=1; externalDram=1; }
	else if(strcmp(argv[1],"scene-terrain")==0 && argc==8) { scene=1; sceneNearby=1; sceneTerrain=1; withVram=1; separate=1; }
	else if(strcmp(argv[1],"scene-dram-terrain")==0 && argc==7) { scene=1; sceneNearby=1; sceneTerrain=1; withVram=1; }
	else if(strcmp(argv[1],"disc-scene-near")==0 && argc==8) { scene=1; sceneNearby=1; withVram=1; indexed=1; disc=1; }
	else if(strcmp(argv[1],"disc-scene-ptr-near")==0 && argc==9) { scene=1; sceneNearby=1; withVram=1; indexed=1; disc=1; separate=1; externalDram=1; }
	else if(strcmp(argv[1],"disc-scene")==0 && argc==8) { scene=1; withVram=1; indexed=1; disc=1; }
	else if(strcmp(argv[1],"disc-scene-ptr")==0 && argc==9) { scene=1; withVram=1; indexed=1; disc=1; separate=1; externalDram=1; }
	else if(strcmp(argv[1],"scene")==0 && argc==8) { scene=1; withVram=1; separate=1; }
	else if(strcmp(argv[1],"scene-dram")==0 && argc==7) { scene=1; withVram=1; }
	else if(strcmp(argv[1],"disc-animations")==0 && argc==6) { listAnimations=1; indexed=1; disc=1; mpk=1; }
	else if(strcmp(argv[1],"animations")==0 && argc==5) { listAnimations=1; mpk=1; }
	else if(strcmp(argv[1],"disc-sequence")==0 && argc==11) { sequence=1; preview=1; withVram=1; indexed=1; disc=1; mpk=1; }
	else if(strcmp(argv[1],"sequence")==0 && argc==10) { sequence=1; preview=1; withVram=1; mpk=1; }
	else if (strcmp(argv[1], "disc-models") == 0 && argc == 4) { listModels = 1; indexed = 1; disc = 1; mpk = 1; }
	else if (strcmp(argv[1], "models") == 0 && argc == 3) { listModels = 1; mpk = 1; }
	else if (strcmp(argv[1], "disc-preview") == 0 && argc == 10) { preview = 1; withVram = 1; indexed = 1; disc = 1; mpk = 1; }
	else if (strcmp(argv[1], "preview") == 0 && argc == 9) { preview = 1; withVram = 1; mpk = 1; }
	else if (strcmp(argv[1], "vram") == 0 && argc == 3) onlyVram = 1;
	else if (strcmp(argv[1], "disc-vram") == 0 && argc == 4) { onlyVram = 1; indexed = 1; disc = 1; }
	else if (strcmp(argv[1], "disc-mpk-vram") == 0 && argc == 5) { withVram = 1; indexed = 1; disc = 1; mpk = 1; }
	else if (strcmp(argv[1], "disc-lev-vram") == 0 && argc == 5) { withVram = 1; indexed = 1; disc = 1; }
	else if (strcmp(argv[1], "disc-lev-ptr-vram") == 0 && argc == 6) { withVram = 1; indexed = 1; disc = 1; separate = 1; externalDram = 1; }
	else if (strcmp(argv[1], "mpk") == 0 && argc == 3) mpk = 1;
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
	if (withVram && disc && !Validator_VramIndices(argv[separate ? 5 : 4], vramIndices, &vramCount)) goto usage;
	if (preview && (!Validator_Index(argv[argc-5-sequence], &previewModel) || !Validator_Index(argv[argc-4-sequence], &previewHeader) || !Validator_Index(argv[argc-2-sequence], &previewFrame))) goto usage;
	if(scene && (!Validator_Index(argv[argc-3-(sceneTerrain==3)],&previewFrame) || !Validator_Index(argv[argc-2-(sceneTerrain==3)],&previewFrames) || previewFrames==0 || previewFrames>256)) goto usage;
	if(sceneTerrain==3 && (!Validator_Index(argv[argc-2],&sceneTicks) || sceneTicks==0 || sceneTicks>256)) goto usage;
	if(sequence && (!Validator_Index(argv[argc-2],&previewFrames) || previewFrames==0 || previewFrames>256)) goto usage;
	if(listAnimations && (!Validator_Index(argv[argc-2],&previewModel) || !Validator_Index(argv[argc-1],&previewHeader))) goto usage;
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
	if (onlyVram || withVram)
	{
		struct NativeVramLoadInfo info;
		vramStorage = calloc(1, NATIVE_VRAM_BYTES);
		if (vramStorage == NULL || NativeVram_Bind(vramStorage, NATIVE_VRAM_BYTES, &vram) != NATIVE_ASSET_OK) goto done;
		size_t uploads=withVram && disc ? vramCount : 1;
		for(size_t i=0;i<uploads;i++)
		{
			free(vramFile.bytes); vramFile=(struct ValidatorInput){0};
			if(withVram && !(disc ? Validator_ReadDisc(vramIndices[i],&vramFile) : Validator_Read(argv[scene && separate ? 4 : 3],0,0,&vramFile)))
			{ fprintf(stderr,"Cannot read VRAM upload %zu.\n",i); goto done; }
			struct ValidatorInput *source=onlyVram ? &asset : &vramFile;
			if(NativeVram_Load(&vram,source->bytes,source->size,&info)!=NATIVE_ASSET_OK)
			{ fprintf(stderr,"Invalid VRAM rectangle asset at upload %zu.\n",i); goto done; }
			if(withVram && disc) printf("VRAM upload %zu: index %u\n",i,vramIndices[i]);
			printf("VRAM OK: %zu rectangles, %zu uploaded words, %zu input bytes\n",info.rectangles,info.words,source->size);
		}
		if (onlyVram) { result = 0; goto done; }
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
		if (NativeMpk_Open(&load.pointers, &view) != NATIVE_ASSET_OK) goto invalid;
		if(listAnimations)
		{
			struct NativeModelView model; struct NativeModelHeaderView header;
			if(NativeMpk_GetModel(&view,previewModel,&model)!=NATIVE_ASSET_OK || NativeModel_GetHeader(&model,previewHeader,&header)!=NATIVE_ASSET_OK) goto invalid;
			for(u32 i=0;i<header.animationCount;i++)
			{
				struct NativeAnimationView animation;
				enum NativeAssetResult ar=NativeModel_GetAnimation(&model,previewHeader,i,&animation);
				if(ar==NATIVE_ASSET_NOT_FOUND) { printf("%u: absent\n",i); continue; }
				if(ar!=NATIVE_ASSET_OK) goto invalid;
				printf("%u: %s logical=%u stored=%u interpolated=%d compressed=%d\n",i,animation.name,animation.logicalFrameCount,animation.storedFrameCount,animation.interpolated,animation.hasDelta);
			}
			result=0; goto done;
		}
		if (listModels)
		{
			for (u32 i=0;i<view.modelCount;i++)
			{
				struct NativeModelView model; struct NativeModelHeaderView header;
				if (NativeMpk_GetModel(&view,i,&model)!=NATIVE_ASSET_OK) goto invalid;
				printf("%u: %s id=%d headers=%u",i,model.name,model.id,model.headerCount);
				if (model.headerCount && NativeModel_GetHeader(&model,0,&header)==NATIVE_ASSET_OK)
					printf(" animations=%u",header.animationCount);
				printf("\n");
			}
			result=0; goto done;
		}
		if (preview)
		{
			result = Validator_Preview(&view,&vram,previewModel,previewHeader,argv[argc-3-sequence],previewFrame,previewFrames,sequence,argv[argc-1],previewView) ? 0 : 1;
			goto done;
		}
		if (!Validator_Models(&view, NULL, withVram ? &vram : NULL))
			goto invalid;
		printf("MPK OK: %u models, %zu payload bytes, %zu relocations, %zu-bit pointers\n",
		    view.modelCount, load.payloadBytes, load.pointers.count, sizeof(void *) * 8);
	}
	else
	{
		struct NativeLevelView view;
		struct NativeMeshView mesh;
		if(scene) {
			if(NativeLevel_Open(&load.pointers,&view)!=NATIVE_ASSET_OK) goto invalid;
			result=Validator_Scene(&view,&vram,previewFrame,previewFrames,sceneNearby,sceneTerrain,sceneTicks,argv[argc-1],previewView,subdivisionDepth) ? 0 : 1;
			goto done;
		}
		if (NativeLevel_Open(&load.pointers, &view) != NATIVE_ASSET_OK || !Validator_Models(NULL, &view, withVram ? &vram : NULL) ||
		    NativeLevel_GetMesh(&view, &mesh) != NATIVE_ASSET_OK || !Validator_VertexAnimation(&view,&mesh))
			goto invalid;
		printf("LEV OK: %u models, %u instances, %u quads, %u vertices, %u BSP nodes, %zu payload bytes, %zu relocations, %zu-bit pointers\n",
		    view.modelCount, view.instanceCount, mesh.quadCount, mesh.vertexCount, mesh.bspCount,
		    load.payloadBytes, load.pointers.count, sizeof(void *) * 8);
	}
	result = 0;
	printf("Validation includes local vertex packing/interpolation, triangles, source colors and texture metadata; Q12 matrices and synthetic camera projection included; actual camera integration, rendering and gameplay remain unverified; pixel sampling requires a VRAM mode.\n");
	goto done;
invalid:
	if (status != NATIVE_PTRMAP_OK)
		fprintf(stderr, "Asset relocation validation failed (status %d).\n", (int)status);
	else fprintf(stderr, "Asset root/model/mesh validation failed.\n");
	goto done;
usage:
	fprintf(stderr, "Usage:\n  ctr_native_asset_validate mpk FILE\n  ctr_native_asset_validate lev-dram FILE\n  ctr_native_asset_validate lev FILE PTR (unprefixed payload)\n  ctr_native_asset_validate lev-external FILE PTR (negative DRAM prefix)\n  ctr_native_asset_validate big-mpk BIGFILE INDEX\n  ctr_native_asset_validate big-lev BIGFILE INDEX\n  ctr_native_asset_validate big-lev-ptr BIGFILE LEV_INDEX PTR_INDEX\n  ctr_native_asset_validate disc-mpk ASSETS_DIR INDEX\n  ctr_native_asset_validate disc-lev ASSETS_DIR INDEX\n  ctr_native_asset_validate disc-lev-ptr ASSETS_DIR LEV_INDEX PTR_INDEX\n");
	fprintf(stderr, "  ctr_native_asset_validate vram FILE | disc-vram ASSETS_DIR VRAM_INDEX\n");
	fprintf(stderr, "  ctr_native_asset_validate disc-mpk-vram|disc-lev-vram ASSETS_DIR MODEL_INDEX VRAM_INDEX[,INDEX...]\n");
	fprintf(stderr, "  ctr_native_asset_validate disc-lev-ptr-vram ASSETS_DIR LEV_INDEX PTR_INDEX VRAM_INDEX[,INDEX...]\n");
	fprintf(stderr, "  ctr_native_asset_validate disc-preview ASSETS_DIR MPK_INDEX VRAM_INDEX[,INDEX...] MODEL_INDEX HEADER_INDEX ANIMATION_INDEX|auto|static FRAME OUTPUT.ppm [front|side|top|iso]\n");
	fprintf(stderr, "  ctr_native_asset_validate preview MPK_FILE VRAM_FILE MODEL_INDEX HEADER_INDEX ANIMATION_INDEX|auto|static FRAME OUTPUT.ppm [front|side|top|iso]\n");
	fprintf(stderr, "  ctr_native_asset_validate disc-models ASSETS_DIR MPK_INDEX | models MPK_FILE\n");
	fprintf(stderr,"  ctr_native_asset_validate disc-animations ASSETS_DIR MPK_INDEX MODEL_INDEX HEADER_INDEX | animations MPK_FILE MODEL_INDEX HEADER_INDEX\n");
	fprintf(stderr,"  ctr_native_asset_validate disc-sequence ASSETS_DIR MPK_INDEX VRAM_INDEX[,INDEX...] MODEL_INDEX HEADER_INDEX ANIMATION_INDEX|auto FIRST COUNT OUTPUT_PREFIX [front|side|top|iso]\n");
	fprintf(stderr,"  ctr_native_asset_validate sequence MPK_FILE VRAM_FILE MODEL_INDEX HEADER_INDEX ANIMATION_INDEX|auto FIRST COUNT OUTPUT_PREFIX [front|side|top|iso]\n");
	fprintf(stderr,"  ctr_native_asset_validate disc-scene ASSETS_DIR LEV_INDEX VRAM_INDEX[,INDEX...] FIRST COUNT OUTPUT.ppm [front|side|top|iso]\n");
	fprintf(stderr,"  ctr_native_asset_validate disc-scene-ptr ASSETS_DIR LEV_INDEX PTR_INDEX VRAM_INDEX[,INDEX...] FIRST COUNT OUTPUT.ppm [front|side|top|iso]\n");
	fprintf(stderr,"  ctr_native_asset_validate scene LEV_FILE PTR_FILE VRAM_FILE FIRST COUNT OUTPUT.ppm [front|side|top|iso]\n");
	fprintf(stderr,"  ctr_native_asset_validate scene-dram LEV_FILE VRAM_FILE FIRST COUNT OUTPUT.ppm [front|side|top|iso]\n");
	fprintf(stderr,"  disc-scene-near / disc-scene-ptr-near: same arguments, FIRST is anchor; choose COUNT nearest authored positions\n");
	fprintf(stderr,"  disc-scene-terrain / disc-scene-ptr-terrain / scene-terrain / scene-dram-terrain: same scene arguments; FIRST is anchor, nearest instances plus coarse terrain within 2048 units\n");
	fprintf(stderr,"  disc-scene-textured / disc-scene-ptr-textured / scene-textured / scene-dram-textured: same terrain arguments, face selectors and near textures at tick 0\n");
	fprintf(stderr,"  disc-scene-runtime / disc-scene-ptr-runtime / scene-runtime / scene-dram-runtime: same textured arguments, add TICKS (1..256) before OUTPUT_PREFIX; optional VIEW then subdiv=0..3; files PREFIX-000000.ppm etc\n");
	result = 2;
done:
	NativeAssetLoad_Reset(&load);
	free(vramStorage);
	free(vramFile.bytes);
	free(entries);
	free(ptr.bytes);
	free(asset.bytes);
	return result;
}
