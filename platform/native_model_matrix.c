#include <platform/native_model_matrix.h>
#include <string.h>

static s32 NativeMatrix_S32(u32 n)
{
	return n <= INT32_MAX ? (s32)n : (s32)((s64)n - 4294967296LL);
}
static s16 NativeMatrix_S16(u32 n)
{
	n &= 0xffffu;
	return (s16)(n <= INT16_MAX ? (s32)n : (s32)n - 65536);
}
static s64 NativeMatrix_Floor(s64 n, s64 divisor)
{
	return n / divisor - (n % divisor < 0);
}
static s16 NativeMatrix_Clamp(s64 n)
{
	return n < INT16_MIN ? INT16_MIN : n > INT16_MAX ? INT16_MAX : (s16)n;
}
enum NativeAssetResult NativeModelMatrix_Compose(const struct NativeModelMatrix *left,
    const struct NativeModelMatrix *right, struct NativeModelMatrix *out)
{
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (left == NULL || right == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	for (unsigned row = 0; row < 3; row++) for (unsigned col = 0; col < 3; col++)
	{
		s64 dot = 0;
		for (unsigned k = 0; k < 3; k++) dot += (s64)left->m[row][k] * right->m[k][col];
		out->m[row][col] = NativeMatrix_Clamp(NativeMatrix_Floor(dot, 4096));
	}
	return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeModelMatrix_Build(const struct NativeModelMatrix *rotation,
    const s16 modelScale[3], const s16 instanceScale[3], s32 viewDepth,
    int pixelLod, struct NativeModelMatrix *out)
{
	struct NativeModelMatrix diagonal = {0};
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (rotation == NULL || modelScale == NULL || instanceScale == NULL || (pixelLod != 0 && pixelLod != 1))
		return NATIVE_ASSET_INVALID_ARGUMENT;
	// Retail branches on the signed result of a wrapping MIPS subtraction.
	unsigned shift = NativeMatrix_S32((u32)viewDepth - 4096u) < 0 ? 0 : 2;
	for (unsigned axis = 0; axis < 3; axis++)
	{
		s16 scale = instanceScale[axis];
		if (pixelLod)
		{
			u32 pixelScale = (u32)NativeMatrix_Floor(viewDepth, 2) + 4096u;
			u32 product = pixelScale * (u32)(s32)scale;
			s64 scaled = NativeMatrix_Floor(NativeMatrix_S32(product), 4096);
			scale = NativeMatrix_S16((u32)scaled);
		}
		s16 coefficient = NativeMatrix_S16((u16)modelScale[axis] >> shift);
		diagonal.m[axis][axis] = NativeMatrix_Clamp(NativeMatrix_Floor((s64)coefficient * scale, 4096));
	}
	return NativeModelMatrix_Compose(rotation, &diagonal, out);
}
enum NativeAssetResult NativeModelMatrix_Apply(const struct NativeModelMatrix *matrix,
    const struct NativePackedModelVertex *vertex, struct NativeMatrixVector *out)
{
	s16 position[3];
	if (out == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (matrix == NULL || vertex == NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	enum NativeAssetResult status = NativeModel_UnpackPosition(vertex, position);
	if (status != NATIVE_ASSET_OK) return status;
	for (unsigned row = 0; row < 3; row++)
	{
		s64 dot = 0;
		for (unsigned k = 0; k < 3; k++) dot += (s64)matrix->m[row][k] * position[k];
		out->mac[row] = (s32)NativeMatrix_Floor(dot, 4096);
		out->ir[row] = NativeMatrix_Clamp(out->mac[row]);
		if (out->mac[row] != out->ir[row]) out->saturated |= (u8)(1u << row);
	}
	return NATIVE_ASSET_OK;
}
