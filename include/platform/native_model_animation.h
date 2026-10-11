#ifndef PLATFORM_NATIVE_MODEL_ANIMATION_H
#define PLATFORM_NATIVE_MODEL_ANIMATION_H

#include <platform/native_asset_readers.h>

enum { NATIVE_ANIMATION_BYTES = 0x18, NATIVE_FRAME_BYTES = 0x1c };

struct NativeAnimationView
{
	const struct NativePtrMapView *map;
	u32 offset, framesOffset, storedFrameCount;
	u16 logicalFrameCount, frameStride;
	int interpolated, hasDelta;
	char name[17];
};

struct NativeFrameView
{
	s16 position[3];
	const u8 *wire, *vertices;
	size_t vertexBytes;
};

struct NativeFrameSelection
{
	struct NativeFrameView current, next;
	u32 logicalIndex, storedIndex;
	int hasNext;
};

// Views borrow an unchanged decoded map and asset. Reopen after reload/Rebind.
// Scalars are LE; no wire pointer is cast to a host structure. Outputs clear
// on error. Optional unlisted-zero animation/frame/delta slots return NOT_FOUND.
// Animation opens validate the full pointer table and stored frame span; frame
// access checks the frame-local vertex offset against the entire frame stride.
enum NativeAssetResult NativeModel_GetAnimation(const struct NativeModelView *model,
    u32 headerIndex, u32 animationIndex, struct NativeAnimationView *out);
enum NativeAssetResult NativeAnimation_GetStoredFrame(const struct NativeAnimationView *animation,
    u32 index, struct NativeFrameView *out);
// High bit of numFrames denotes half-rate stored frames. Stored count is
// floor(logicalCount/2)+1, including the interpolation endpoint. Logical
// requests clamp before selecting current/next; no time advancement is implied.
enum NativeAssetResult NativeAnimation_SelectFrame(const struct NativeAnimationView *animation,
    u32 logicalIndex, struct NativeFrameSelection *out);
// Static frame length is not serialized. Caller supplies the vertex bytes it
// will consume; only that span is validated/exposed, never a guessed length.
enum NativeAssetResult NativeModel_GetStaticFrame(const struct NativeModelView *model,
    u32 headerIndex, size_t vertexBytes, struct NativeFrameView *out);
// Compression metadata remains wire bytes. Each requested LE delta word is
// independently bounds-checked; opening an animation checks only the first word.
enum NativeAssetResult NativeAnimation_ReadDeltaWord(const struct NativeAnimationView *animation,
    u32 vertexIndex, u32 *out);
enum NativeAssetResult NativeModel_ReadStaticDeltaWord(const struct NativeModelView *model,
    u32 headerIndex, u32 vertexIndex, u32 *out);

#endif
