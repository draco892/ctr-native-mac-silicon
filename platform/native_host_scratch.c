#include <platform/native_host_scratch.h>
#include <ctr_scratchpad.h>
#include <string.h>
enum { NATIVE_HOST_SCRATCH_BYTES=2048 };
union NativeHostScratchSlot { max_align_t alignment; u8 bytes[NATIVE_HOST_SCRATCH_BYTES]; };
static struct { u32 sizes[NATIVE_HOST_SCRATCH_COUNT]; union NativeHostScratchSlot slots[NATIVE_HOST_SCRATCH_COUNT]; } hostScratch;
void NativeHostScratch_Reset(void) { memset(&hostScratch,0,sizeof(hostScratch)); }
void *NativeHostScratch_Storage(void) { return &hostScratch; }
size_t NativeHostScratch_StorageSize(void) { return sizeof(hostScratch); }
void *NativeHostScratch_Get(enum NativeHostScratchKey key,size_t bytes,size_t alignment)
{
    if((unsigned)key>=NATIVE_HOST_SCRATCH_COUNT || !bytes || bytes>NATIVE_HOST_SCRATCH_BYTES ||
       !alignment || (alignment&(alignment-1)) || alignment>_Alignof(union NativeHostScratchSlot)) return NULL;
    if(hostScratch.sizes[key] && hostScratch.sizes[key]!=bytes) return NULL;
    hostScratch.sizes[key]=(u32)bytes;
    return hostScratch.slots[key].bytes;
}
void *NativeHostScratch_Peek(enum NativeHostScratchKey key,size_t bytes)
{
    if((unsigned)key>=NATIVE_HOST_SCRATCH_COUNT || hostScratch.sizes[key]!=bytes) return NULL;
    return hostScratch.slots[key].bytes;
}
void *NativeScratchpad_At(size_t offset,size_t bytes,size_t alignment)
{
    if(gCTRNativeScratchpadBase==NULL || offset>CTR_SCRATCHPAD_SIZE || bytes>CTR_SCRATCHPAD_SIZE-offset ||
       !alignment || (alignment&(alignment-1)) || ((uintptr_t)gCTRNativeScratchpadBase+offset)%alignment) return NULL;
    return gCTRNativeScratchpadBase+offset;
}

void *NativeScratchpad_GetChecked(size_t offset,size_t bytes,size_t alignment)
{
    void *pointer=NativeScratchpad_At(offset,bytes,alignment);
    if(pointer==NULL) CTR_TRAP();
    return pointer;
}
