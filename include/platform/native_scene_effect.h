#ifndef NATIVE_SCENE_EFFECT_H
#define NATIVE_SCENE_EFFECT_H
#include <stdint.h>
/* Copied eye-space sources in scene world units. No packet/asset pointers. */
enum NativeSceneEffectFlags {
    NATIVE_EFFECT_DIRECTIONAL=1, /* Sky: camera rotation, no parallax. */
    NATIVE_EFFECT_BILLBOARD=2,   /* Sample world centre, keep eye-space axes. */
    NATIVE_EFFECT_STAGED=8,      /* Candidate captured before visibility rejection. */
    NATIVE_EFFECT_FIXED_BUCKET=4
};
struct NativeSceneEffect {
    uint64_t id,anchor;
    unsigned kind,viewport,count,topology,flags,code;
    int bucket;
    float eye[4][3];
    uint32_t colors[4];
    uint8_t u[4],v[4];
    uint16_t page,clut;
};
#endif
