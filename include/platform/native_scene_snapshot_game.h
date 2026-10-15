#ifndef NATIVE_SCENE_SNAPSHOT_GAME_H
#define NATIVE_SCENE_SNAPSHOT_GAME_H
#include <stdint.h>
struct GameTracker;
enum NativeSceneCanonicalPoseReason { NATIVE_SCENE_CANONICAL_MODEL=1,NATIVE_SCENE_CANONICAL_EFFECT=2,NATIVE_SCENE_CANONICAL_GLOW=4,NATIVE_SCENE_CANONICAL_OFFSCREEN=8,NATIVE_SCENE_CANONICAL_SCRIPTED_INTRO=16 };
struct NativeSceneGameSnapshotStats { unsigned objects,sources,triangles,matched,unsupported,effects,effectFallback,retainedModelPackets,canonicalPoseReasons; int regenerated,fallback,currentPose; };
void NativeSceneGameSnapshot_Begin(struct GameTracker *,int enabled,uint64_t signature);
void NativeSceneGameSnapshot_Seal(void);
uint32_t *NativeSceneGameSnapshot_Presentation(struct GameTracker *,float alpha);
struct NativeSceneGameSnapshotStats NativeSceneGameSnapshot_GetStats(void);
#endif
