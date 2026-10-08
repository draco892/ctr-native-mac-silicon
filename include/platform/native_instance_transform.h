#ifndef PLATFORM_NATIVE_INSTANCE_TRANSFORM_H
#define PLATFORM_NATIVE_INSTANCE_TRANSFORM_H
#include <platform/native_model_projection.h>
// Pointer-free camera values; angle units are 4096 per turn, not degrees.
struct NativeInstanceCamera {
    struct NativeModelMatrix view;
    s32 position[3], offset[2], dqb;
    u16 h; s16 dqa;
};
// Matches ConvertRotToMatrix: shared game trig table, Y * X * Z, Q12 floor.
// Borrowed input and output must be disjoint; output clears on error.
enum NativeAssetResult NativeInstance_Rotation(const s16 angles[3],struct NativeModelMatrix *out);
// Normal instance bridge: decoded wire rotation/scale/position -> projection.
// Handles PIXEL_LOD, SCREENSPACE_INSTANCE and DRAW_HUGE. Other flags remain
// metadata; no custom/billboard matrices, visibility, reflection, animation
// timing or instance mutation. SCREENSPACE bypasses translation rotation only;
// the normal matrix still uses the supplied camera view.
// Near/far translation and coefficient scale are selected by raw camera depth.
// Camera/instance/outputs must be disjoint. No allocation or global GTE state.
enum NativeAssetResult NativeInstance_Projection(const struct NativeInstanceDefView *instance,
    u32 headerIndex,const struct NativeInstanceCamera *camera,
    struct NativeProjectionConfig *out,s32 *rawDepth);
#endif
