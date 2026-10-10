#ifndef PLATFORM_NATIVE_SCENE_GAME_H
#define PLATFORM_NATIVE_SCENE_GAME_H
#include <macros.h>
struct PushBuffer; struct PrimMem;
// Legacy runtime adapter, deliberately separate from wire decoders. Returns 0
// before publication if no immutable owner or insufficient primitive capacity.
// Experimental path includes native clipping and immutable water/scenery
// animation. Retail subdivision dispatch, env-map/reflections, split-screen
// and lighting parity remain unverified. Enable explicitly in CMake.
int NativeSceneConsumer_Terrain(const void *legacyLevel,const struct PushBuffer *pushBuffer,
    struct PrimMem *memory,const void *leafPvs,const void *facePvs,u32 tick,u32 *visibleNodes);
#endif
