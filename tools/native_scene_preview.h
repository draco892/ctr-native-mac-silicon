#ifndef NATIVE_SCENE_PREVIEW_H
#define NATIVE_SCENE_PREVIEW_H
// Diagnostic CLI consumer, included by native_asset_validate.c after its camera
// helper. Owns temporary buffers; sources and decoded maps remain read-only.
struct ValidatorSceneItem {
    struct NativeInstanceDefView instance;
    u32 header,animation,vertices;
};
static void Validator_SceneBounds(const struct NativeModelMatrix *view,const s32 position[3],s32 minimum[3],s32 maximum[3])
{
    for(unsigned row=0;row<3;row++) {
        s64 dot=0; for(unsigned k=0;k<3;k++) dot+=(s64)view->m[row][k]*position[k];
        s32 value=(s32)(dot/4096-(dot%4096<0));
        if(value<minimum[row]) minimum[row]=value;
        if(value>maximum[row]) maximum[row]=value;
    }
}
static enum NativeAssetResult Validator_TerrainTriangle(const struct NativeMeshTriangle *source,
    const struct NativeInstanceCamera *camera,struct NativeDrawTriangle *out)
{
    struct NativeProjectionConfig projection={.rotation=camera->view,.h=camera->h};
    memcpy(projection.offset,camera->offset,sizeof(projection.offset));
    for(unsigned row=0;row<3;row++) {
        s64 dot=0; for(unsigned k=0;k<3;k++) dot-=(s64)camera->view.m[row][k]*camera->position[k];
        projection.translation[row]=(s32)(dot/4096-(dot%4096<0));
    }
    struct NativePackedModelVertex packed[3]; struct NativeProjectionState state={0};
    struct NativeProjectionResult projected; struct NativeDrawTriangle triangle={0};
    for(unsigned k=0;k<3;k++) {
        const s16 *position=source->vertices[k].position;
        packed[k].xy=(u16)position[0]|((u32)(u16)position[1]<<16); packed[k].z=(u16)position[2];
        triangle.source.vertices[k]=source->indices[k]; triangle.source.colors[k]=source->vertices[k].colorHigh;
    }
    enum NativeAssetResult status=NativeModelProjection_Project3(&projection,packed,&state,&projected);
    if(status!=NATIVE_ASSET_OK) return status;
    memcpy(triangle.screen,state.screen,sizeof(triangle.screen));
    for(unsigned k=0;k<3;k++) triangle.depth[k]=state.depth[k+1];
    triangle.projectionFlags=projected.flags;
    *out=triangle; return NATIVE_ASSET_OK;
}
static int Validator_Scene(const struct NativeLevelView *level,const struct NativeVramView *vram,
    u32 first,u32 requested,int nearby,int terrain,const char *path,const char *viewName)
{
    struct ValidatorSceneItem *items=NULL;
    struct NativeModelDrawWorkspace workspace={0}; struct NativeRasterView target;
    struct NativeInstanceCamera camera={.h=256,.offset={256*65536,256*65536}};
    u8 *rgb=NULL; u32 *depth=NULL; FILE *file=NULL; int success=0;
    size_t selected=0,unsupported=0,unavailable=0,maxVertices=0;
    struct NativeMeshView mesh={0}; u32 *terrainQuads=NULL; size_t terrainCount=0;
    enum NativeAssetResult status=NATIVE_ASSET_INVALID_ARGUMENT;
    if(requested==0 || requested>256 || first>=level->instanceCount || (nearby ? requested>level->instanceCount : requested>level->instanceCount-first) ||
       !Validator_PreviewView(viewName,&camera.view)) goto done;
    if(terrain) {
        struct NativeInstanceDefView anchor;
        status=NativeLevel_GetInstance(level,first,&anchor); if(status!=NATIVE_ASSET_OK) goto done;
        status=NativeLevel_GetMesh(level,&mesh); if(status!=NATIVE_ASSET_OK) goto done;
        size_t terrainBytes=(size_t)mesh.quadCount*sizeof(*terrainQuads);
        if(terrainBytes/sizeof(*terrainQuads)!=mesh.quadCount) goto done;
        if(mesh.quadCount) { terrainQuads=malloc(terrainBytes); if(terrainQuads==NULL) goto done; }
        for(u32 q=0;q<mesh.quadCount;q++) {
            struct NativeMeshQuad quad; s32 low[3]={INT32_MAX,INT32_MAX,INT32_MAX},high[3]={INT32_MIN,INT32_MIN,INT32_MIN};
            status=NativeMesh_GetQuad(&mesh,q,&quad); if(status!=NATIVE_ASSET_OK) goto done;
            for(unsigned v=0;v<9;v++) {
                struct NativeMeshVertex vertex;
                status=NativeMesh_GetVertex(&mesh,quad.indices[v],&vertex); if(status!=NATIVE_ASSET_OK) goto done;
                for(unsigned k=0;k<3;k++) { if(vertex.position[k]<low[k]) low[k]=vertex.position[k]; if(vertex.position[k]>high[k]) high[k]=vertex.position[k]; }
            }
            s64 distance=0;
            for(unsigned k=0;k<3;k++) { s64 d=anchor.position[k]<low[k] ? low[k]-anchor.position[k] : anchor.position[k]>high[k] ? anchor.position[k]-high[k] : 0; distance+=d*d; }
            if(distance<=2048LL*2048) terrainQuads[terrainCount++]=q;
        }
        printf("Terrain selection: %zu/%u quad blocks within 2048 units of instance %u, coarse vertex colors\n",terrainCount,mesh.quadCount,first);
    }
    u32 indices[256];
    for(u32 i=0;i<requested;i++) indices[i]=first+i;
    if(nearby) {
        struct NativeInstanceDefView anchor;
        status=NativeLevel_GetInstance(level,first,&anchor); if(status!=NATIVE_ASSET_OK) goto done;
        s64 distances[256]={0}; size_t used=1; indices[0]=first;
        for(u32 i=0;i<level->instanceCount;i++) {
            if(i==first) continue;
            struct NativeInstanceDefView candidate;
            status=NativeLevel_GetInstance(level,i,&candidate); if(status!=NATIVE_ASSET_OK) goto done;
            s64 distance=0; for(unsigned k=0;k<3;k++) { s64 d=(s32)candidate.position[k]-anchor.position[k]; distance+=d*d; }
            size_t at=0; while(at<used && distance>=distances[at]) at++;
            if(at>=requested) continue;
            if(used<requested) used++;
            for(size_t k=used-1;k>at;k--) { indices[k]=indices[k-1]; distances[k]=distances[k-1]; }
            indices[at]=i; distances[at]=distance;
        }
    }
    items=calloc(requested,sizeof(*items)); if(items==NULL) goto done;
    for(u32 i=0;i<requested;i++) {
        struct ValidatorSceneItem item={.animation=UINT32_MAX};
        status=NativeLevel_GetInstance(level,indices[i],&item.instance); if(status!=NATIVE_ASSET_OK) goto done;
        // Fit and render only the normal world-instance path in this milestone.
        if(item.instance.flags&(0x200u|0x400u|0x800u|0x8000000u)) { unsupported++; continue; }
        int found=0,custom=0;
        for(u32 h=0;h<item.instance.model.headerCount;h++) {
            struct NativeModelHeaderView header;
            status=NativeModel_GetHeader(&item.instance.model,h,&header); if(status!=NATIVE_ASSET_OK) goto done;
            if(header.flags&1u) { custom=1; continue; }
            status=NativeModel_GetVertexCount(&item.instance.model,h,&item.vertices);
            if(status==NATIVE_ASSET_NOT_FOUND) continue;
            if(status!=NATIVE_ASSET_OK) goto done;
            if(item.vertices==0) continue;
            item.animation=UINT32_MAX;
            if(header.animationCount) {
                for(u32 a=0;a<header.animationCount;a++) {
                    struct NativeAnimationView animation;
                    status=NativeModel_GetAnimation(&item.instance.model,h,a,&animation);
                    if(status==NATIVE_ASSET_NOT_FOUND) continue;
                    if(status!=NATIVE_ASSET_OK) goto done;
                    item.animation=a; break;
                }
                if(item.animation==UINT32_MAX) continue;
            } else {
                struct NativeFrameView frame;
                status=NativeModel_GetStaticFrame(&item.instance.model,h,0,&frame);
                if(status==NATIVE_ASSET_NOT_FOUND) continue;
                if(status!=NATIVE_ASSET_OK) goto done;
            }
            item.header=h; found=1; break;
        }
        if(!found) { if(custom) unsupported++; else unavailable++; continue; }
        printf("Scene instance %u: %s, model %s, header %u, position %d %d %d\n",indices[i],item.instance.name,item.instance.model.name,item.header,item.instance.position[0],item.instance.position[1],item.instance.position[2]);
        items[selected++]=item;
        if(item.vertices>maxVertices) maxVertices=item.vertices;
    }
    if((selected==0 && terrainCount==0) || maxVertices>SIZE_MAX/sizeof(*workspace.packed) || maxVertices>SIZE_MAX/sizeof(*workspace.current)) goto done;
    if(maxVertices==0) maxVertices=1;
    workspace.current=malloc(maxVertices*sizeof(*workspace.current)); workspace.next=malloc(maxVertices*sizeof(*workspace.next));
    workspace.packed=malloc(maxVertices*sizeof(*workspace.packed)); workspace.capacity=maxVertices;
    rgb=malloc(512*512*3); depth=malloc(512*512*sizeof(*depth));
    if(workspace.current==NULL || workspace.next==NULL || workspace.packed==NULL || rgb==NULL || depth==NULL) goto done;
    s32 minimum[3]={INT32_MAX,INT32_MAX,INT32_MAX},maximum[3]={INT32_MIN,INT32_MIN,INT32_MIN};
    for(size_t i=0;i<selected;i++) {
        struct ValidatorSceneItem *item=&items[i]; struct NativeModelHeaderView header;
        struct NativeModelMatrix rotation,world; u32 count;
        status=NativeModel_GetHeader(&item->instance.model,item->header,&header); if(status!=NATIVE_ASSET_OK) goto done;
        status=NativeInstance_Rotation(item->instance.rotation,&rotation); if(status!=NATIVE_ASSET_OK) goto done;
        status=NativeModelMatrix_Build(&rotation,header.scale,item->instance.scale,4096,0,&world); if(status!=NATIVE_ASSET_OK) goto done;
        status=item->animation==UINT32_MAX ?
            NativeModel_PackStaticVertices(&item->instance.model,item->header,workspace.current,workspace.packed,maxVertices,&count) :
            NativeModel_PackAnimationVertices(&item->instance.model,item->header,item->animation,0,workspace.current,workspace.next,workspace.packed,maxVertices,&count);
        if(status!=NATIVE_ASSET_OK) goto done;
        for(u32 v=0;v<count;v++) {
            struct NativeMatrixVector local;
            status=NativeModelMatrix_Apply(&world,&workspace.packed[v],&local); if(status!=NATIVE_ASSET_OK) goto done;
            s32 position[3]; for(unsigned k=0;k<3;k++) position[k]=local.mac[k]+item->instance.position[k];
            Validator_SceneBounds(&camera.view,position,minimum,maximum);
        }
    }
    for(size_t q=0;q<terrainCount;q++) for(u32 t=0;t<2;t++) {
        struct NativeMeshTriangle triangle;
        status=NativeMesh_GetLowTriangle(&mesh,terrainQuads[q],t,&triangle); if(status!=NATIVE_ASSET_OK) goto done;
        for(unsigned v=0;v<3;v++) {
            s32 position[3]; for(unsigned k=0;k<3;k++) position[k]=triangle.vertices[v].position[k];
            Validator_SceneBounds(&camera.view,position,minimum,maximum);
        }
    }
    // Fit a single inspection camera to world-space bounds. The normal adapter
    // still chooses each instance's near/far matrix and translation together.
    s32 span=maximum[0]-minimum[0]; if(maximum[1]-minimum[1]>span) span=maximum[1]-minimum[1];
    s32 distance=span>256 ? span : 256;
    const s32 viewPosition[3]={(minimum[0]+maximum[0])/2,(minimum[1]+maximum[1])/2,minimum[2]-distance};
    for(unsigned row=0;row<3;row++) {
        s64 dot=0; for(unsigned k=0;k<3;k++) dot+=(s64)camera.view.m[k][row]*viewPosition[k];
        camera.position[row]=(s32)(dot/4096-(dot%4096<0));
    }
    if(NativeRaster_Bind(rgb,512*512*3,depth,512*512,512,512,&target)!=NATIVE_ASSET_OK) goto done;
    const u8 background[3]={24,28,36}; NativeRaster_Clear(&target,background);
    size_t triangles=0,writes=0,skipped=0;
    size_t terrainTriangles=0,terrainWrites=0;
    for(size_t q=0;q<terrainCount;q++) for(u32 t=0;t<2;t++) {
        struct NativeMeshTriangle source; struct NativeDrawTriangle triangle; struct NativeRasterStats stats;
        status=NativeMesh_GetLowTriangle(&mesh,terrainQuads[q],t,&source); if(status!=NATIVE_ASSET_OK) goto done;
        status=Validator_TerrainTriangle(&source,&camera,&triangle); if(status!=NATIVE_ASSET_OK) goto done;
        status=NativeRaster_Draw(&target,&triangle,vram,&stats); if(status!=NATIVE_ASSET_OK) goto done;
        terrainTriangles++; terrainWrites+=stats.written;
        triangles++; writes+=stats.written; skipped+=stats.skipped!=0;
    }
    for(size_t i=0;i<selected;i++) {
        struct ValidatorSceneItem *item=&items[i]; struct NativeProjectionConfig projection;
        struct NativeModelDraw draw; struct NativeDrawTriangle triangle; s32 rawDepth;
        status=NativeInstance_Projection(&item->instance,item->header,&camera,&projection,&rawDepth); if(status!=NATIVE_ASSET_OK) goto done;
        status=NativeModelDraw_Open(&item->instance.model,item->header,item->animation,0,&projection,&workspace,vram,&draw); if(status!=NATIVE_ASSET_OK) goto done;
        while((status=NativeModelDraw_Next(&draw,&triangle))==NATIVE_ASSET_OK) {
            // Near model projection uses four times world depth. Normalize for
            // comparison with terrain (and far models) in this scene mode only.
            if(terrain && rawDepth<4096) for(unsigned k=0;k<3;k++) triangle.depth[k]/=4;
            struct NativeRasterStats stats; status=NativeRaster_Draw(&target,&triangle,vram,&stats);
            if(status!=NATIVE_ASSET_OK) goto done;
            triangles++; writes+=stats.written; skipped+=stats.skipped!=0;
        }
        if(status!=NATIVE_ASSET_NOT_FOUND) goto done;
    }
    if(writes==0) goto done;
    file=fopen(path,"wb"); if(file==NULL) goto done;
    if(fprintf(file,"P6\n512 512\n255\n")<0 || fwrite(rgb,1,512*512*3,file)!=512*512*3) goto done;
    if(fclose(file)!=0) { file=NULL; goto done; } file=NULL;
    printf("Scene selection: %s\n",nearby ? "nearest authored positions" : "consecutive definitions");
    if(terrain) printf("Terrain OK: %zu quad blocks, %zu coarse triangles, %zu fragment writes; shared world-unit depth, no terrain textures\n",terrainCount,terrainTriangles,terrainWrites);
    printf("Scene OK: first %u, requested %u, rendered %zu instances, unsupported %zu, unavailable %zu, %zu triangles, %zu skipped triangles, %zu fragment writes -> %s\n",
        first,requested,selected,unsupported,unavailable,triangles,skipped,writes,path);
    printf("Scene camera: %d %d %d, view %s; one normal header/instance, frame 0, shared RGB/depth; terrain/material/visibility parity unverified.\n",
        camera.position[0],camera.position[1],camera.position[2],viewName);
    success=1;
done:
    if(!success) fprintf(stderr,"Scene failed (status %d, selected %zu instances).\n",status,selected);
    if(file!=NULL) fclose(file);
    free(terrainQuads); free(items); free(workspace.current); free(workspace.next); free(workspace.packed); free(rgb); free(depth);
    return success;
}
#endif
