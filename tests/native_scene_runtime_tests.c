#include <platform/native_scene_render.h>
#include <platform/native_scene_assets.h>
#include <platform/native_scene_gpu.h>
#include <platform/native_gpu_links.h>
#include <namespace_Mempack.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s line %d\n",#c,__LINE__); return 1; } } while(0)
// Compile the actual library consumer with a minimal runtime aggregate, rather
// than pulling 32-bit binary-layout assertions into this ARM64 unit target.
static struct Mempack scenePools[4];
static struct Mempack *sceneActive=&scenePools[0];
struct Mempack **Platform_GetActiveMempackSlot(void) { return &sceneActive; }
struct Mempack *Platform_GetMempackPools(void) { return scenePools; }
void NativeCheckpoint_OnMempackArenaReset(void) {}
void CTR_ErrorScreen(u8 r,u8 g,u8 b) { (void)r; (void)g; (void)b; }
#define COMMON_H
struct Model { char name[16]; s16 id,numHeaders; void *headers; };
struct GameTracker { struct Model *modelPtr[NATIVE_MODEL_LIBRARY_SLOTS]; };
void Platform_LogError(const char *format,...) { (void)format; }
#include "../game/LibraryOfModels.c"
struct SceneTestMatrix { s16 m[3][3]; s32 t[3]; };
struct PushBuffer { struct { s16 w,h; } rect; s32 distanceToScreen_PREV; struct SceneTestMatrix matrix_Camera,matrix_ViewProj; u32 *ptrOT; };
struct PrimMem { void *cursor,*end; s32 primitiveCount; };
#include "../game/RenderLevel/NativeSceneConsumer.c"
static void Put16(u8 *p,u16 n) { p[0]=(u8)n; p[1]=(u8)(n>>8); }
static void Box(u8 *p,s16 x0,s16 x1)
{ Put16(p,(u16)x0); Put16(p+2,0xff00); Put16(p+4,200); Put16(p+6,(u16)x1); Put16(p+8,256); Put16(p+10,600); }
static const u32 slots[]={0,0x10,0x18,0x20c,0x210,0x218,0x25c,0x27c,0x850,0x870,0x914,0x960,0x964,0x96c};
static void Fixture(u8 *a,u8 *ptr)
{
    memset(a,0,0xb00); CTR_WriteU32LE(a,0x200);
    CTR_WriteU32LE(a+0xc,1); CTR_WriteU32LE(a+0x10,0x860);
    CTR_WriteU32LE(a+0x14,1); CTR_WriteU32LE(a+0x18,0x850); CTR_WriteU32LE(a+0x850,0x900);
    CTR_WriteU32LE(a+0x200,2); CTR_WriteU32LE(a+0x204,18); CTR_WriteU32LE(a+0x20c,0x300);
    CTR_WriteU32LE(a+0x210,0x3b8); CTR_WriteU32LE(a+0x218,0x220); CTR_WriteU32LE(a+0x21c,3);
    Box(a+0x224,-128,1200); Put16(a+0x238,0x4001); Put16(a+0x23a,0x4002);
    for(unsigned leaf=1;leaf<=2;leaf++) {
        u8 *node=a+0x220+leaf*32; Put16(node,1); Box(node+4,leaf==1 ? -128 : 900,leaf==1 ? 128 : 1200);
        CTR_WriteU32LE(node+24,1); CTR_WriteU32LE(node+28,0x300+(leaf-1)*92);
    }
    static const s16 grid[9][2]={{-64,-64},{64,-64},{-64,64},{64,64},{0,-64},{-64,0},{0,0},{64,0},{0,64}};
    for(unsigned q=0;q<2;q++) {
        u8 *quad=a+0x300+q*92; CTR_WriteU32LE(quad+20,0x80000000);
        for(unsigned v=0;v<9;v++) {
            Put16(quad+2*v,(u16)(q*9+v)); u8 *vertex=a+0x3b8+(q*9+v)*16;
            Put16(vertex,(u16)(grid[v][0]+q*1000)); Put16(vertex+2,(u16)grid[v][1]); Put16(vertex+4,400); CTR_WriteU32LE(vertex+8,0x00808080);
        }
    }
    CTR_WriteU32LE(a+0x870,0x900); for(unsigned k=0;k<3;k++) Put16(a+0x874+2*k,4096); Put16(a+0x894,300);
    Put16(a+0x910,17); Put16(a+0x912,1); CTR_WriteU32LE(a+0x914,0x940);
    for(unsigned k=0;k<3;k++) Put16(a+0x958+2*k,4096);
    CTR_WriteU32LE(a+0x960,0xa60); CTR_WriteU32LE(a+0x964,0xa20); CTR_WriteU32LE(a+0x96c,0xa80);
    CTR_WriteU32LE(a+0xa38,28); const u8 xyz[9]={0,0,0,16,0,0,0,8,16}; memcpy(a+0xa3c,xyz,9);
    CTR_WriteU32LE(a+0xa64,0x80010000); CTR_WriteU32LE(a+0xa68,0x00020000); CTR_WriteU32LE(a+0xa6c,0x00030000); CTR_WriteU32LE(a+0xa70,0xffffffff); CTR_WriteU32LE(a+0xa80,0x00808080);
    CTR_WriteU32LE(ptr,sizeof(slots)); for(unsigned k=0;k<sizeof(slots)/sizeof(*slots);k++) CTR_WriteU32LE(ptr+4+k*4,slots[k]);
}
struct SceneTestRebase { uintptr_t source,target; u32 size; };
static int RebaseOwner(void *user,u64 source,u32 bytes,uintptr_t *live)
{
    struct SceneTestRebase *ctx=user;
    if(source!=ctx->source || bytes!=ctx->size) return 0;
    *live=ctx->target; return 1;
}
static enum NativeAssetResult Count(void *user,const struct NativeDrawTriangle *triangle)
{ (*(size_t *)user)++; return triangle->depth[0] ? NATIVE_ASSET_OK : NATIVE_ASSET_INVALID_DATA; }
static enum NativeAssetResult Reject(void *user,const struct NativeDrawTriangle *triangle)
{ (void)user; (void)triangle; return NATIVE_ASSET_OUTPUT_TOO_SMALL; }
int main(void)
{
    u8 *asset=malloc(0xb00); CHECK(asset!=NULL); u8 ptr[4+sizeof(slots)]; Fixture(asset,ptr);
    struct NativePtrMapEntry entries[sizeof(slots)/sizeof(*slots)]; struct NativePtrMapView map;
    CHECK(NativePtrMap_Decode(asset,0xb00,ptr,sizeof(ptr),entries,sizeof(entries)/sizeof(*entries),&map)==NATIVE_PTRMAP_OK);
    struct NativeLevelView level; struct NativeMeshView mesh;
    CHECK(NativeLevel_Open(&map,&level)==NATIVE_ASSET_OK && NativeLevel_GetMesh(&level,&mesh)==NATIVE_ASSET_OK);
    struct NativeSceneCamera camera; const s32 position[3]={0}; const s16 rotation[3]={0};
    CHECK(NativeSceneCamera_Init(position,rotation,512,512,256,128,12000,&camera)==NATIVE_ASSET_OK);
    const s16 insideMin[3]={-64,-64,300},insideMax[3]={64,64,400},outsideMin[3]={1000,-64,300},outsideMax[3]={1100,64,400};
    CHECK(NativeSceneCamera_BoxVisible(&camera,insideMin,insideMax)); CHECK(!NativeSceneCamera_BoxVisible(&camera,outsideMin,outsideMax));
    u32 stack[7]; u8 states[3],quads[2]; struct NativeVisibilityWorkspace w={stack,states,quads,7,3,2}; struct NativeVisibilityResult visible;
    CHECK(NativeSceneVisibility_Select(&level,&mesh,&camera,NULL,0,&w,&visible)==NATIVE_ASSET_OK && visible.visibleQuads==1 && quads[0] && !quads[1]);
    u8 pvs[4]; CTR_WriteU32LE(pvs,0x20000000); // Only leaf 2, which is offscreen.
    CHECK(NativeSceneVisibility_Select(&level,&mesh,&camera,pvs,4,&w,&visible)==NATIVE_ASSET_OK && visible.visibleQuads==0);
    CTR_WriteU32LE(pvs,0x40000000);
    CHECK(NativeSceneVisibility_Select(&level,&mesh,&camera,pvs,4,&w,&visible)==NATIVE_ASSET_OK && visible.visibleQuads==1);
    CHECK(NativeSceneVisibility_Select(&level,&mesh,&camera,pvs,3,&w,&visible)==NATIVE_ASSET_INVALID_DATA && visible.quadMask==NULL);
    struct NativeSceneRenderStats stats; size_t triangles=0;
    CHECK(NativeSceneRender_Terrain(&level,&mesh,&camera,NULL,0,NULL,0,0,2,&w,Count,&triangles,&stats)==NATIVE_ASSET_OK && triangles==8 && stats.triangles==8);
    struct NativeSceneCamera badCamera=camera; badCamera.width=0;
    CHECK(NativeSceneRender_Terrain(&level,&mesh,&badCamera,NULL,0,NULL,0,0,2,&w,NULL,NULL,&stats)==NATIVE_ASSET_INVALID_ARGUMENT);
    Put16(asset+0x238,0xc001); // Signed-negative resident child IDs disable the branch.
    CHECK(NativeSceneVisibility_Select(&level,&mesh,&camera,NULL,0,&w,&visible)==NATIVE_ASSET_OK && visible.visibleQuads==0);
    Put16(asset+0x238,0x4001);
    struct NativeVisibilityWorkspace small=w; small.stackCapacity=1;
    CHECK(NativeSceneVisibility_Select(&level,&mesh,&camera,NULL,0,&small,&visible)==NATIVE_ASSET_OUTPUT_TOO_SMALL);
    u8 facePvs[4]={0}; CHECK(NativeSceneRender_Terrain(&level,&mesh,&camera,NULL,0,facePvs,4,0,2,&w,Count,&triangles,&stats)==NATIVE_ASSET_OK && stats.triangles==0);
    Put16(asset+0x33c,31); CTR_WriteU32LE(facePvs,1); // Wire block ID differs from quad ordinal.
    CHECK(NativeSceneRender_Terrain(&level,&mesh,&camera,NULL,0,facePvs,4,0,2,&w,NULL,NULL,&stats)==NATIVE_ASSET_OK && stats.triangles==8);
    CTR_WriteU32LE(facePvs,0x80000000);
    CHECK(NativeSceneRender_Terrain(&level,&mesh,&camera,NULL,0,facePvs,4,0,2,&w,NULL,NULL,&stats)==NATIVE_ASSET_OK && stats.triangles==0);
    Put16(asset+0x33c,0);
    CHECK(NativeSceneRender_Terrain(&level,&mesh,&camera,NULL,0,NULL,0,0,2,&w,Reject,NULL,&stats)==NATIVE_ASSET_OUTPUT_TOO_SMALL && stats.triangles==0);
    camera.transform.position[0]=1000;
    CHECK(NativeSceneVisibility_Select(&level,&mesh,&camera,NULL,0,&w,&visible)==NATIVE_ASSET_OK && !quads[0] && quads[1]); camera.transform.position[0]=0;
    Put16(asset+0x238,0);
    CHECK(NativeSceneVisibility_Select(&level,&mesh,&camera,NULL,0,&w,&visible)==NATIVE_ASSET_INVALID_DATA && visible.quadMask==NULL); Put16(asset+0x238,0x4001);
    CTR_WriteU32LE(asset+0x258,3);
    CHECK(NativeSceneVisibility_Select(&level,&mesh,&camera,NULL,0,&w,&visible)==NATIVE_ASSET_INVALID_DATA); CTR_WriteU32LE(asset+0x258,1);
    struct NativeInstanceDefView definition; struct NativeRuntimeInstance instance;
    CHECK(NativeLevel_GetInstance(&level,0,&definition)==NATIVE_ASSET_OK && NativeRuntimeInstance_Init(&definition,&instance)==NATIVE_ASSET_OK);
    struct NativeModelVertex current[3],next[3]; struct NativePackedModelVertex packed[3]; struct NativeModelDrawWorkspace modelWork={current,next,packed,3}; triangles=0;
    CHECK(NativeSceneRender_Model(&instance,&camera,&modelWork,NULL,Count,&triangles,&stats)==NATIVE_ASSET_OK && triangles==1);
    instance.position[2]=124; triangles=0;
    CHECK(NativeSceneRender_Model(&instance,&camera,&modelWork,NULL,Count,&triangles,&stats)==NATIVE_ASSET_OK && triangles==1 && stats.clipped==1);
    CHECK(NativeSceneRender_Model(&instance,&camera,&modelWork,NULL,NULL,NULL,&stats)==NATIVE_ASSET_OK && stats.triangles==1);
    instance.position[2]=300;
    camera.subdivisionDepth=1; triangles=0;
    CHECK(NativeSceneRender_Model(&instance,&camera,&modelWork,NULL,Count,&triangles,&stats)==NATIVE_ASSET_OK && triangles==4 && stats.subdivided==1);
    camera.subdivisionDepth=0;
    instance.position[0]=1000;
    CHECK(NativeSceneRender_Model(&instance,&camera,&modelWork,NULL,Count,&triangles,&stats)==NATIVE_ASSET_OK && stats.triangles==0);
    instance.flags=0x800; CHECK(NativeSceneRender_Model(&instance,&camera,&modelWork,NULL,Count,&triangles,&stats)==NATIVE_ASSET_NOT_FOUND);
    struct NativeSceneAssets owners={0}; struct NativeModelView model;
    CHECK(NativeSceneAssets_BeginRaw(&owners,asset,0xb00)==NATIVE_PTRMAP_OK);
    CHECK(NativeSceneAssets_GetLevel(&owners,asset,&level)==NATIVE_ASSET_NOT_FOUND);
    size_t pendingSize=NativeSceneAssets_CheckpointSize(&owners); u8 *pending=malloc(pendingSize); CHECK(pending!=NULL);
    CHECK(NativeSceneAssets_CaptureCheckpoint(&owners,pending,pendingSize));
    struct NativeSceneAssets pendingOwner={0}; struct SceneTestRebase pendingRebase={(uintptr_t)asset,(uintptr_t)asset,0xb00};
    CHECK(NativeSceneAssets_RestoreCheckpoint(&pendingOwner,pending,pendingSize,RebaseOwner,&pendingRebase));
    CHECK(NativeSceneAssets_GetLevel(&pendingOwner,asset,&level)==NATIVE_ASSET_NOT_FOUND);
    CHECK(NativeSceneAssets_CompletePtr(&pendingOwner,asset,ptr,sizeof(ptr))==NATIVE_PTRMAP_OK);
    CHECK(NativeSceneAssets_GetModel(&pendingOwner,asset+0x900,&model)==NATIVE_ASSET_OK);
    NativeSceneAssets_Reset(&pendingOwner); free(pending);

    CHECK(NativeSceneAssets_CompletePtr(&owners,asset,ptr,3)!=NATIVE_PTRMAP_OK);
    CHECK(NativeSceneAssets_CompletePtr(&owners,asset,ptr,sizeof(ptr))==NATIVE_PTRMAP_OK);
    CHECK(NativeSceneAssets_CompletePtr(&owners,asset,ptr,sizeof(ptr))==NATIVE_PTRMAP_INVALID_ARGUMENT);
    CHECK(NativeSceneAssets_GetModel(&owners,asset+0x900,&model)==NATIVE_ASSET_OK && model.id==17);
    CHECK(NativeModelLibrary_StoreModel(&owners.library,&model)==NATIVE_ASSET_OK);
    size_t checkpointSize=NativeSceneAssets_CheckpointSize(&owners);
    u8 *checkpoint=malloc(checkpointSize); CHECK(checkpoint!=NULL);
    CHECK(NativeSceneAssets_CaptureCheckpoint(&owners,checkpoint,checkpointSize));
    struct NativeSceneAssets restored={0};
    struct SceneTestRebase rebase={(uintptr_t)asset,(uintptr_t)asset+0x100000,0xb00};
    CHECK(NativeSceneAssets_RestoreCheckpoint(&restored,checkpoint,checkpointSize,RebaseOwner,&rebase));
    CHECK(NativeSceneAssets_GetModel(&restored,(void *)(rebase.target+0x900),&model)==NATIVE_ASSET_OK && model.id==17);
    CHECK(NativeModelLibrary_Get(&restored.library,17,&model)==NATIVE_ASSET_OK);
    CHECK(!NativeSceneAssets_RestoreCheckpoint(&restored,checkpoint,checkpointSize-1,RebaseOwner,&rebase));
    u32 savedSlot=CTR_ReadU32LE(checkpoint+checkpointSize-4); CTR_WriteU32LE(checkpoint+checkpointSize-4,0xb00);
    CHECK(!NativeSceneAssets_RestoreCheckpoint(&restored,checkpoint,checkpointSize,RebaseOwner,&rebase));
    CHECK(NativeModelLibrary_Get(&restored.library,17,&model)==NATIVE_ASSET_OK);
    CTR_WriteU32LE(checkpoint+checkpointSize-4,savedSlot);
    NativeSceneAssets_Reset(&restored); free(checkpoint);
    CTR_WriteU32LE(asset+0x914,0x12345678); // Legacy relocation must not affect the snapshot.
    CHECK(NativeSceneAssets_GetModel(&owners,asset+0x900,&model)==NATIVE_ASSET_OK && model.headersOffset==0x940);
    CHECK(NativeSceneAssets_Capture(&owners,asset,0xb00,ptr,3)!=NATIVE_PTRMAP_OK);
    CHECK(NativeSceneAssets_GetModel(&owners,asset+0x900,&model)==NATIVE_ASSET_OK);
    NativeSceneAssets_ForgetRange(&owners,asset+0x800,asset+0xb00);
    CHECK(NativeSceneAssets_GetModel(&owners,asset+0x900,&model)==NATIVE_ASSET_NOT_FOUND && NativeModelLibrary_Get(&owners.library,17,&model)==NATIVE_ASSET_NOT_FOUND);
    NativeSceneAssets_Reset(&owners); Fixture(asset,ptr);
    CHECK(NativeSceneAssets_Capture(&gNativeSceneAssets,asset,0xb00,ptr,sizeof(ptr))==NATIVE_PTRMAP_OK);
    struct GameTracker tracker={0}; struct Model *models[2]={(void *)(asset+0x900),NULL};
    LibraryOfModels_Store(&tracker,1,models);
    CHECK(NativeModelLibrary_Get(&gNativeSceneAssets.library,17,&model)==NATIVE_ASSET_OK && tracker.modelPtr[17]==models[0]);
    LibraryOfModels_Clear(&tracker);
    CHECK(NativeModelLibrary_Get(&gNativeSceneAssets.library,17,&model)==NATIVE_ASSET_NOT_FOUND && tracker.modelPtr[17]==NULL);
    struct NativeSceneGpuPacket packets[16]; u32 ot[0x400];
    for(unsigned k=0;k<0x400;k++) ot[k]=NATIVE_GPU_LINK_TERMINATOR;
    NativeGpuLinks_Reset();
    CHECK(NativeGpuLinks_RegisterRange(packets,sizeof(packets),NULL) && NativeGpuLinks_RegisterRange(ot,sizeof(ot),NULL));
    struct NativeSceneGpuSink gpu;
    CHECK(NativeSceneGpu_Init(packets,16,ot,0x400,&gpu)==NATIVE_ASSET_OK);
    struct NativeDrawTriangle triangle={.depth={128,256,192},.screen={{-1,2},{3,4},{5,6}},.orderingBias=-2};
    triangle.source.colors[0]=0x112233; triangle.source.colors[1]=0x445566; triangle.source.colors[2]=0x778899;
    CHECK(NativeSceneGpu_Emit(&gpu,&triangle)==NATIVE_ASSET_OK && gpu.count==1);
    CHECK(packets[0].words[0]==0x06ffffff && packets[0].words[1]==0x30112233 && packets[0].words[2]==0x0002ffff);
    CHECK(NativeGpuLinks_ToHostPointer(ot[2])==&packets[0]);
    triangle.source.textured=1; triangle.source.texture.clut=0x123; triangle.source.texture.tpage=0x60;
    triangle.source.texture.u[0]=7; triangle.source.texture.v[0]=9;
    CHECK(NativeSceneGpu_Emit(&gpu,&triangle)==NATIVE_ASSET_OK && packets[1].words[1]==0x34112233 && packets[1].words[3]==0x01230907);
    CHECK(NativeGpuLinks_ToHostPointer(packets[1].words[0])==&packets[0]);
    triangle.source.texture.tpage=0;
    CHECK(NativeSceneGpu_Emit(&gpu,&triangle)==NATIVE_ASSET_OK && packets[2].words[1]==0x36112233);
    gpu.count=gpu.capacity; u32 preserved=ot[2];
    CHECK(NativeSceneGpu_Emit(&gpu,&triangle)==NATIVE_ASSET_OUTPUT_TOO_SMALL && ot[2]==preserved);
    for(unsigned k=0;k<0x400;k++) ot[k]=NATIVE_GPU_LINK_TERMINATOR;
    struct PushBuffer pb={.rect={512,512},.distanceToScreen_PREV=256,.ptrOT=ot};
    for(unsigned k=0;k<3;k++) pb.matrix_ViewProj.m[k][k]=4096;
    struct PrimMem memory={.cursor=packets,.end=packets+1}; u32 visibleNodes;
    CHECK(!NativeSceneConsumer_Terrain(asset,&pb,&memory,NULL,NULL,0,&visibleNodes));
    CHECK(memory.cursor==packets && ot[6]==NATIVE_GPU_LINK_TERMINATOR);
    memory.end=packets+16;
    CHECK(NativeSceneConsumer_Terrain(asset,&pb,&memory,NULL,NULL,0,&visibleNodes));
    CHECK(memory.primitiveCount==8 && memory.cursor==packets+8 && visibleNodes==2);
    CHECK(NativeGpuLinks_ToHostPointer(ot[6])==packets+7);
    NativeGpuLinks_Reset(); gpu.count=0;
    CHECK(NativeSceneGpu_Emit(&gpu,&triangle)==NATIVE_ASSET_INVALID_ARGUMENT);
    NativeSceneAssets_Reset(&gNativeSceneAssets);
    MEMPACK_Init(0x200000);
    u8 *low=MEMPACK_AllocMem(0xb00,"scene-owner");
    Fixture(low,ptr);
    CHECK(NativeSceneAssets_Capture(&gNativeSceneAssets,low,0xb00,ptr,sizeof(ptr))==NATIVE_PTRMAP_OK);
    CHECK(NativeSceneAssets_GetModel(&gNativeSceneAssets,low+0x900,&model)==NATIVE_ASSET_OK);
    CHECK(NativeModelLibrary_StoreModel(&gNativeSceneAssets.library,&model)==NATIVE_ASSET_OK);
    MEMPACK_ReallocMem(0x800);
    CHECK(NativeSceneAssets_GetModel(&gNativeSceneAssets,low+0x900,&model)==NATIVE_ASSET_NOT_FOUND);
    CHECK(NativeModelLibrary_Get(&gNativeSceneAssets.library,17,&model)==NATIVE_ASSET_NOT_FOUND);
    MEMPACK_ClearLowMem();
    MEMPACK_PushState(); low=MEMPACK_AllocMem(0xb00,"bookmark-owner"); Fixture(low,ptr);
    CHECK(NativeSceneAssets_Capture(&gNativeSceneAssets,low,0xb00,ptr,sizeof(ptr))==NATIVE_PTRMAP_OK);
    MEMPACK_PopState();
    CHECK(NativeSceneAssets_GetLevel(&gNativeSceneAssets,low,&level)==NATIVE_ASSET_NOT_FOUND);
    u8 *high=MEMPACK_AllocHighMem(0xb00,"high-owner"); Fixture(high,ptr);
    CHECK(NativeSceneAssets_Capture(&gNativeSceneAssets,high,0xb00,ptr,sizeof(ptr))==NATIVE_PTRMAP_OK);
    MEMPACK_ClearHighMem();
    CHECK(NativeSceneAssets_GetLevel(&gNativeSceneAssets,high,&level)==NATIVE_ASSET_NOT_FOUND);
    NativeSceneAssets_Reset(&gNativeSceneAssets); free(asset);
    puts("Scene runtime ownership, camera, visibility, model/terrain sinks and game library OK"); return 0;
}
