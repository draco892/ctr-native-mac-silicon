#ifndef CTR_NATIVE_RENDERER_CLIP_H
#define CTR_NATIVE_RENDERER_CLIP_H

#include <macros.h>
#include <psx/libgte.h>
#include <psx/libgpu.h>
#include <platform/native_glad.h>

struct NativeRendererClipRect
{
	int x, y, w, h;
};

static inline struct NativeRendererClipRect NativeRenderer_ClipScissorRect(const RECT16 *rect, const DISPENV *display)
{
	return (struct NativeRendererClipRect){rect->x - display->disp.x,
	                                      display->disp.h - (rect->y - display->disp.y) - rect->h,
	                                      rect->w, rect->h};
}

static inline void NativeRenderer_ResetFrameClip(int *cachedScissorState)
{
	// A split's scissor must never restrict the next frame's stencil clear.
	glDisable(GL_SCISSOR_TEST);
	*cachedScissorState = 0;
	glClear(GL_STENCIL_BUFFER_BIT);
}

#endif
