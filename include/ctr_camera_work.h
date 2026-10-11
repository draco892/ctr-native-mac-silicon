#ifndef CTR_CAMERA_WORK_H
#define CTR_CAMERA_WORK_H
#include <namespace_Coll.h>
// Camera scratchpad overlay rooted at retail scratchpad 0x1f800108.
// Camera-owned fields begin at work+0x20c, absolute scratchpad 0x1f800314.
struct CameraCollisionScratch
{
#if defined(CTR_NATIVE_HOST64)
	struct ScratchpadStruct search;
#else
	u8 pad_000[0x1e];
	s16 terrainHeightFloor;
	u8 pad_020[0x20c - 0x20];
#endif
};

#if !defined(CTR_NATIVE_HOST64)
CTR_STATIC_ASSERT(offsetof(struct CameraCollisionScratch, terrainHeightFloor) == 0x01e);
CTR_STATIC_ASSERT(sizeof(struct CameraCollisionScratch) == 0x20c);
#endif

struct CameraAngleAxisScratchCamera
{
	SVec3 rot;     // +0x00 (abs 0x314)
	s16 _pad0;     // +0x06
	Vec3 posCopy;  // +0x08 (abs 0x31C) s32 copies of pos below
	MATRIX matrix; // +0x14 (abs 0x328)
	Vec3 pos;      // +0x34 (abs 0x348)
	Vec3 dir;      // +0x40 (abs 0x354) written by CAM_LookAtPosition
};

CTR_STATIC_ASSERT(offsetof(struct CameraAngleAxisScratchCamera, rot) == 0x00);
CTR_STATIC_ASSERT(offsetof(struct CameraAngleAxisScratchCamera, posCopy) == 0x08);
CTR_STATIC_ASSERT(offsetof(struct CameraAngleAxisScratchCamera, matrix) == 0x14);
CTR_STATIC_ASSERT(offsetof(struct CameraAngleAxisScratchCamera, pos) == 0x34);
CTR_STATIC_ASSERT(offsetof(struct CameraAngleAxisScratchCamera, dir) == 0x40);
CTR_STATIC_ASSERT(sizeof(struct CameraAngleAxisScratchCamera) == 0x4c);

struct CameraScratch
{
	SVec3 rot;
	s16 _pad0;
	Vec3 posCopy;
	MATRIX matrix;
	Vec3 pos;
	Vec3 dir;

	Vec3 delta; // +0x4C (abs 0x360)
};

CTR_STATIC_ASSERT(offsetof(struct CameraScratch, rot) == 0x00);
CTR_STATIC_ASSERT(offsetof(struct CameraScratch, posCopy) == 0x08);
CTR_STATIC_ASSERT(offsetof(struct CameraScratch, matrix) == 0x14);
CTR_STATIC_ASSERT(offsetof(struct CameraScratch, pos) == 0x34);
CTR_STATIC_ASSERT(offsetof(struct CameraScratch, dir) == 0x40);
CTR_STATIC_ASSERT(offsetof(struct CameraScratch, delta) == 0x4C);
CTR_STATIC_ASSERT(sizeof(struct CameraScratch) == 0x58);

struct CameraAngleAxisScratch
{
	struct CameraCollisionScratch collision;
#if defined(CTR_NATIVE_HOST64)
	struct CameraScratch camera;
#else
	struct CameraAngleAxisScratchCamera camera;
#endif
};

CTR_STATIC_ASSERT(offsetof(struct CameraAngleAxisScratch, collision) == 0x0);
#if !defined(CTR_NATIVE_HOST64)
CTR_STATIC_ASSERT(offsetof(struct CameraAngleAxisScratch, collision.terrainHeightFloor) == 0x01e);
CTR_STATIC_ASSERT(offsetof(struct CameraAngleAxisScratch, camera) == 0x20c);
CTR_STATIC_ASSERT(sizeof(struct CameraAngleAxisScratch) == 0x258);
#endif

struct CameraScratchWork
{
#if defined(CTR_NATIVE_HOST64)
	// Both union members have an identical common initial sequence.
	union {
		struct { struct CameraCollisionScratch collision; struct CameraScratch camera; };
		struct CameraAngleAxisScratch angleAxis;
	};
#else
	struct CameraCollisionScratch collision;
	struct CameraScratch camera;
#endif

	u8 pad_264[0x18];
	Vec3 sideOffset;
	SVec3 trackPathPos;
	s16 pad_28e;
	SVec3 trackPathLookaheadPos;
};

CTR_STATIC_ASSERT(offsetof(struct CameraScratchWork, collision) == 0x0);
#if !defined(CTR_NATIVE_HOST64)
CTR_STATIC_ASSERT(offsetof(struct CameraScratchWork, collision.terrainHeightFloor) == 0x01e);
CTR_STATIC_ASSERT(offsetof(struct CameraScratchWork, camera) == 0x20c);
CTR_STATIC_ASSERT(offsetof(struct CameraScratchWork, sideOffset) == 0x27c);
CTR_STATIC_ASSERT(offsetof(struct CameraScratchWork, trackPathPos) == 0x288);
CTR_STATIC_ASSERT(offsetof(struct CameraScratchWork, trackPathLookaheadPos) == 0x290);
#else
CTR_STATIC_ASSERT(sizeof(struct CameraScratchWork) <= 2048);
CTR_STATIC_ASSERT(offsetof(struct CameraScratchWork, camera) >= sizeof(struct CameraCollisionScratch));
#endif

static inline struct CameraAngleAxisScratch *CameraScratchWork_AsAngleAxis(struct CameraScratchWork *work)
{
#if defined(CTR_NATIVE_HOST64)
	return &work->angleAxis;
#else
	return (struct CameraAngleAxisScratch *)work;
#endif
}

static inline struct ScratchpadStruct *CameraScratchWork_Collision(struct CameraScratchWork *work)
{
#if defined(CTR_NATIVE_HOST64)
    return &work->collision.search;
#else
    return (struct ScratchpadStruct *)work;
#endif
}
static inline s16 *CameraScratchWork_TerrainHeight(struct CameraScratchWork *work)
{
#if defined(CTR_NATIVE_HOST64)
    return &work->collision.search.Union.QuadBlockColl.hitPos.y;
#else
    return &work->collision.terrainHeightFloor;
#endif
}

#endif
