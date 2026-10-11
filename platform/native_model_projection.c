/* Projection/divisor derived from native_gte_core.c (REDRIVER2/PsyCross MIT).
 * See THIRD_PARTY_NOTICES.md for copyright and license details. */
#include <platform/native_model_projection.h>
#include <string.h>

static s64 NativeProjection_Floor(s64 n, s64 divisor) { return n / divisor - (n % divisor < 0); }
static s32 NativeProjection_S32(u32 n) { return n <= INT32_MAX ? (s32)n : (s32)((s64)n - 4294967296LL); }
static s16 NativeProjection_S16(u32 n)
{
	n &= 65535u; return (s16)(n <= INT16_MAX ? (s32)n : (s32)n - 65536);
}
static s32 NativeProjection_Clamp(s64 n, s32 low, s32 high, u32 flag, u32 *flags)
{
	if (n < low) { *flags |= flag; return low; }
	if (n > high) { *flags |= flag; return high; }
	return (s32)n;
}
static void NativeProjection_Mac0(s64 n, u32 *flags)
{
	if (n > INT32_MAX) *flags |= 0x80010000u;
	if (n < INT32_MIN) *flags |= 0x80008000u;
}
static u32 NativeProjection_Divide(u16 h, u16 z, u32 *flags)
{
	if (h >= (u32)z * 2u) { *flags |= 0x80020000u; return 0x1ffffu; }
	static const u8 table[] = {
		    0xff, 0xfd, 0xfb, 0xf9, 0xf7, 0xf5, 0xf3, 0xf1, 0xef, 0xee, 0xec, 0xea, 0xe8, 0xe6, 0xe4, 0xe3, 0xe1, 0xdf, 0xdd, 0xdc, 0xda, 0xd8, 0xd6, 0xd5,
		    0xd3, 0xd1, 0xd0, 0xce, 0xcd, 0xcb, 0xc9, 0xc8, 0xc6, 0xc5, 0xc3, 0xc1, 0xc0, 0xbe, 0xbd, 0xbb, 0xba, 0xb8, 0xb7, 0xb5, 0xb4, 0xb2, 0xb1, 0xb0,
		    0xae, 0xad, 0xab, 0xaa, 0xa9, 0xa7, 0xa6, 0xa4, 0xa3, 0xa2, 0xa0, 0x9f, 0x9e, 0x9c, 0x9b, 0x9a, 0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90,
		    0x8f, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x7f, 0x7e, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x77, 0x75, 0x74,
		    0x73, 0x72, 0x71, 0x70, 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61, 0x60, 0x5f, 0x5e, 0x5d, 0x5d,
		    0x5c, 0x5b, 0x5a, 0x59, 0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4f, 0x4e, 0x4d, 0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x48, 0x48,
		    0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3f, 0x3f, 0x3e, 0x3d, 0x3c, 0x3c, 0x3b, 0x3a, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35,
		    0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2f, 0x2e, 0x2e, 0x2d, 0x2c, 0x2c, 0x2b, 0x2a, 0x2a, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
		    0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a, 0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15,
		    0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11, 0x10, 0x0f, 0x0f, 0x0e, 0x0e, 0x0d, 0x0d, 0x0c, 0x0c, 0x0b, 0x0a, 0x0a, 0x09, 0x09, 0x08, 0x08,
		    0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00};
	unsigned shift = 0;
	while (((u32)z << shift) < 0x8000u) shift++;
	u32 r1 = ((u32)z << shift) & 0x7fffu;
	u32 r2 = table[(r1 + 0x40u) / 128u] + 0x101u;
	s64 correction = 0x80 - (s64)r2 * (r1 + 0x8000u);
	u32 r3 = (u32)NativeProjection_Floor(correction, 256) & 0x1ffffu;
	u32 reciprocal = (r2 * r3 + 0x80u) / 256u;
	u32 result = (u32)(((u64)reciprocal * ((u32)h << shift) + 0x8000u) / 65536u);
	return result > 0x1ffffu ? 0x1ffffu : result;
}
static void NativeProjection_One(const struct NativeProjectionConfig *config,
    const struct NativePackedModelVertex *vertex, struct NativeProjectionState *state,
    struct NativeProjectionResult *out)
{
	s16 position[3]; s64 unshifted[3];
	NativeModel_UnpackPosition(vertex, position);
	for (unsigned axis = 0; axis < 3; axis++)
	{
		s64 sum = (s64)config->translation[axis] * 4096;
		for (unsigned k = 0; k < 3; k++) sum += (s64)config->rotation.m[axis][k] * position[k];
		unshifted[axis] = sum;
		// Preserve the current native core's asymmetric bounds and truncation.
		if (sum > 0x7ffffffffffLL) out->flags |= 0x80000000u | (1u << (30 - axis));
		if (sum < -0x8000000000LL) out->flags |= 0x80000000u | (1u << (27 - axis));
		out->mac[axis] = NativeProjection_S32((u32)NativeProjection_Floor(sum, 4096));
		u32 irFlag = axis == 0 ? 0x81000000u : axis == 1 ? 0x80800000u : 0x00400000u;
		out->ir[axis] = (s16)NativeProjection_Clamp(out->mac[axis], -32768, 32767, irFlag, &out->flags);
	}
	for (unsigned i = 0; i < 3; i++) state->depth[i] = state->depth[i + 1];
	state->depth[3] = (u16)NativeProjection_Clamp(
	    NativeProjection_S32((u32)NativeProjection_Floor(unshifted[2], 4096)), 0, 65535, 0x80040000u, &out->flags);
	out->ratio = NativeProjection_Divide(config->h, state->depth[3], &out->flags);
	for (unsigned i = 0; i < 2; i++) memcpy(state->screen[i], state->screen[i + 1], sizeof(state->screen[i]));
	for (unsigned axis = 0; axis < 2; axis++)
	{
		s64 screen = (s64)config->offset[axis] + (s64)out->ir[axis] * out->ratio;
		NativeProjection_Mac0(screen, &out->flags);
		state->screen[2][axis] = (s16)NativeProjection_Clamp(NativeProjection_Floor(screen, 65536),
		    -1024, 1023, axis == 0 ? 0x80004000u : 0x80002000u, &out->flags);
	}
}
static enum NativeAssetResult NativeProjection_Run(const struct NativeProjectionConfig *config,
    const struct NativePackedModelVertex *vertices, unsigned count, struct NativeProjectionState *state,
    struct NativeProjectionResult *out)
{
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (config == NULL || vertices == NULL || state == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	struct NativeProjectionState pending = *state;
	struct NativeProjectionResult result = {0};
	for (unsigned i = 0; i < count; i++) NativeProjection_One(config, &vertices[i], &pending, &result);
	s64 depthCue = (s64)config->dqb + (s64)config->dqa * result.ratio;
	NativeProjection_Mac0(depthCue, &result.flags);
	result.mac0 = NativeProjection_S32((u32)depthCue);
	// Native Lm_H sets bit12 alone; retain its depth-cue saturation semantics.
	s64 cue = NativeProjection_Floor(depthCue, 4096);
	if (cue < 0 || cue > 4096) result.flags |= 0x1000u;
	result.ir0 = (u16)(cue < 0 ? 0 : cue > 4096 ? 4096 : cue);
	*state = pending; *out = result;
	return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeModelProjection_Project(const struct NativeProjectionConfig *config,
    const struct NativePackedModelVertex *vertex, struct NativeProjectionState *state,
    struct NativeProjectionResult *out)
{ return NativeProjection_Run(config, vertex, 1, state, out); }
enum NativeAssetResult NativeModelProjection_Project3(const struct NativeProjectionConfig *config,
    const struct NativePackedModelVertex vertices[3], struct NativeProjectionState *state,
    struct NativeProjectionResult *out)
{ return NativeProjection_Run(config, vertices, 3, state, out); }
enum NativeAssetResult NativeModelProjection_ViewTranslation(const struct NativeModelMatrix *view,
    const s32 instance[3], const s32 camera[3], int screenspace, int drawHuge,
    s32 out[3], s32 *rawDepth)
{
	if (out == NULL || rawDepth == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, 3 * sizeof(*out)); *rawDepth = 0;
	if (instance == NULL || (screenspace != 0 && screenspace != 1) || (drawHuge != 0 && drawHuge != 1) ||
	    (!screenspace && (view == NULL || camera == NULL))) return NATIVE_ASSET_INVALID_ARGUMENT;
	if (screenspace) memcpy(out, instance, 3 * sizeof(*out));
	else
	{
		s16 relative[3];
		for (unsigned i = 0; i < 3; i++) relative[i] = NativeProjection_S16((u32)instance[i] - (u32)camera[i]);
		for (unsigned row = 0; row < 3; row++)
		{
			s64 dot = 0;
			for (unsigned k = 0; k < 3; k++) dot += (s64)view->m[row][k] * relative[k];
			u32 unused = 0;
			out[row] = NativeProjection_Clamp(NativeProjection_Floor(dot, 4096), -32768, 32767, 0, &unused);
		}
	}
	*rawDepth = out[2];
	if (NativeProjection_S32((u32)out[2] - 4096u) < 0)
		for (unsigned i = 0; i < 3; i++) out[i] = NativeProjection_S32((u32)out[i] << 2);
	if (drawHuge) for (unsigned i = 0; i < 3; i++) out[i] = (s32)NativeProjection_Floor(out[i], 4);
	return NATIVE_ASSET_OK;
}
