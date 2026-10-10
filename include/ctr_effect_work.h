#ifndef CTR_EFFECT_WORK_H
#define CTR_EFFECT_WORK_H
#include <macros.h>
#include <ctr_math.h>
#include <psx/libgte.h>
struct PushBuffer;
struct Driver;
struct Instance;

enum
{
	TORCH_RING0_SCRATCH_OFFSET = 0x68,
	TORCH_RING1_SCRATCH_OFFSET = 0x8c,
	TORCH_RING2_SCRATCH_OFFSET = 0xb0,
};

struct TorchCardRegs
{
	u32 left;
	u32 right;
	u32 top;
	u32 bottom;
};

struct TorchRingScratch
{
	u32 center;
	u32 top;
	u32 topRight;
	u32 right;
	u32 bottomRight;
	u32 bottom;
	u32 bottomLeft;
	u32 left;
	u32 topLeft;
};

enum TorchRingIndex
{
	TORCH_RING_0,
	TORCH_RING_1,
	TORCH_RING_2,
};

enum TorchRingPoint
{
	TORCH_POINT_CENTER = offsetof(struct TorchRingScratch, center),
	TORCH_POINT_TOP = offsetof(struct TorchRingScratch, top),
	TORCH_POINT_TOP_RIGHT = offsetof(struct TorchRingScratch, topRight),
	TORCH_POINT_RIGHT = offsetof(struct TorchRingScratch, right),
	TORCH_POINT_BOTTOM_RIGHT = offsetof(struct TorchRingScratch, bottomRight),
	TORCH_POINT_BOTTOM = offsetof(struct TorchRingScratch, bottom),
	TORCH_POINT_BOTTOM_LEFT = offsetof(struct TorchRingScratch, bottomLeft),
	TORCH_POINT_LEFT = offsetof(struct TorchRingScratch, left),
	TORCH_POINT_TOP_LEFT = offsetof(struct TorchRingScratch, topLeft),
};

enum TorchUvSlot
{
	TORCH_UV_SLOT_0,
	TORCH_UV_SLOT_1,
	TORCH_UV_SLOT_2,
	TORCH_UV_SLOT_3,
};

struct TorchPointSource
{
	enum TorchRingIndex ring;
	enum TorchRingPoint point;
};

union TorchUvClutScratch
{
	struct
	{
		u8 u;
		u8 v;
		u16 clut;
	};
	u32 word;
};

union TorchUvTpageScratch
{
	struct
	{
		u8 u;
		u8 v;
		u16 tpage;
	};
	u32 word;
};

union TorchUvPairScratch
{
	struct
	{
		u8 u0;
		u8 v0;
		u8 u1;
		u8 v1;
	};
	u32 word;
};

// Pointer-free scalar payload. firstParticlePtr32 is reserved/unused on native.
struct TorchScratch
{
	u8 pad_000[0x30];
	u32 firstParticlePtr32;
	u32 pad_034;
	u32 swapchainIndex;
	u8 pad_03c[0x08];
	u32 color;
	u32 screenWFP;
	u32 screenHFP;
	s16 rectX;
	s16 rectYWithSwapchain;
	u16 maxX;
	u16 maxY;
	u16 tileUBase;
	u16 pad_05a;
	union TorchUvClutScratch uv0;
	union TorchUvTpageScratch uv1;
	union TorchUvPairScratch uv23;
	struct TorchRingScratch rings[3];
};

CTR_STATIC_ASSERT(sizeof(struct TorchRingScratch) == 0x24);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, firstParticlePtr32) == 0x30);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, swapchainIndex) == 0x38);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, color) == 0x44);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, screenWFP) == 0x48);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, screenHFP) == 0x4c);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, rectX) == 0x50);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, rectYWithSwapchain) == 0x52);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, maxX) == 0x54);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, maxY) == 0x56);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, tileUBase) == 0x58);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, uv0) == 0x5c);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, uv1) == 0x60);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, uv23) == 0x64);
CTR_STATIC_ASSERT(offsetof(struct TorchScratch, rings) == TORCH_RING0_SCRATCH_OFFSET);
CTR_STATIC_ASSERT(CTR_OFFSET_OF_ARRAY(struct TorchScratch, rings, 1) == TORCH_RING1_SCRATCH_OFFSET);
CTR_STATIC_ASSERT(CTR_OFFSET_OF_ARRAY(struct TorchScratch, rings, 2) == TORCH_RING2_SCRATCH_OFFSET);

struct VehGroundSkidsScratch
{
	SVECTOR projected[3];
	struct PushBuffer *pushBuffer;
	u32 colorNear;
	u32 colorFar;
	union
	{
		u32 segmentFlags;
		struct
		{
			u8 segmentFlagsLow;
			u8 segmentFlagsPadding[3];
		} bytes;
	} segment;
	u32 currXY[9];
	u32 prevXY[9];
	s32 currDepth[9];
	s32 prevDepth[9];
	Vec3 origin;
};

#if !defined(CTR_NATIVE_HOST64)
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, projected) == 0x0);
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, pushBuffer) == 0x18);
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, colorNear) == 0x1c);
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, colorFar) == 0x20);
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, segment.segmentFlags) == 0x24);
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, currXY) == 0x28);
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, prevXY) == 0x4c);
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, currDepth) == 0x70);
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, prevDepth) == 0x94);
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, origin) == 0xb8);
CTR_STATIC_ASSERT(sizeof(struct VehGroundSkidsScratch) == 0xc4);
#else
CTR_STATIC_ASSERT(sizeof(((struct VehGroundSkidsScratch *)0)->pushBuffer) == sizeof(void *));
CTR_STATIC_ASSERT(offsetof(struct VehGroundSkidsScratch, pushBuffer) % _Alignof(void *) == 0);
CTR_STATIC_ASSERT(sizeof(struct VehGroundSkidsScratch) <= 2048);
#endif

#ifdef CTR_NATIVE
// The shadow's scalar retail payload remains byte-addressed. Object pointers
// live in separate typed arrays, including the ninth sentinel entry.
struct NativeShadowWork {
    union { max_align_t alignment; u8 bytes[1024]; } payload;
    struct Driver *drivers[9];
    struct Instance *instances[9];
};
CTR_STATIC_ASSERT(sizeof(struct NativeShadowWork) <= 2048);
struct TorchScratch *NativeTorchWork_Get(void);
struct VehGroundSkidsScratch *NativeSkidWork_Get(void);
struct NativeShadowWork *NativeShadowWork_Get(void);
struct Driver **NativeShadowWork_DriverSlot(struct NativeShadowWork *work, const void *retailSlot);
struct Instance **NativeShadowWork_InstanceSlot(struct NativeShadowWork *work, const void *retailSlot);
typedef void (*NativeEffectPointerVisitor)(void *user, void *slot, u32 width, int image);
void NativeEffectWork_VisitHostPointers(NativeEffectPointerVisitor visit, void *user);
#endif
#endif
