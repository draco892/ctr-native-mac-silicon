#include <ctr_effect_work.h>
#include <ctr_scratchpad.h>
#include <platform/native_checkpoint_relocation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s at %d\n",#c,__LINE__); return 1; } } while(0)
u8 *gCTRNativeScratchpadBase;
void VehGroundSkids_Subset2(struct VehGroundSkidsScratch *, const SVECTOR *, const SVECTOR *, const SVECTOR *);
struct Relocate { struct NativeCheckpointAddressRange oldRange, liveRange; size_t slots; };
static void Rebase(void *user, void *slot, u32 width, int image)
{
    struct Relocate *ctx=user; u64 old, live;
    if(image || width!=sizeof(void *) || !NativeCheckpointSlot_Read(slot,width,&old)) abort();
    ctx->slots++;
    if(old==0) return;
    if(!NativeCheckpointRanges_Rebase(&ctx->oldRange,1,&ctx->liveRange,1,old,&live) ||
       !NativeCheckpointSlot_Write(slot,width,live)) abort();
}
int main(void)
{
    _Alignas(max_align_t) u8 retail[1024]; memset(retail,0xa5,sizeof(retail)); gCTRNativeScratchpadBase=retail;
    NativeHostScratch_Reset();
    struct TorchScratch *torch=NativeTorchWork_Get();
    struct VehGroundSkidsScratch *skids=NativeSkidWork_Get();
    struct NativeShadowWork *shadow=NativeShadowWork_Get();
    CHECK(torch && skids && shadow);
    CHECK((uintptr_t)skids%_Alignof(struct VehGroundSkidsScratch)==0);
    memset(shadow->payload.bytes,0x5a,sizeof(shadow->payload.bytes));
    u8 *old=malloc(256),*live=malloc(256); CHECK(old && live);
    skids->pushBuffer=(struct PushBuffer *)(old+16);
	shadow->ot = (u32 *)(old + 32);
	for(size_t i=0;i<9;i++) {
        struct Driver **driver=NativeShadowWork_DriverSlot(shadow,shadow->payload.bytes+0xb8+i*0x28);
        struct Instance **instance=NativeShadowWork_InstanceSlot(shadow,shadow->payload.bytes+0xbc+i*0x28);
        CHECK(driver==&shadow->drivers[i] && instance==&shadow->instances[i]);
        *driver=i==8 ? NULL : (struct Driver *)(old+i*8);
        *instance=i==8 ? NULL : (struct Instance *)(old+128+i*8);
    }
    for(size_t i=0;i<sizeof(shadow->payload.bytes);i++) CHECK(shadow->payload.bytes[i]==0x5a);
    torch->rings[2].bottom=0x12345678; torch->screenWFP=0x87654321;
    skids->origin=(Vec3){.x=32767,.y=-32768,.z=65535};
    SVECTOR vertices[3]={{.vx=-32768,.vy=32767,.vz=0},{.vx=0,.vy=0,.vz=-1},{.vx=3,.vy=4,.vz=5}};
    VehGroundSkids_Subset2(skids,&vertices[0],&vertices[1],&vertices[2]);
    CHECK(skids->projected[0].vx==4 && skids->projected[0].vy==-4 && skids->projected[0].vz==4);
    CHECK(skids->projected[1].vx==4 && skids->projected[1].vy==0 && skids->projected[1].vz==0);
    CHECK(skids->projected[2].vx==16 && skids->projected[2].vy==16 && skids->projected[2].vz==24);
    size_t bytes=NativeHostScratch_StorageSize(); void *snapshot=malloc(bytes); CHECK(snapshot);
    memcpy(snapshot,NativeHostScratch_Storage(),bytes); NativeHostScratch_Reset();
    memcpy(NativeHostScratch_Storage(),snapshot,bytes);
    struct Relocate rebase={{.kind=1,.start=(u64)(uintptr_t)old,.size=256},{.kind=1,.start=(u64)(uintptr_t)live,.size=256},0};
    NativeEffectWork_VisitHostPointers(Rebase,&rebase);
	CHECK(rebase.slots == 20 && skids->pushBuffer == (struct PushBuffer *)(live + 16));
	CHECK(shadow->ot == (u32 *)(live + 32));
	for(size_t i=0;i<8;i++) CHECK(shadow->drivers[i]==(struct Driver *)(live+i*8) && shadow->instances[i]==(struct Instance *)(live+128+i*8));
    CHECK(shadow->drivers[8]==NULL && shadow->instances[8]==NULL);
    CHECK(torch->rings[2].bottom==0x12345678 && torch->screenWFP==0x87654321);
    for(size_t i=0;i<sizeof(retail);i++) CHECK(retail[i]==0xa5);
    free(snapshot); free(old); free(live);
    puts("Effect workspaces: typed shadow/skid pointers, scalar isolation, skid wraparound and checkpoint relocation OK");
    return 0;
}
