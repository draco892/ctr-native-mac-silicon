#ifndef NATIVE_SCENE_SNAPSHOT_GAME_H
#define NATIVE_SCENE_SNAPSHOT_GAME_H
#include <stdint.h>
struct GameTracker;
struct NativeSceneGameSnapshotStats { unsigned objects,sources,triangles,matched,unsupported; int regenerated,fallback; };
void NativeSceneGameSnapshot_Begin(struct GameTracker *,int enabled,uint64_t signature);
void NativeSceneGameSnapshot_Seal(void);
uint32_t *NativeSceneGameSnapshot_Presentation(struct GameTracker *,float alpha);
struct NativeSceneGameSnapshotStats NativeSceneGameSnapshot_GetStats(void);
#endif
