#include <platform/native_renderer_clip.h>
#include <stdio.h>
#include <stdlib.h>

static int scissorEnabled, stencil[240];
static int scissorBottom, scissorHeight;
static void APIENTRY Disable(GLenum capability)
{
	if (capability != GL_SCISSOR_TEST)
		abort();
	scissorEnabled = 0;
}
static void APIENTRY Clear(GLbitfield mask)
{
	if (mask != GL_STENCIL_BUFFER_BIT)
		abort();
	for (int row = 0; row < 240; row++)
		if (!scissorEnabled || (row >= scissorBottom && row < scissorBottom + scissorHeight))
			stencil[row] = 0;
}
PFNGLDISABLEPROC glad_glDisable = Disable;
PFNGLCLEARPROC glad_glClear = Clear;

static void Check(int condition)
{
	if (!condition)
		abort();
}

int main(void)
{
	// Simulate a preceding frame whose last draw masked the lower screen,
	// then left a top-half scissor enabled. A clipped clear leaves that mask.
	for (int row = 0; row < 240; row++)
		stencil[row] = 1;
	scissorEnabled = 1;
	scissorBottom = 120;
	scissorHeight = 120;
	int cached = 1;
	NativeRenderer_ResetFrameClip(&cached);
	Check(cached == 0 && !scissorEnabled);
	for (int row = 0; row < 240; row++)
		Check(stencil[row] == 0);

	// Both page-flip origins and every row boundary must map exactly,
	// including odd heights and the very last pixel row.
	for (int page = 0; page < 4; page++)
	{
		DISPENV display = {0};
		const int heightLimit = page < 2 ? 216 : 240;
		display.disp = (RECT16){0, (page % 2) * 296, 512, heightLimit};
		for (int y = 0; y < heightLimit; y++)
			for (int height = 1; height <= heightLimit - y; height++)
			{
				RECT16 rect = {0, display.disp.y + y, 512, height};
				struct NativeRendererClipRect clip = NativeRenderer_ClipScissorRect(&rect, &display);
				Check(clip.x == 0 && clip.w == 512 && clip.y == heightLimit - y - height && clip.h == height);
			}
	}
	puts("Renderer clipping tests passed");
	return 0;
}
