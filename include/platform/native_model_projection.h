#ifndef PLATFORM_NATIVE_MODEL_PROJECTION_H
#define PLATFORM_NATIVE_MODEL_PROJECTION_H
#include <platform/native_model_matrix.h>

struct NativeProjectionConfig
{
	struct NativeModelMatrix rotation;
	s32 translation[3], offset[2], dqb; // offset is Q16, translation integer.
	u16 h;
	s16 dqa;
};
struct NativeProjectionState { s16 screen[3][2]; u16 depth[4]; };
struct NativeProjectionResult
{
	s32 mac[3], mac0;
	s16 ir[3];
	u16 ir0;
	u32 ratio, flags;
};
// Current native GTE RTPS/RTPT compatibility, SF=1 LM=0. Explicit FIFO state;
// no globals, pointers retained, allocation or rasterization. NOT a rejection
// test: behind-camera/overflow points return OK with saturated values/flags.
// Config, input, state and output must not overlap. Errors clear output and
// preserve state. Project3 accumulates flags and leaves last vertex registers.
// Known native-core accumulator/FLAG quirks are preserved, not corrected here.
enum NativeAssetResult NativeModelProjection_Project(const struct NativeProjectionConfig *config,
    const struct NativePackedModelVertex *vertex, struct NativeProjectionState *state,
    struct NativeProjectionResult *out);
enum NativeAssetResult NativeModelProjection_Project3(const struct NativeProjectionConfig *config,
    const struct NativePackedModelVertex vertices[3], struct NativeProjectionState *state,
    struct NativeProjectionResult *out);
// Camera-relative translation follows GetViewPosition + AdjustViewPositionForMvp:
// wrapping subtract, signed low16 inputs, Q12 view rotate/IR clamp, near <<2,
// then optional DRAW_HUGE arithmetic >>2. screenspace bypasses camera rotation.
// rawDepth is before the near/huge adjustment and can drive matrix Build/LOD.
enum NativeAssetResult NativeModelProjection_ViewTranslation(const struct NativeModelMatrix *view,
    const s32 instance[3], const s32 camera[3], int screenspace, int drawHuge,
    s32 out[3], s32 *rawDepth);
#endif
