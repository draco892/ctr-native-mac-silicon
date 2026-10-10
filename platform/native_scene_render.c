#include <platform/native_scene_render.h>
#include <platform/native_scene_geometry.h>
#include <string.h>
#include <stdlib.h>
#include <platform/native_vertex_animation.h>
enum NativeAssetResult NativeRuntimeInstance_Init(const struct NativeInstanceDefView *definition,struct NativeRuntimeInstance *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    if(definition==NULL || definition->model.map==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    struct NativeRuntimeInstance instance={.model=definition->model,.flags=definition->flags,.animation=UINT32_MAX};
    for(unsigned k=0;k<3;k++) { instance.position[k]=definition->position[k]; instance.scale[k]=definition->scale[k]; }
    enum NativeAssetResult status=NativeInstance_Rotation(definition->rotation,&instance.rotation);
    if(status!=NATIVE_ASSET_OK) return status;
    *out=instance; return NATIVE_ASSET_OK;
}
static int SceneRender_Accept(const struct NativeSceneCamera *camera,const struct NativeDrawTriangle *triangle,struct NativeSceneRenderStats *stats)
{
    if(triangle->projectionFlags&0x60000u) { stats->flagged++; return 0; }
    s32 minimum[2]={INT16_MAX,INT16_MAX},maximum[2]={INT16_MIN,INT16_MIN};
    unsigned near=0,far=0;
    for(unsigned k=0;k<3;k++) {
        near+=triangle->depth[k]<camera->nearDepth; far+=triangle->depth[k]>camera->farDepth;
        for(unsigned axis=0;axis<2;axis++) { if(triangle->screen[k][axis]<minimum[axis]) minimum[axis]=triangle->screen[k][axis]; if(triangle->screen[k][axis]>maximum[axis]) maximum[axis]=triangle->screen[k][axis]; }
    }
    if(near || far==3 || maximum[0]<0 || maximum[1]<0 || minimum[0]>=(s32)camera->width || minimum[1]>=(s32)camera->height) { stats->culled++; return 0; }
    if(triangle->signedArea==0) { stats->degenerate++; return 0; }
    return 1;
}
struct SceneRenderEmit {
    const struct NativeSceneCamera *camera;
    const struct NativeTerrainTriangle *terrain;
    int reflected;
    NativeSceneTriangleSink sink; void *user;
    struct NativeSceneRenderStats *stats;
};
static enum NativeAssetResult SceneRender_FinalTriangle(void *user,const struct NativeDrawTriangle *triangle)
{
    struct SceneRenderEmit *ctx=user;
    if(ctx->terrain!=NULL && !NativeTerrain_FrontFacing(ctx->terrain,ctx->reflected ? -triangle->signedArea : triangle->signedArea)) { ctx->stats->culled++; return NATIVE_ASSET_OK; }
    if(!SceneRender_Accept(ctx->camera,triangle,ctx->stats)) return NATIVE_ASSET_OK;
    if(ctx->sink!=NULL) { enum NativeAssetResult status=ctx->sink(ctx->user,triangle); if(status!=NATIVE_ASSET_OK) return status; }
    ctx->stats->triangles++; return NATIVE_ASSET_OK;
}
static enum NativeAssetResult SceneRender_Emit(struct SceneRenderEmit *ctx,const struct NativeDrawTriangle *original,const struct NativeSceneClipVertex vertices[3])
{
    int clip=NativeSceneGeometry_NeedsClip(ctx->camera,vertices) || (original->projectionFlags&0x60000u);
    if(!clip && !ctx->camera->subdivisionDepth) return SceneRender_FinalTriangle(ctx,original);
    if(clip) ctx->stats->clipped++;
    if(ctx->camera->subdivisionDepth) ctx->stats->subdivided++;
    size_t count;
    enum NativeAssetResult status=NativeSceneGeometry_Subdivide(ctx->camera,&original->source,vertices,original->orderingBias,
        ctx->camera->subdivisionDepth,SceneRender_FinalTriangle,ctx,&count);
    if(status==NATIVE_ASSET_OK && !count) ctx->stats->culled++;
    return status;
}
enum NativeAssetResult NativeSceneRender_Model(const struct NativeRuntimeInstance *instance,
    const struct NativeSceneCamera *camera,const struct NativeModelDrawWorkspace *workspace,
    const struct NativeVramView *vram,NativeSceneTriangleSink sink,void *user,struct NativeSceneRenderStats *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    if(instance==NULL || !NativeSceneCamera_IsValid(camera)) return NATIVE_ASSET_INVALID_ARGUMENT;
    // Custom/billboard/huge/screen paths need separate material/matrix contracts.
    if(instance->flags&(0x200u|0x400u|0x800u|0x8000000u)) return NATIVE_ASSET_NOT_FOUND;
    struct NativeModelHeaderView header;
    enum NativeAssetResult status=NativeModel_GetHeader(&instance->model,instance->header,&header); if(status!=NATIVE_ASSET_OK) return status;
    if(header.flags&1) return NATIVE_ASSET_NOT_FOUND;
    struct NativeProjectionConfig projection={.h=camera->transform.h,.dqa=camera->transform.dqa,.dqb=camera->transform.dqb};
    memcpy(projection.offset,camera->transform.offset,sizeof(projection.offset));
    s32 rawDepth; struct NativeModelMatrix world;
    status=NativeModelProjection_ViewTranslation(&camera->transform.view,instance->position,camera->transform.position,0,0,projection.translation,&rawDepth); if(status!=NATIVE_ASSET_OK) return status;
    status=NativeModelMatrix_Build(&instance->rotation,header.scale,instance->scale,rawDepth,0,&world); if(status!=NATIVE_ASSET_OK) return status;
    status=NativeModelMatrix_Compose(&camera->transform.view,&world,&projection.rotation); if(status!=NATIVE_ASSET_OK) return status;
    struct NativeModelDraw draw; struct NativeDrawTriangle triangle; struct NativeSceneRenderStats stats={0};
    status=NativeModelDraw_Open(&instance->model,instance->header,instance->animation,instance->frame,&projection,workspace,vram,&draw); if(status!=NATIVE_ASSET_OK) return status;
    while((status=NativeModelDraw_Next(&draw,&triangle))==NATIVE_ASSET_OK) {
        if(rawDepth<4096) for(unsigned k=0;k<3;k++) triangle.depth[k]/=4;
        triangle.averageDepth=(u16)(((u32)triangle.depth[0]+triangle.depth[1]+triangle.depth[2])/3);
        struct NativeSceneClipVertex vertices[3];
        for(unsigned k=0;k<3;k++) {
            status=NativeSceneGeometry_Transform(&projection,&draw.packed[triangle.source.vertices[k]],rawDepth<4096 ? 4 : 1,vertices[k].view);
            if(status!=NATIVE_ASSET_OK) return status;
            NativeSceneGeometry_Attributes(&triangle.source,k,&vertices[k]);
        }
        struct SceneRenderEmit emit={camera,NULL,0,sink,user,&stats};
        status=SceneRender_Emit(&emit,&triangle,vertices);
        if(status!=NATIVE_ASSET_OK) return status;
    }
    if(status!=NATIVE_ASSET_NOT_FOUND) return status;
    *out=stats; return NATIVE_ASSET_OK;
}
static enum NativeAssetResult SceneRender_TerrainMesh(const struct NativeLevelView *level,const struct NativeMeshView *mesh,
    const struct NativeSceneCamera *camera,const void *leafPvs,size_t leafPvsBytes,const void *facePvs,size_t facePvsBytes,
    u32 tick,u32 lod,const struct NativeVisibilityWorkspace *workspace,NativeSceneTriangleSink sink,void *user,struct NativeSceneRenderStats *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    if(mesh==NULL || !NativeSceneCamera_IsValid(camera) || (lod!=UINT32_MAX && lod>=3)) return NATIVE_ASSET_INVALID_ARGUMENT;
    if(facePvs!=NULL && facePvsBytes<((size_t)mesh->quadCount+31)/32*4) return NATIVE_ASSET_INVALID_DATA;
    struct NativeVisibilityResult visible;
    enum NativeAssetResult status=NativeSceneVisibility_Select(level,mesh,camera,leafPvs,leafPvsBytes,workspace,&visible); if(status!=NATIVE_ASSET_OK) return status;
    struct NativeSceneRenderStats stats={.visibleNodes=visible.visibleNodes,.visibleQuads=visible.visibleQuads};
    for(u32 q=0;q<mesh->quadCount;q++) {
        if(!visible.quadMask[q]) continue;
        if(facePvs!=NULL) {
            struct NativeMeshQuad quad;
            status=NativeMesh_GetQuad(mesh,q,&quad); if(status!=NATIVE_ASSET_OK) return status;
            size_t word=(size_t)quad.blockID/32*4;
            if(word>facePvsBytes || facePvsBytes-word<4) return NATIVE_ASSET_INVALID_DATA;
            if(!(CTR_ReadU32LE((const u8 *)facePvs+word)&(0x80000000u>>(quad.blockID%32)))) continue;
        }
        u32 textureLod=lod;
        if(lod==UINT32_MAX) {
            struct NativeMeshQuad quad; status=NativeMesh_GetQuad(mesh,q,&quad); if(status!=NATIVE_ASSET_OK) return status;
            s64 center[3]={0},depth=0;
            for(unsigned v=0;v<4;v++) {
                struct NativeMeshVertex vertex; status=NativeMesh_GetVertex(mesh,quad.indices[v],&vertex); if(status!=NATIVE_ASSET_OK) return status;
                for(unsigned k=0;k<3;k++) center[k]+=vertex.position[k];
            }
            for(unsigned k=0;k<3;k++) depth+=(s64)camera->transform.view.m[2][k]*(center[k]/4-camera->transform.position[k]);
            depth/=4096;
            textureLod=depth<(s32)camera->transform.h*12 ? 2 : depth<(s32)camera->transform.h*24 ? 1 : 0;
        }
        for(u32 face=0;face<4;face++) for(u32 t=0;t<2;t++) {
            struct NativeTerrainTriangle source; struct NativeDrawTriangle triangle; int frontFacing;
            status=NativeTerrain_GetTriangle(level,mesh,q,face,t,textureLod,tick,&source);
            if(status==NATIVE_ASSET_NOT_FOUND) { stats.degenerate++; continue; } if(status!=NATIVE_ASSET_OK) return status;
            status=NativeSceneCamera_ProjectTerrain(camera,&source,&triangle,&frontFacing); if(status!=NATIVE_ASSET_OK) return status;
            struct NativeProjectionConfig projection={.rotation=camera->transform.view,.h=camera->transform.h};
            for(unsigned row=0;row<3;row++) {
                s64 dot=0;
                for(unsigned k=0;k<3;k++) dot-=(s64)projection.rotation.m[row][k]*camera->transform.position[k];
                s64 translation=dot/4096-(dot%4096<0);
                if(translation<INT32_MIN || translation>INT32_MAX) return NATIVE_ASSET_INVALID_ARGUMENT;
                projection.translation[row]=(s32)translation;
            }
            struct NativeSceneClipVertex vertices[3];
            for(unsigned k=0;k<3;k++) {
                const s16 *p=source.geometry.vertices[k].position;
                struct NativePackedModelVertex packed={.xy=(u16)p[0]|((u32)(u16)p[1]<<16),.z=(u16)p[2]};
                status=NativeSceneGeometry_Transform(&projection,&packed,1,vertices[k].view);
                if(status!=NATIVE_ASSET_OK) return status;
                NativeSceneGeometry_Attributes(&triangle.source,k,&vertices[k]);
            }
            s64 determinant=0;
            for(unsigned k=0;k<3;k++) determinant+=(s64)projection.rotation.m[0][k]*
                ((s64)projection.rotation.m[1][(k+1)%3]*projection.rotation.m[2][(k+2)%3]-(s64)projection.rotation.m[1][(k+2)%3]*projection.rotation.m[2][(k+1)%3]);
            struct SceneRenderEmit emit={camera,&source,determinant<0,sink,user,&stats};
            status=SceneRender_Emit(&emit,&triangle,vertices);
            if(status!=NATIVE_ASSET_OK) return status;
        }
    }
    *out=stats; return NATIVE_ASSET_OK;
}

enum NativeAssetResult NativeSceneRender_Terrain(const struct NativeLevelView *level,const struct NativeMeshView *mesh,
    const struct NativeSceneCamera *camera,const void *leafPvs,size_t leafPvsBytes,const void *facePvs,size_t facePvsBytes,
    u32 tick,u32 lod,const struct NativeVisibilityWorkspace *workspace,NativeSceneTriangleSink sink,void *user,struct NativeSceneRenderStats *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    struct NativeVertexAnimationView animation;
    enum NativeAssetResult status=NativeVertexAnimation_Open(level,mesh,&animation);
    if(status!=NATIVE_ASSET_OK) return status;
    void *vertices=NULL; size_t bytes=0;
    if(animation.count) {
#if SIZE_MAX <= UINT32_MAX
        if(mesh->vertexCount>SIZE_MAX/NATIVE_VERTEX_BYTES) return NATIVE_ASSET_INVALID_DATA;
#endif
        bytes=(size_t)mesh->vertexCount*NATIVE_VERTEX_BYTES;
        vertices=malloc(bytes);
        if(vertices==NULL) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
    }
    struct NativeMeshView animated;
    status=NativeVertexAnimation_Apply(&animation,tick,NULL,NULL,0,vertices,bytes,&animated);
    if(status==NATIVE_ASSET_OK) status=SceneRender_TerrainMesh(level,&animated,camera,leafPvs,leafPvsBytes,facePvs,facePvsBytes,tick,lod,workspace,sink,user,out);
    free(vertices); return status;
}
