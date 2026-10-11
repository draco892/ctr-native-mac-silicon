#include <ctr_camera_work.h>
#include <ctr_scratchpad.h>
#include <platform/native_checkpoint_relocation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s at %d\n",#c,__LINE__); return 1; } } while(0)
u8 *gCTRNativeScratchpadBase;
// Compile the production traversal and scrub routines against the fields they
// consume. The full game aggregates are still guarded; this does not certify
// their wire layouts or execution of the vehicle/camera loop.
#define COMMON_H
typedef s16 BspChildId;
enum { BSP_CHILD_ID_INDEX_MASK=0x3fff,BSP_CHILD_ID_LEAF_FLAG=0x4000,BSP_CHILD_ID_NONE=0xffff };
struct BSP { struct BoundingBox box; union { struct { BspChildId childID[2]; } branch; } data; };
struct mesh_info { struct BSP *bspRoot; s32 numBspNodes; };
#include "../game/COLL_SearchBSP.c"
#include "../game/COLL_ScrubHistory.c"
static struct BSP nodes[5];
static unsigned order[16],visited;
static int nested;
static void Leaf(struct BSP *node,struct ScratchpadStruct *sps)
{
    order[visited++]=(unsigned)(node-nodes);
    if(!nested) {
        nested=1;
        struct ScratchpadStruct local=*sps;
        COLL_SearchBSP_CallbackPARAM(nodes,&local.bbox,Leaf,&local);
    }
}
static void Hit(struct ScratchpadStruct *work,void *object) { (void)work; (void)object; }
struct Rebase { struct NativeCheckpointAddressRange before,after; size_t rebased,images; };
static void RebaseSlot(void *user,void *slot,u32 width,int image)
{
    struct Rebase *ctx=user; u64 old,live;
    if(image) { ctx->images++; return; }
    if(NativeCheckpointSlot_Read(slot,width,&old) && NativeCheckpointRanges_Rebase(&ctx->before,1,&ctx->after,1,old,&live)) {
        if(!NativeCheckpointSlot_Write(slot,width,live)) abort();
        ctx->rebased++;
    }
}
int main(void)
{
    _Alignas(max_align_t) u8 retail[CTR_SCRATCHPAD_SIZE]; memset(retail,0xa5,sizeof(retail)); gCTRNativeScratchpadBase=retail;
    NativeHostScratch_Reset();
    struct ScratchpadStruct *w=CTR_COLLISION_WORK(); struct CameraScratchWork *c=CTR_CAMERA_WORK();
    CHECK(w!=NULL && c!=NULL && w!=CameraScratchWork_Collision(c));
    CHECK((uintptr_t)w%_Alignof(struct ScratchpadStruct)==0 && (uintptr_t)c%_Alignof(struct CameraScratchWork)==0);
    c->camera.pos.x=123; c->camera.matrix.t[1]=456;
    *CameraScratchWork_TerrainHeight(c)=789;
    CHECK(CameraScratchWork_Collision(c)->Union.QuadBlockColl.hitPos.y==789);
    struct CameraAngleAxisScratch *angle=CameraScratchWork_AsAngleAxis(c);
    angle->camera.pos.y=17; angle->camera.dir.z=19;
    CHECK(c->camera.pos.y==17 && c->camera.dir.z==19 && c->camera.pos.x==123);
    w->Union.ThBuckColl.funcCallback=Hit;
    w->Union.ThBuckColl.thread=(struct Thread *)c;
    w->Union.QuadBlockColl.searchFlags=COLL_SEARCH_HIGH_LOD;
    w->Union.QuadBlockColl.hitPos.y=-77;
    CHECK(w->Union.ThBuckColl.funcCallback==Hit && w->Union.ThBuckColl.thread==(struct Thread *)c);
    // Moving all collision fields cannot overwrite the adjacent camera state.
    memset(CameraScratchWork_Collision(c),0,sizeof(*w));
    CHECK(c->camera.pos.x==123 && c->camera.pos.y==17 && c->camera.matrix.t[1]==456);
    for(unsigned i=0;i<CTR_SCRATCHPAD_SIZE;i++) CHECK(retail[i]==0xa5);
    struct BoundingBox box={{-10,-10,-10},{10,10,10}};
    for(unsigned i=0;i<5;i++) nodes[i].box=box;
    nodes[0].data.branch.childID[0]=1; nodes[0].data.branch.childID[1]=0x4004;
    nodes[1].data.branch.childID[0]=0x4002; nodes[1].data.branch.childID[1]=0x4003;
    struct mesh_info mesh={nodes,5}; w->ptr_mesh_info=&mesh; w->bbox=box;
    COLL_SearchBSP_CallbackPARAM(nodes,&w->bbox,Leaf,w);
    CHECK(visited==6 && order[0]==4 && order[1]==4 && order[2]==3 && order[3]==2 && order[4]==3 && order[5]==2);
    visited=0; nested=1; nodes[4].box.min.x=20; nodes[4].box.max.x=30;
    COLL_SearchBSP_CallbackPARAM(nodes,&w->bbox,Leaf,w); CHECK(visited==2 && order[0]==3 && order[1]==2);
    u8 quads[15];
    COLL_MOVED_FindScrub(NULL,0,w); CHECK(w->scrubCount==0);
    for(unsigned i=0;i<15;i++) COLL_MOVED_FindScrub((struct QuadBlock *)&quads[i],(s32)i,w);
    CHECK(w->scrubCount==15);
    for(unsigned i=0;i<6;i++) COLL_MOVED_FindScrub((struct QuadBlock *)&quads[7],7,w);
    CHECK(w->scrubCount==15 && w->Input1.scrubDepth==0x500 && (w->Union.QuadBlockColl.searchFlags&COLL_SEARCH_REPEAT_SCRUB));
    COLL_MOVED_FindScrub(NULL,0,w); CHECK(w->scrubCount==0 && !(w->Union.QuadBlockColl.searchFlags&COLL_SEARCH_REPEAT_SCRUB));
    u8 *old=malloc(128),*live=malloc(128); CHECK(old!=NULL && live!=NULL);
    w->ptr_mesh_info=(struct mesh_info *)old; w->hit.ptrQuadblock=(struct QuadBlock *)(old+16);
    w->bspSearchVert[4].pLevelVertex=(struct LevVertex *)(old+32);
    w->hitBspSearchTriangle.v0=&w->bspSearchVert[4];
    w->scrubHistory[2].quadblock=(struct QuadBlock *)(old+48);
    CameraScratchWork_Collision(c)->candidate.ptrQuadblock=(struct QuadBlock *)(old+64);
    size_t bytes=NativeHostScratch_StorageSize(); void *snapshot=malloc(bytes); CHECK(snapshot!=NULL);
    memcpy(snapshot,NativeHostScratch_Storage(),bytes); NativeHostScratch_Reset(); memcpy(NativeHostScratch_Storage(),snapshot,bytes);
    struct Rebase ctx={{1,128,(u64)(uintptr_t)old},{1,128,(u64)(uintptr_t)live},0,0};
    NativeCollisionWork_VisitHostPointers(RebaseSlot,&ctx);
    CHECK(ctx.rebased==5 && ctx.images==2 && w->Union.ThBuckColl.funcCallback==Hit);
    CHECK(w->hit.ptrQuadblock==(struct QuadBlock *)(live+16) && w->scrubHistory[2].quadblock==(struct QuadBlock *)(live+48));
    CHECK(w->hitBspSearchTriangle.v0==&w->bspSearchVert[4]);
    CHECK(CameraScratchWork_Collision(c)->candidate.ptrQuadblock==(struct QuadBlock *)(live+64));
    CHECK(c->camera.pos.x==123 && c->camera.matrix.t[1]==456);
    free(snapshot); free(live); free(old);
    puts("ARM64 collision/camera workspaces, nested BSP searches, scrub history and checkpoint slots OK"); return 0;
}
