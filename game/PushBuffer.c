#include <common.h>


#include "PushBuffer_Core.c"

void PushBuffer_SetDrawEnv_DecalMP(void *ot, struct DB *backBuffer, RECT *viewport, s16 offsetX, s16 offsetY, u8 dtd, u8 dfe, u8 isbg, u8 tpageUpper,
                                   u8 tpageLower)
{
	void *p;
	DRAWENV newDrawEnv;

#ifdef CTR_NATIVE
	// NOTE(aalhendi): Retail receives PS1 RAM OT slots here. Native translates
	// 24-bit OT tokens back to host pointers, so stale DecalMP range metadata
	// must not splice a DR_ENV packet into unrelated current-frame memory.
	if (!CtrGpu_IsCurrentOTRange(backBuffer, ot, ot))
	{
		return;
	}
#endif

	// Copy DrawEnv from gGT->backBuffer
	for (u32 i = 0; i < sizeof(DRAWENV) / 4; i++)
	{
		CTR_WriteU32LE((u8 *)&newDrawEnv + i * 4, CTR_ReadU32LE((u8 *)&backBuffer->drawEnv + i * 4));
	}

	// Now modify DrawEnv...

	// RECT viewport (startX, startY, endX, endY)
	newDrawEnv.clip.x = viewport->x;
	newDrawEnv.clip.y = viewport->y;
	newDrawEnv.clip.w = viewport->w;
	newDrawEnv.clip.h = viewport->h;

	newDrawEnv.ofs[1] = offsetY;

	// tpage
	newDrawEnv.tpage = (u16)((tpageUpper << 8) | tpageLower);

	// dtd (dithering)
	newDrawEnv.dtd = dtd;

	// dfe (blocked or permitted)
	newDrawEnv.dfe = dfe;

	// isbg (always 0)
	newDrawEnv.isbg = isbg;

	p = backBuffer->primMem.cursor;
	void *prim = NULL;

	// cursor < guardEnd
	if (p <= backBuffer->primMem.guardEnd)
	{
		// advance curr
		backBuffer->primMem.cursor = (u8 *)backBuffer->primMem.cursor + 0x40;

		prim = p;
	}

	if (prim == NULL)
	{
		return;
	}

	// ofs[X]
	newDrawEnv.ofs[0] = offsetX;

	// DrawEnv just built
	SetDrawEnv(prim, &newDrawEnv);

	// This doesn't really draw a primitive,
	// it links the ptrOT from the camera,
	// into the ptrOT of backBuffer DB, allowing
	// this camera's primitives to draw
	AddPrim(ot, prim);
}


void PushBuffer_SetDrawEnv_Normal(void *ot, struct PushBuffer *pb, struct DB *backBuffer, s16 *copyDrawEnvNULL, int isbg)
{
	DRAWENV newDrawEnv;

	for (u32 i = 0; i < sizeof(DRAWENV) / 4; i++)
	{
		CTR_WriteU32LE((u8 *)&newDrawEnv + i * 4, CTR_ReadU32LE((u8 *)&backBuffer->drawEnv + i * 4));
	}

	// always?
	if (copyDrawEnvNULL == 0)
	{
		newDrawEnv.clip.x += pb->rect.x;
		newDrawEnv.clip.y += pb->rect.y;
		newDrawEnv.clip.w = pb->rect.w;
		newDrawEnv.clip.h = pb->rect.h;
		newDrawEnv.ofs[0] += pb->rect.x;
		newDrawEnv.ofs[1] += pb->rect.y;
	}

	else
	{
		newDrawEnv.clip.x = copyDrawEnvNULL[0];
		newDrawEnv.clip.y = copyDrawEnvNULL[1];
		newDrawEnv.clip.w = copyDrawEnvNULL[2];
		newDrawEnv.clip.h = copyDrawEnvNULL[3];
		newDrawEnv.ofs[0] = copyDrawEnvNULL[0];
		newDrawEnv.ofs[1] = copyDrawEnvNULL[1];
	}

	newDrawEnv.isbg = isbg;

	void *p = backBuffer->primMem.cursor;
	if (p <= backBuffer->primMem.guardEnd)
	{
		backBuffer->primMem.cursor = (u8 *)backBuffer->primMem.cursor + 0x40;

		SetDrawEnv(p, &newDrawEnv);

		// This doesn't really draw a primitive,
		// it links the ptrOT from the camera,
		// into the ptrOT of backBuffer DB, allowing
		// this camera's primitives to draw
		AddPrim(ot, p);
	}
}

void PushBuffer_FadeOneWindow(struct PushBuffer *pb)
{
	typedef struct
	{
		u32 tag;
		u32 tpage;
		POLY_F4 f4;
	} multiCmdPacket;

	int fadeStrength;
	multiCmdPacket *p = NULL;

	struct DB *backBuffer = sdata->gGT->backBuffer;

	s16 currValue = pb->fadeFromBlack_currentValue;

	// if not 0x1000, which means there must be
	// some amount of fading
	if (currValue != 0x1000)
	{
		p = (multiCmdPacket *)backBuffer->primMem.cursor;

		setlen(p, 7);
		p->f4.tag = 0;
		p->f4.code = 0x2a;
		p->f4.x0 = 0;
		p->f4.y0 = 0;

		// if we are fading to black
		if (currValue < 0x1001)
		{
			p->tpage = 0xe1000a40;

			// get strength of fade (0 to 0x1000)
			fadeStrength = 0xfff - currValue;
		}
		else
		{
			// fade to white
			p->tpage = 0xe1000a20;

			// get strength of fade (0 to 0x1000)
			fadeStrength = currValue - 0x1000;
		}

#ifdef CTR_NATIVE
		// NOTE(aalhendi): Native PsyCross needs dfe=1 for this full-window fade.
		p->tpage |= 0x400; // set dfe=1
#endif

		// strength of fade
		fadeStrength = fadeStrength >> 4;

		p->f4.r0 = (u8)fadeStrength;
		p->f4.g0 = (u8)fadeStrength;
		p->f4.b0 = (u8)fadeStrength;
		p->f4.x1 = pb->rect.w;
		p->f4.y1 = 0;
		p->f4.x2 = 0;
		p->f4.y2 = pb->rect.h;
		p->f4.x3 = pb->rect.w;
		p->f4.y3 = pb->rect.h;
		AddPrim(pb->ptrOT, p);

		// move pointer after writing polygons
		backBuffer->primMem.cursor = p + 1;
	}

	// alter the fade value by the fade velocity
	currValue += pb->fade_step;

	// if fade velocity is negative
	if (pb->fade_step < 1)
	{
		// if we go lower than the desired fade
		if (currValue < pb->fadeFromBlack_desiredResult)
		{
			// set to desired fade
			currValue = pb->fadeFromBlack_desiredResult;
		}
	}

	// if fade velocity is positive
	else
	{
		// if we go higher than the desired fade value
		if (pb->fadeFromBlack_desiredResult < currValue)
		{
			// set to desired fade value
			currValue = pb->fadeFromBlack_desiredResult;
		}
	}

	// set new fade value
	pb->fadeFromBlack_currentValue = currValue;
}


void PushBuffer_FadeAllWindows()
{
	struct GameTracker *gGT = sdata->gGT;

	for (int i = 0; i < gGT->numPlyrCurrGame; i++)
	{
		PushBuffer_FadeOneWindow(&gGT->pushBuffer[i]);
	}

	PushBuffer_FadeOneWindow(&gGT->pushBuffer_UI);
}
