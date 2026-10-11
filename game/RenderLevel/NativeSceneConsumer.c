#include <common.h>
#include <platform/native_scene_game.h>
#include <platform/native_scene_assets.h>
#include <platform/native_scene_gpu.h>
#include <stdlib.h>
#include <string.h>

int NativeSceneConsumer_Terrain(const void *legacyLevel,const struct PushBuffer *pb,
    struct PrimMem *memory,const void *leafPvs,const void *facePvs,u32 tick,u32 *visibleNodes)
{
    if(visibleNodes!=NULL) *visibleNodes=0;
    if(pb==NULL || memory==NULL || pb->rect.w<=0 || pb->rect.h<=0 || pb->distanceToScreen_PREV<=0 || pb->distanceToScreen_PREV>65535) return 0;
    struct NativeLevelView level; struct NativeMeshView mesh;
    if(NativeSceneAssets_GetLevel(&gNativeSceneAssets,legacyLevel,&level)!=NATIVE_ASSET_OK || NativeLevel_GetMesh(&level,&mesh)!=NATIVE_ASSET_OK) return 0;
    struct NativeSceneCamera camera={.width=(u32)pb->rect.w,.height=(u32)pb->rect.h,.nearDepth=pb->distanceToScreen_PREV/2+1,.farDepth=65535};
    camera.transform.h=(u16)pb->distanceToScreen_PREV;
    for(unsigned row=0;row<3;row++) {
        camera.transform.position[row]=pb->matrix_Camera.t[row];
        for(unsigned col=0;col<3;col++) camera.transform.view.m[row][col]=pb->matrix_ViewProj.m[row][col];
    }
    camera.transform.offset[0]=(pb->rect.w/2)*65536; camera.transform.offset[1]=(pb->rect.h/2)*65536;
    size_t stackCount=(size_t)mesh.bspCount*2+1;
    if(stackCount>SIZE_MAX/sizeof(u32)) return 0;
    struct NativeVisibilityWorkspace w={.stackCapacity=stackCount,.stateCapacity=mesh.bspCount,.quadCapacity=mesh.quadCount};
    w.stack=malloc(stackCount*sizeof(*w.stack)); w.states=calloc(mesh.bspCount ? mesh.bspCount : 1,1); w.quads=calloc(mesh.quadCount ? mesh.quadCount : 1,1);
    int success=0;
    if(w.stack==NULL || w.states==NULL || w.quads==NULL) goto done;
    size_t leafBytes=((size_t)mesh.bspCount+31)/32*4,faceBytes=((size_t)mesh.quadCount+31)/32*4;
    struct NativeSceneRenderStats stats;
    if(NativeSceneRender_Terrain(&level,&mesh,&camera,leafPvs,leafBytes,facePvs,faceBytes,tick,UINT32_MAX,&w,NULL,NULL,&stats)!=NATIVE_ASSET_OK) goto done;
    uintptr_t begin=(uintptr_t)memory->cursor,end=(uintptr_t)memory->end;
    if(begin>end || stats.triangles>(end-begin)/sizeof(struct NativeSceneGpuPacket) || stats.triangles>(size_t)INT32_MAX || memory->primitiveCount<0 || stats.triangles>(size_t)(INT32_MAX-memory->primitiveCount)) goto done;
    if(stats.triangles==0) { if(visibleNodes!=NULL) *visibleNodes=stats.visibleNodes; success=1; goto done; }
    struct NativeSceneGpuSink sink;
    if(NativeSceneGpu_Init(memory->cursor,stats.triangles,pb->ptrOT,0x400,&sink)!=NATIVE_ASSET_OK) goto done;
    if(NativeSceneRender_Terrain(&level,&mesh,&camera,leafPvs,leafBytes,facePvs,faceBytes,tick,UINT32_MAX,&w,NativeSceneGpu_Emit,&sink,&stats)!=NATIVE_ASSET_OK)
    { Platform_LogError("[CTR Native] Decoded terrain changed after preflight\n"); CTR_TRAP(); }
    memory->cursor=(u8 *)memory->cursor+sink.count*sizeof(struct NativeSceneGpuPacket);
    memory->primitiveCount+=(s32)sink.count;
    if(visibleNodes!=NULL) *visibleNodes=stats.visibleNodes;
    success=1;
done:
    free(w.stack); free(w.states); free(w.quads); return success;
}
