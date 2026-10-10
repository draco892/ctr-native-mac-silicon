#include <platform/native_scene_render.h>
#include <string.h>
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
        if(!SceneRender_Accept(camera,&triangle,&stats)) continue;
        if(sink!=NULL) { status=sink(user,&triangle); if(status!=NATIVE_ASSET_OK) return status; }
        stats.triangles++;
    }
    if(status!=NATIVE_ASSET_NOT_FOUND) return status;
    *out=stats; return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeSceneRender_Terrain(const struct NativeLevelView *level,const struct NativeMeshView *mesh,
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
            if(!frontFacing) { stats.culled++; continue; }
            if(!SceneRender_Accept(camera,&triangle,&stats)) continue;
            if(sink!=NULL) { status=sink(user,&triangle); if(status!=NATIVE_ASSET_OK) return status; }
            stats.triangles++;
        }
    }
    *out=stats; return NATIVE_ASSET_OK;
}
