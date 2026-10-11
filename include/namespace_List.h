#ifndef CTR_NATIVE_NAMESPACE_LIST_H
#define CTR_NATIVE_NAMESPACE_LIST_H

// NOTE(aalhendi): Pool objects also use this two-pointer prefix as their intrusive list link.
struct CTR_MAY_ALIAS Item
{
	// 0x0
	struct Item *next;

	// 0x4
	struct Item *prev;
};

struct LinkedList
{
	// 0x0
	struct Item *first;

	// 0x4
	struct Item *last;

	// 0x8
	s32 count;
};

CTR_STATIC_ASSERT(OFFSETOF(struct Item, next) == 0x0);
CTR_STATIC_ASSERT(OFFSETOF(struct Item, prev) == sizeof(void *));
CTR_STATIC_ASSERT(sizeof(struct Item) == 2 * sizeof(void *));
CTR_STATIC_ASSERT(OFFSETOF(struct LinkedList, first) == 0x0);
CTR_STATIC_ASSERT(OFFSETOF(struct LinkedList, last) == sizeof(void *));
CTR_STATIC_ASSERT(OFFSETOF(struct LinkedList, count) == 2 * sizeof(void *));
#if !defined(CTR_NATIVE_HOST64)
CTR_STATIC_ASSERT(sizeof(struct LinkedList) == 0xC);
#endif

#endif
