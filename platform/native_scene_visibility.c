#include <platform/native_scene_visibility.h>
#include <string.h>
static u16 Vis_U16(const u8 *p) { return (u16)(p[0]|((u16)p[1]<<8)); }
static s16 Vis_S16(const u8 *p) { u16 n=Vis_U16(p); return (s16)(n<=INT16_MAX ? (s32)n : (s32)n-65536); }
enum NativeAssetResult NativeSceneBsp_Get(const struct NativeLevelView *level,const struct NativeMeshView *mesh,u32 index,struct NativeSceneBsp *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    if(level==NULL || level->map==NULL || mesh==NULL || (mesh->bspCount && mesh->bsp==NULL)) return NATIVE_ASSET_INVALID_ARGUMENT;
    if(index>=mesh->bspCount) return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
    const u8 *wire=mesh->bsp+(size_t)index*NATIVE_BSP_BYTES;
    struct NativeSceneBsp result={.flags=Vis_U16(wire),.id=Vis_S16(wire+2)};
    for(unsigned k=0;k<3;k++) {
        result.minimum[k]=Vis_S16(wire+4+2*k); result.maximum[k]=Vis_S16(wire+10+2*k);
        if(result.minimum[k]>result.maximum[k]) return NATIVE_ASSET_INVALID_DATA;
    }
    if(result.flags&1) {
        result.quadCount=CTR_ReadU32LE(wire+24);
        if(result.quadCount>mesh->quadCount) return NATIVE_ASSET_INVALID_DATA;
        uintptr_t base=(uintptr_t)level->map->origin,slot=(uintptr_t)(wire+28);
        if(slot<base || slot-base>UINT32_MAX) return NATIVE_ASSET_INVALID_DATA;
        void *pointer=NULL;
        enum NativePtrMapResult status=NativePtrMap_Resolve(level->map,(u32)(slot-base),(size_t)result.quadCount*NATIVE_QUAD_BYTES,&pointer);
        if(result.quadCount==0 && status==NATIVE_PTRMAP_SLOT_NOT_FOUND && CTR_ReadU32LE(wire+28)==0) { *out=result; return NATIVE_ASSET_OK; }
        uintptr_t target=(uintptr_t)pointer,quads=(uintptr_t)mesh->quads;
        if(status!=NATIVE_PTRMAP_OK || target<quads || (target-quads)%NATIVE_QUAD_BYTES || (target-quads)/NATIVE_QUAD_BYTES>mesh->quadCount) return NATIVE_ASSET_INVALID_DATA;
        result.firstQuad=(u32)((target-quads)/NATIVE_QUAD_BYTES);
        if(result.quadCount>mesh->quadCount-result.firstQuad) return NATIVE_ASSET_INVALID_DATA;
    } else {
        for(unsigned k=0;k<2;k++) {
            result.children[k]=Vis_U16(wire+24+2*k);
            // Resident RenderLists_PushChild skips every signed-negative ID,
            // including disabled 0xc000 leaves, not just the 0xffff sentinel.
            if(!(result.children[k]&0x8000) && (result.children[k]&0x3fff)>=mesh->bspCount) return NATIVE_ASSET_INVALID_DATA;
        }
    }
    *out=result; return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeSceneVisibility_Select(const struct NativeLevelView *level,const struct NativeMeshView *mesh,
    const struct NativeSceneCamera *camera,const void *pvs,size_t pvsBytes,const struct NativeVisibilityWorkspace *w,struct NativeVisibilityResult *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    if(level==NULL || mesh==NULL || !NativeSceneCamera_IsValid(camera) || w==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    if(w->quadCapacity<mesh->quadCount || w->stateCapacity<mesh->bspCount || w->stackCapacity<(size_t)mesh->bspCount*2+1) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
    if((mesh->quadCount && w->quads==NULL) || (mesh->bspCount && (w->stack==NULL || w->states==NULL))) return NATIVE_ASSET_INVALID_ARGUMENT;
    if(pvs!=NULL && pvsBytes<((size_t)mesh->bspCount+31)/32*4) return NATIVE_ASSET_INVALID_DATA;
    if(mesh->quadCount) memset(w->quads,0,mesh->quadCount);
    if(mesh->bspCount) memset(w->states,0,mesh->bspCount);
    struct NativeVisibilityResult result={.quadMask=w->quads};
    if(mesh->bspCount==0) {
        for(u32 q=0;q<mesh->quadCount;q++) {
            struct NativeMeshQuad quad; enum NativeAssetResult status=NativeMesh_GetQuad(mesh,q,&quad); if(status!=NATIVE_ASSET_OK) return status;
            s16 minimum[3]={INT16_MAX,INT16_MAX,INT16_MAX},maximum[3]={INT16_MIN,INT16_MIN,INT16_MIN};
            for(unsigned v=0;v<9;v++) {
                struct NativeMeshVertex vertex; status=NativeMesh_GetVertex(mesh,quad.indices[v],&vertex); if(status!=NATIVE_ASSET_OK) return status;
                for(unsigned k=0;k<3;k++) { if(vertex.position[k]<minimum[k]) minimum[k]=vertex.position[k]; if(vertex.position[k]>maximum[k]) maximum[k]=vertex.position[k]; }
            }
            if(NativeSceneCamera_BoxVisible(camera,minimum,maximum)) { w->quads[q]=1; result.visibleQuads++; }
        }
        *out=result; return NATIVE_ASSET_OK;
    }
    size_t count=1; w->stack[0]=0;
    while(count) {
        u32 id=w->stack[--count],index=id&0x3fff;
        if(id&0x80000000u) { w->states[index]=2; continue; }
        if(w->states[index]==1) return NATIVE_ASSET_INVALID_DATA;
        if(w->states[index]==2) continue;
        struct NativeSceneBsp node; enum NativeAssetResult status=NativeSceneBsp_Get(level,mesh,index,&node); if(status!=NATIVE_ASSET_OK) return status;
        if(pvs!=NULL && index!=0 && !(CTR_ReadU32LE((const u8 *)pvs+index/32*4)&(0x80000000u>>(index%32)))) continue;
        if(!NativeSceneCamera_BoxVisible(camera,node.minimum,node.maximum)) continue;
        result.visibleNodes++;
        if(node.flags&1) {
            w->states[index]=2;
            for(u32 q=0;q<node.quadCount;q++) { u32 at=node.firstQuad+q; if(!w->quads[at]) { w->quads[at]=1; result.visibleQuads++; } }
        } else {
            w->states[index]=1; w->stack[count++]=index|0x80000000u;
            for(unsigned k=0;k<2;k++) if(!(node.children[k]&0x8000)) w->stack[count++]=node.children[k]&0x3fff;
        }
    }
    *out=result; return NATIVE_ASSET_OK;
}
