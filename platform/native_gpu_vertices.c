#include <platform/native_gpu_vertices.h>
#include <platform/native_visual.h>
#include <assert.h>
#include <string.h>

// Draw offsets belong to the current GPU environment, not historical packets.
void DrawEnvOffset(float *x,float *y);

void MakeLineArray(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1)
{
	const VERTTYPE dx = p1[0] - p0[0];
	const VERTTYPE dy = p1[1] - p0[1];

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 4);

	float x0=p0[0],y0=p0[1],x1=p1[0],y1=p1[1];
	NativeVisual_SampleXY(p0,&x0,&y0); NativeVisual_SampleXY(p1,&x1,&y1);
	if (dx > abs((s16)dy))
	{ // horizontal
		vertex[0].x = x0 + ofsX;
		vertex[0].y = y0 + ofsY;

		vertex[1].x = x1 + ofsX + 1;
		vertex[1].y = y1 + ofsY;

		vertex[2].x = vertex[1].x;
		vertex[2].y = vertex[1].y + 1;

		vertex[3].x = vertex[0].x;
		vertex[3].y = vertex[0].y + 1;
	}
	else
	{ // vertical
		vertex[0].x = x0 + ofsX;
		vertex[0].y = y0 + ofsY;

		vertex[1].x = x1 + ofsX;
		vertex[1].y = y1 + ofsY + 1;

		vertex[2].x = vertex[1].x + 1;
		vertex[2].y = vertex[1].y;

		vertex[3].x = vertex[0].x + 1;
		vertex[3].y = vertex[0].y;
	} // TODO diagonal line alignment
}

void MakeVertexTriangle(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1, VERTTYPE *p2)
{
	assert(p0);
	assert(p1);
	assert(p2);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 3);

	float visualX0 = p0[0], visualY0 = p0[1];
	NativeVisual_SampleXY(p0, &visualX0, &visualY0);
	vertex[0].x = visualX0 + ofsX;
	vertex[0].y = visualY0 + ofsY;

	float visualX1 = p1[0], visualY1 = p1[1];
	NativeVisual_SampleXY(p1, &visualX1, &visualY1);
	vertex[1].x = visualX1 + ofsX;
	vertex[1].y = visualY1 + ofsY;

	float visualX2 = p2[0], visualY2 = p2[1];
	NativeVisual_SampleXY(p2, &visualX2, &visualY2);
	vertex[2].x = visualX2 + ofsX;
	vertex[2].y = visualY2 + ofsY;
}

void MakeVertexQuad(GrVertex *vertex, VERTTYPE *p0, VERTTYPE *p1, VERTTYPE *p2, VERTTYPE *p3)
{
	assert(p0);
	assert(p1);
	assert(p2);
	assert(p3);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 4);

	float visualX0 = p0[0], visualY0 = p0[1];
	NativeVisual_SampleXY(p0, &visualX0, &visualY0);
	vertex[0].x = visualX0 + ofsX;
	vertex[0].y = visualY0 + ofsY;

	float visualX1 = p1[0], visualY1 = p1[1];
	NativeVisual_SampleXY(p1, &visualX1, &visualY1);
	vertex[1].x = visualX1 + ofsX;
	vertex[1].y = visualY1 + ofsY;

	float visualX2 = p2[0], visualY2 = p2[1];
	NativeVisual_SampleXY(p2, &visualX2, &visualY2);
	vertex[2].x = visualX2 + ofsX;
	vertex[2].y = visualY2 + ofsY;

	float visualX3 = p3[0], visualY3 = p3[1];
	NativeVisual_SampleXY(p3, &visualX3, &visualY3);
	vertex[3].x = visualX3 + ofsX;
	vertex[3].y = visualY3 + ofsY;
}

void MakeVertexRect(GrVertex *vertex, VERTTYPE *p0, s16 w, s16 h)
{
	assert(p0);

	float ofsX, ofsY;
	DrawEnvOffset(&ofsX, &ofsY);

	memset(vertex, 0, sizeof(GrVertex) * 4);

	float visualX0 = p0[0], visualY0 = p0[1];
	NativeVisual_SampleXY(p0, &visualX0, &visualY0);
	vertex[0].x = visualX0 + ofsX;
	vertex[0].y = visualY0 + ofsY;
	float visualW = w, visualH = h;
	NativeVisual_SampleRect(p0, &visualW, &visualH);

	vertex[1].x = vertex[0].x;
	vertex[1].y = vertex[0].y + visualH;

	vertex[2].x = vertex[0].x + visualW;
	vertex[2].y = vertex[0].y + visualH;

	vertex[3].x = vertex[0].x + visualW;
	vertex[3].y = vertex[0].y;
}

void MakeColourNoShade(GrVertex *vertex, int n)
{
	--n;
	while (n >= 0)
	{
		vertex[n].r = 128;
		vertex[n].g = 128;
		vertex[n].b = 128;
		vertex[n].a = 255;
		--n;
	}
}

void MakeColourLine(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 4);
		return;
	}
	assert(col0);
	assert(col1);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	NativeVisual_SampleRGB(col0, &vertex[0].r, &vertex[0].g, &vertex[0].b);
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	NativeVisual_SampleRGB(col1, &vertex[1].r, &vertex[1].g, &vertex[1].b);
	vertex[1].a = 255;

	vertex[2].r = col1[0];
	vertex[2].g = col1[1];
	vertex[2].b = col1[2];
	vertex[2].a = 255;

	vertex[3].r = col0[0];
	vertex[3].g = col0[1];
	vertex[3].b = col0[2];
	vertex[3].a = 255;
}

void MakeColourTriangle(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1, u8 *col2)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 3);
		return;
	}

	assert(col0);
	assert(col1);
	assert(col2);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	NativeVisual_SampleRGB(col0, &vertex[0].r, &vertex[0].g, &vertex[0].b);
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	NativeVisual_SampleRGB(col1, &vertex[1].r, &vertex[1].g, &vertex[1].b);
	vertex[1].a = 255;

	vertex[2].r = col2[0];
	vertex[2].g = col2[1];
	vertex[2].b = col2[2];
	NativeVisual_SampleRGB(col2, &vertex[2].r, &vertex[2].g, &vertex[2].b);
	vertex[2].a = 255;
}

void MakeColourQuad(GrVertex *vertex, bool shadeTexOn, u8 *col0, u8 *col1, u8 *col2, u8 *col3)
{
	if (!shadeTexOn)
	{
		MakeColourNoShade(vertex, 4);
		return;
	}

	assert(col0);
	assert(col1);
	assert(col2);
	assert(col3);

	vertex[0].r = col0[0];
	vertex[0].g = col0[1];
	vertex[0].b = col0[2];
	NativeVisual_SampleRGB(col0, &vertex[0].r, &vertex[0].g, &vertex[0].b);
	vertex[0].a = 255;

	vertex[1].r = col1[0];
	vertex[1].g = col1[1];
	vertex[1].b = col1[2];
	NativeVisual_SampleRGB(col1, &vertex[1].r, &vertex[1].g, &vertex[1].b);
	vertex[1].a = 255;

	vertex[2].r = col2[0];
	vertex[2].g = col2[1];
	vertex[2].b = col2[2];
	NativeVisual_SampleRGB(col2, &vertex[2].r, &vertex[2].g, &vertex[2].b);
	vertex[2].a = 255;

	vertex[3].r = col3[0];
	vertex[3].g = col3[1];
	vertex[3].b = col3[2];
	NativeVisual_SampleRGB(col3, &vertex[3].r, &vertex[3].g, &vertex[3].b);
	vertex[3].a = 255;
}

