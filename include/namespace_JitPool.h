#ifndef CTR_NATIVE_NAMESPACE_JITPOOL_H
#define CTR_NATIVE_NAMESPACE_JITPOOL_H

struct JitPool
{
	// 0x0
	struct LinkedList free;

	// 0xc
	struct LinkedList taken;

	// 0x18
	s32 maxItems;

	// 0x1c
	u32 itemSize;

	// 0x20
	s32 poolSize;

	// 0x24
	void *ptrPoolData;
};

CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, free) == 0x0);
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, taken) == CTR_RUNTIME_ABI(0xc, 0x18));
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, maxItems) == CTR_RUNTIME_ABI(0x18, 0x30));
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, itemSize) == CTR_RUNTIME_ABI(0x1c, 0x34));
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, poolSize) == CTR_RUNTIME_ABI(0x20, 0x38));
CTR_STATIC_ASSERT(OFFSETOF(struct JitPool, ptrPoolData) == CTR_RUNTIME_ABI(0x24, 0x40));
CTR_STATIC_ASSERT(sizeof(struct JitPool) == CTR_RUNTIME_ABI(0x28, 0x48));

#endif
