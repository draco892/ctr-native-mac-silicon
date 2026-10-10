#include <platform/native_checkpoint_relocation.h>
#include <string.h>
#include <stdlib.h>
int NativeCheckpointRanges_Validate(const struct NativeCheckpointAddressRange *ranges,size_t count)
{
    if(ranges==NULL && count) return 0;
    for(size_t i=0;i<count;i++) {
        const struct NativeCheckpointAddressRange *r=&ranges[i];
        if(!r->kind || !r->size || !r->start || r->start>UINT64_MAX-r->size) return 0;
        for(size_t j=0;j<i;j++) {
            const struct NativeCheckpointAddressRange *other=&ranges[j];
            if(r->kind==other->kind || (r->start<other->start+other->size && other->start<r->start+r->size)) return 0;
        }
    }
    return 1;
}
int NativeCheckpointRanges_Owner(const struct NativeCheckpointAddressRange *ranges,size_t count,u64 address,u32 *index,u32 *offset)
{
    if(index==NULL || offset==NULL || count>UINT32_MAX || !NativeCheckpointRanges_Validate(ranges,count)) return 0;
    // Prefer an actual owner to the previous range's one-past-end sentinel.
    for(unsigned end=0;end<2;end++) for(size_t i=0;i<count;i++) {
        u64 relative=address-ranges[i].start;
        if(address>=ranges[i].start && (end ? relative==ranges[i].size : relative<ranges[i].size)) {
            *index=(u32)i; *offset=(u32)relative; return 1;
        }
    }
    return 0;
}
int NativeCheckpointRanges_Rebase(const struct NativeCheckpointAddressRange *oldRanges,size_t oldCount,
    const struct NativeCheckpointAddressRange *liveRanges,size_t liveCount,u64 address,u64 *out)
{
    u32 index,offset;
    if(out==NULL || !NativeCheckpointRanges_Validate(liveRanges,liveCount) ||
       !NativeCheckpointRanges_Owner(oldRanges,oldCount,address,&index,&offset)) return 0;
    for(size_t i=0;i<liveCount;i++) if(liveRanges[i].kind==oldRanges[index].kind && offset<=liveRanges[i].size) {
        *out=liveRanges[i].start+offset; return 1;
    }
    return 0;
}
int NativeCheckpointSlot_Read(const void *slot,u32 width,u64 *out)
{
    if(slot==NULL || out==NULL) return 0;
    if(width==4) { u32 value; memcpy(&value,slot,4); *out=value; return 1; }
    if(width==8) { memcpy(out,slot,8); return 1; }
    return 0;
}
int NativeCheckpointSlot_Write(void *slot,u32 width,u64 address)
{
    if(slot==NULL) return 0;
    if(width==4 && address<=UINT32_MAX) { u32 value=(u32)address; memcpy(slot,&value,4); return 1; }
    if(width==8) { memcpy(slot,&address,8); return 1; }
    return 0;
}
int NativeCheckpointImage_Rebase(u64 address,u64 oldAnchor,u64 liveAnchor,u32 width,u64 *out)
{
    if(out==NULL || !oldAnchor || !liveAnchor || (width!=4 && width!=8)) return 0;
    u64 maximum=width==4 ? UINT32_MAX : UINT64_MAX;
    if(address>maximum) return 0;
    if(address==0 || address==maximum || address==maximum-1) { *out=address; return 1; }
    if(liveAnchor>=oldAnchor) {
        u64 delta=liveAnchor-oldAnchor;
        if(delta>maximum-address) return 0;
        *out=address+delta;
    } else {
        u64 delta=oldAnchor-liveAnchor;
        if(delta>address) return 0;
        *out=address-delta;
    }
    return 1;
}

static int CheckpointSlots_Compare(const void *lhs,const void *rhs)
{
    const struct NativeCheckpointPointerSlotRecord *a=lhs,*b=rhs;
    if(a->slotRegion!=b->slotRegion) return a->slotRegion<b->slotRegion ? -1 : 1;
    return a->slotOffset<b->slotOffset ? -1 : a->slotOffset>b->slotOffset;
}
int NativeCheckpointSlots_Validate(const struct NativeCheckpointAddressRange *ranges,size_t rangeCount,
    const struct NativeCheckpointPointerSlotRecord *slots,size_t slotCount)
{
    if((slots==NULL && slotCount) || slotCount>65536 || !NativeCheckpointRanges_Validate(ranges,rangeCount)) return 0;
    struct NativeCheckpointPointerSlotRecord *sorted=malloc((slotCount ? slotCount : 1)*sizeof(*sorted));
    if(sorted==NULL) return 0;
    if(slotCount) memcpy(sorted,slots,slotCount*sizeof(*sorted));
    qsort(sorted,slotCount,sizeof(*sorted),CheckpointSlots_Compare);
    int valid=1;
    for(size_t i=0;i<slotCount;i++) {
        const struct NativeCheckpointPointerSlotRecord *r=&sorted[i];
        const struct NativeCheckpointAddressRange *owner=NULL;
        for(size_t k=0;k<rangeCount;k++) if(ranges[k].kind==r->slotRegion) { owner=&ranges[k]; break; }
        if(owner==NULL || (r->width!=4 && r->width!=8) || r->reserved || r->slotOffset>owner->size || r->width>owner->size-r->slotOffset ||
           (i && sorted[i-1].slotRegion==r->slotRegion && r->slotOffset<sorted[i-1].slotOffset+sorted[i-1].width)) { valid=0; break; }
    }
    free(sorted); return valid;
}
