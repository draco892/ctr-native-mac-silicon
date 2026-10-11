#include <common.h>

s32 MATH_VectorNormalize(SVec3 *vector)
{
	s32 length = MATH_VectorLength(vector);

	if (length != 0)
	{
		vector->x = CTR_MipsSll(vector->x, 12) / length;
		vector->y = CTR_MipsSll(vector->y, 12) / length;
		vector->z = CTR_MipsSll(vector->z, 12) / length;
	}

	return length;
}
