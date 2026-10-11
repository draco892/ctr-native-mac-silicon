#include <ctr_effect_work.h>
#include <platform/native_host_scratch.h>

static void *EffectWork_Get(enum NativeHostScratchKey key, size_t bytes, size_t alignment)
{
    void *work = NativeHostScratch_Get(key, bytes, alignment);
    if(work == NULL) CTR_TRAP();
    return work;
}
struct TorchScratch *NativeTorchWork_Get(void)
{ return EffectWork_Get(NATIVE_HOST_SCRATCH_TORCH, sizeof(struct TorchScratch), _Alignof(struct TorchScratch)); }
struct VehGroundSkidsScratch *NativeSkidWork_Get(void)
{ return EffectWork_Get(NATIVE_HOST_SCRATCH_SKIDS, sizeof(struct VehGroundSkidsScratch), _Alignof(struct VehGroundSkidsScratch)); }
struct NativeShadowWork *NativeShadowWork_Get(void)
{ return EffectWork_Get(NATIVE_HOST_SCRATCH_SHADOW, sizeof(struct NativeShadowWork), _Alignof(struct NativeShadowWork)); }

static size_t ShadowWork_Index(const struct NativeShadowWork *work, const void *slot, size_t first)
{
    if(work == NULL || slot == NULL) CTR_TRAP();
    uintptr_t base = (uintptr_t)work->payload.bytes, at = (uintptr_t)slot;
    if(at < base || at - base < first) CTR_TRAP();
    size_t offset = at - base - first;
    if(offset % 0x28 || offset / 0x28 >= 9) CTR_TRAP();
    return offset / 0x28;
}
struct Driver **NativeShadowWork_DriverSlot(struct NativeShadowWork *work, const void *slot)
{ return &work->drivers[ShadowWork_Index(work, slot, 0xb8)]; }
struct Instance **NativeShadowWork_InstanceSlot(struct NativeShadowWork *work, const void *slot)
{ return &work->instances[ShadowWork_Index(work, slot, 0xbc)]; }

void NativeEffectWork_VisitHostPointers(NativeEffectPointerVisitor visit, void *user)
{
#if defined(CTR_NATIVE_HOST64)
    if(visit == NULL) return;
    struct VehGroundSkidsScratch *skids = NativeHostScratch_Peek(NATIVE_HOST_SCRATCH_SKIDS, sizeof(struct VehGroundSkidsScratch));
    struct NativeShadowWork *shadow = NativeHostScratch_Peek(NATIVE_HOST_SCRATCH_SHADOW, sizeof(struct NativeShadowWork));
    if(skids != NULL) visit(user, &skids->pushBuffer, sizeof(skids->pushBuffer), 0);
	if (shadow != NULL)
		visit(user, &shadow->ot, sizeof(shadow->ot), 0);
	if(shadow != NULL) for(size_t i = 0; i < 9; i++) {
        visit(user, &shadow->drivers[i], sizeof(shadow->drivers[i]), 0);
        visit(user, &shadow->instances[i], sizeof(shadow->instances[i]), 0);
    }
#else
    (void)visit; (void)user;
#endif
}
