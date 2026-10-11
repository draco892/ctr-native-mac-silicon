#include <platform/native_checkpoint_relocation.h>
#include <platform/native_checkpoint_file.h>
#include <platform/native_host_scratch.h>
#include <ctr_scratchpad.h>
#include <psx/libetc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s at %d\n",#c,__LINE__); return 1; } } while(0)
u8 *gCTRNativeScratchpadBase;
static int calls;
static void CallbackA(void) { calls++; }
static void CallbackB(void) { calls+=2; }
int main(int argc,char **argv)
{
    CHECK(argc==2);
    u8 *old=malloc(128),*live=malloc(128); CHECK(old!=NULL && live!=NULL);
    struct NativeCheckpointAddressRange before[]={{1,128,(u64)(uintptr_t)old}},after[]={{1,128,(u64)(uintptr_t)live}};
#if UINTPTR_MAX>UINT32_MAX
    CHECK((uintptr_t)old>UINT32_MAX && (uintptr_t)live>UINT32_MAX);
#endif
    CHECK(NativeCheckpointRanges_Validate(before,1));
    u64 address=0; CHECK(NativeCheckpointRanges_Rebase(before,1,after,1,(u64)(uintptr_t)(old+71),&address));
    CHECK(address==(u64)(uintptr_t)(live+71));
    CHECK(NativeCheckpointRanges_Rebase(before,1,after,1,(u64)(uintptr_t)(old+128),&address) && address==(u64)(uintptr_t)(live+128));
    CHECK(!NativeCheckpointRanges_Rebase(before,1,after,1,(u64)(uintptr_t)(old+128)+1,&address));
    struct NativeCheckpointAddressRange bad[]={{1,128,0x1000},{2,128,0x1040}};
    CHECK(!NativeCheckpointRanges_Validate(bad,2)); bad[1].start=0x2000; bad[1].kind=1;
    CHECK(!NativeCheckpointRanges_Validate(bad,2)); bad[1].kind=2; bad[1].start=UINT64_MAX-63;
    CHECK(!NativeCheckpointRanges_Validate(bad,2));
    struct NativeCheckpointAddressRange adjacent[]={{1,16,0x1000},{2,16,0x1010}};
    u32 index,offset; CHECK(NativeCheckpointRanges_Owner(adjacent,2,0x1010,&index,&offset) && index==1 && offset==0);
    struct NativeCheckpointPointerSlotRecord slots[]={{1,0,4,0},{1,8,8,0}};
    CHECK(NativeCheckpointSlots_Validate(before,1,slots,2));
    slots[1].slotOffset=3; CHECK(!NativeCheckpointSlots_Validate(before,1,slots,2));
    slots[1].slotOffset=124; CHECK(!NativeCheckpointSlots_Validate(before,1,slots,2));
    slots[1].slotOffset=8; slots[1].width=6; CHECK(!NativeCheckpointSlots_Validate(before,1,slots,2));
    slots[1].width=8; slots[1].slotRegion=99; CHECK(!NativeCheckpointSlots_Validate(before,1,slots,2));
    slots[1].slotRegion=1; slots[1].reserved=1; CHECK(!NativeCheckpointSlots_Validate(before,1,slots,2));
    u8 unaligned[16]; memset(unaligned,0xa5,sizeof(unaligned));
    CHECK(NativeCheckpointSlot_Write(unaligned+1,8,0x1234567887654321ULL));
    CHECK(NativeCheckpointSlot_Read(unaligned+1,8,&address) && address==0x1234567887654321ULL);
    CHECK(unaligned[0]==0xa5 && unaligned[9]==0xa5);
    CHECK(!NativeCheckpointSlot_Write(unaligned+1,4,0x100000000ULL));
    CHECK(NativeCheckpointSlot_Write(unaligned+1,4,0x87654321u));
    CHECK(NativeCheckpointSlot_Read(unaligned+1,4,&address) && address==0x87654321u);
    CHECK(!NativeCheckpointSlot_Read(unaligned,6,&address));
    CHECK(NativeCheckpointImage_Rebase(0x100000030ULL,0x100000000ULL,0x200000000ULL,8,&address) && address==0x200000030ULL);
    CHECK(NativeCheckpointImage_Rebase(0x200000030ULL,0x200000000ULL,0x100000000ULL,8,&address) && address==0x100000030ULL);
    CHECK(NativeCheckpointImage_Rebase(UINT64_MAX,0x1000,0x2000,8,&address) && address==UINT64_MAX);
    CHECK(NativeCheckpointImage_Rebase(UINT32_MAX-1,0x1000,0x2000,4,&address) && address==UINT32_MAX-1);
    CHECK(!NativeCheckpointImage_Rebase(UINT64_MAX-3,1,8,8,&address));
    CHECK(!NativeCheckpointImage_Rebase(1,8,1,8,&address));
    CHECK(VSyncCallback(CallbackA)==NULL && VSyncCallback(CallbackB)==CallbackA);
    vsync_callback(); CHECK(calls==2 && ResetCallback()==CallbackB && vsync_callback==NULL);
    CtrCallbackArg transported=(CtrCallbackArg)CallbackA;
    CHECK((CtrCallbackArg)(NativeVSyncCallback)(CtrCallbackArg)-2==-2);
    CHECK((NativeVSyncCallback)transported==CallbackA && sizeof(CtrCallbackArg)==sizeof(void *));
    _Alignas(max_align_t) u8 retail[CTR_SCRATCHPAD_SIZE]; memset(retail,0x5a,sizeof(retail)); gCTRNativeScratchpadBase=retail;
    CHECK(NativeScratchpad_At(0x108,8,8)==retail+0x108);
    CHECK(NativeScratchpad_At(0x3fc,8,4)==NULL && NativeScratchpad_At(1,4,4)==NULL);
    CHECK(NativeScratchpad_At(CTR_SCRATCHPAD_SIZE,0,1)==retail+CTR_SCRATCHPAD_SIZE);
    NativeHostScratch_Reset();
    struct Work { void *pointer; u32 flags; };
    struct Work *work=NativeHostScratch_Get(NATIVE_HOST_SCRATCH_RENDER_BUCKET,sizeof(*work),_Alignof(struct Work));
    CHECK(work!=NULL && (uintptr_t)work%_Alignof(struct Work)==0); work->pointer=old; work->flags=17;
    void *other=NativeHostScratch_Get(NATIVE_HOST_SCRATCH_PARTICLES,sizeof(*work),_Alignof(struct Work));
    CHECK(other!=NULL && other!=work && NativeHostScratch_Get(NATIVE_HOST_SCRATCH_RENDER_BUCKET,sizeof(*work),_Alignof(struct Work))==work);
    CHECK(NativeHostScratch_Get(NATIVE_HOST_SCRATCH_RENDER_BUCKET,sizeof(*work)+1,1)==NULL);
    CHECK(retail[0]==0x5a && retail[CTR_SCRATCHPAD_SIZE-1]==0x5a);
    size_t hostBytes=NativeHostScratch_StorageSize(); void *hostCopy=malloc(hostBytes); CHECK(hostCopy!=NULL);
    memcpy(hostCopy,NativeHostScratch_Storage(),hostBytes); NativeHostScratch_Reset();
    memcpy(NativeHostScratch_Storage(),hostCopy,hostBytes); CHECK(work->pointer==old && work->flags==17); free(hostCopy);
    // The production CTST container persists the full-width relocation payload.
    struct NativeCheckpointFileWriter writer; struct NativeCheckpointFileRecordInfo records[2],info;
    CHECK(NativeCheckpointFile_BeginWrite(&writer,argv[1]));
    CHECK(NativeCheckpointFile_AppendRecord(&writer,before,sizeof(before),0,11,NULL));
    CHECK(NativeCheckpointFile_AppendRecord(&writer,after,sizeof(after),1,22,NULL));
    CHECK(NativeCheckpointFile_EndWrite(&writer));
    int count; CHECK(NativeCheckpointFile_Validate(argv[1],records,2,&count) && count==2);
    struct NativeCheckpointAddressRange loaded;
    CHECK(NativeCheckpointFile_ReadRecord(argv[1],1,&loaded,sizeof(loaded),&info));
    CHECK(loaded.start==after[0].start && info.replayFrame==22);
    FILE *file=fopen(argv[1],"r+b"); CHECK(file!=NULL && fseek(file,records[0].payloadOffset,SEEK_SET)==0);
    CHECK(fputc(0xff,file)!=EOF && fclose(file)==0);
    CHECK(!NativeCheckpointFile_Validate(argv[1],records,2,&count));
    CHECK(remove(argv[1])==0);
    free(old); free(live); puts("64-bit checkpoint relocation, container, callbacks and host scratch OK"); return 0;
}
