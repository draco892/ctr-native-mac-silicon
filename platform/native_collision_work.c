#include <platform/native_collision_work.h>
#include <platform/native_host_scratch.h>
#include <ctr_camera_work.h>
#if defined(CTR_NATIVE_HOST64)
struct ScratchpadStruct *NativeCollisionWork_Get(void)
{
    void *work=NativeHostScratch_Get(NATIVE_HOST_SCRATCH_COLLISION,sizeof(struct ScratchpadStruct),_Alignof(struct ScratchpadStruct));
    if(work==NULL) CTR_TRAP();
    return work;
}
struct CameraScratchWork *NativeCameraWork_Get(void)
{
    void *work=NativeHostScratch_Get(NATIVE_HOST_SCRATCH_CAMERA,sizeof(struct CameraScratchWork),_Alignof(struct CameraScratchWork));
    if(work==NULL) CTR_TRAP();
    return work;
}
void NativeCollisionWork_VisitPointers(struct ScratchpadStruct *w,NativeCollisionPointerVisitor visit,void *user)
{
    if(w==NULL || visit==NULL) return;
#define COLL_VISIT(field) visit(user,&w->field,sizeof(w->field),0)
    COLL_VISIT(Union.ThBuckColl.thread);
    visit(user,&w->Union.ThBuckColl.funcCallback,sizeof(w->Union.ThBuckColl.funcCallback),1);
    COLL_VISIT(ptr_mesh_info); COLL_VISIT(ptr_mesh_info_2); COLL_VISIT(bspHitbox);
    COLL_VISIT(candidate.ptrQuadblock); COLL_VISIT(hit.ptrQuadblock);
    for(unsigned i=0;i<15;i++) { COLL_VISIT(bspHitboxesHit[i]); COLL_VISIT(scrubHistory[i].quadblock); }
    COLL_VISIT(hitLevelTriangle.v0); COLL_VISIT(hitLevelTriangle.v1); COLL_VISIT(hitLevelTriangle.v2);
    COLL_VISIT(hitBspSearchTriangle.v0); COLL_VISIT(hitBspSearchTriangle.v1); COLL_VISIT(hitBspSearchTriangle.v2);
    for(unsigned i=0;i<9;i++) COLL_VISIT(bspSearchVert[i].pLevelVertex);
#undef COLL_VISIT
}
void NativeCollisionWork_VisitHostPointers(NativeCollisionPointerVisitor visit,void *user)
{
    struct ScratchpadStruct *collision=NativeHostScratch_Peek(NATIVE_HOST_SCRATCH_COLLISION,sizeof(struct ScratchpadStruct));
    struct CameraScratchWork *camera=NativeHostScratch_Peek(NATIVE_HOST_SCRATCH_CAMERA,sizeof(struct CameraScratchWork));
    NativeCollisionWork_VisitPointers(collision,visit,user);
    if(camera!=NULL) NativeCollisionWork_VisitPointers(CameraScratchWork_Collision(camera),visit,user);
}
#else
struct ScratchpadStruct *NativeCollisionWork_Get(void)
{
    return NativeScratchpad_GetChecked(0x108,sizeof(struct ScratchpadStruct),_Alignof(struct ScratchpadStruct));
}
struct CameraScratchWork *NativeCameraWork_Get(void)
{
    return NativeScratchpad_GetChecked(0x108,sizeof(struct CameraScratchWork),_Alignof(struct CameraScratchWork));
}
void NativeCollisionWork_VisitPointers(struct ScratchpadStruct *w,NativeCollisionPointerVisitor visit,void *user)
{ (void)w; (void)visit; (void)user; }
void NativeCollisionWork_VisitHostPointers(NativeCollisionPointerVisitor visit,void *user)
{ (void)visit; (void)user; }
#endif
