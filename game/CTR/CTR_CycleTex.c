#include <common.h>

// NOTE(aalhendi): Index from the AnimTex base so GCC preserves retail's signed 16-bit frame index and pointer-add order.
#ifdef CTR_NATIVE
#define CYCLE_TEX_FRAME(animtex, frame) (ANIMTEX_GETARRAY(animtex)[(s16)(frame)])
static int CTR_CycleTex_End(const struct AnimTex *node, const struct AnimTex *first)
{
	const void *marker;
	memcpy(&marker, node, sizeof(marker));
	return marker == first;
}
#else
#define CYCLE_TEX_FRAME(animtex, frame) \
	(((struct IconGroup4 **)((u32)(animtex) + ((u32)(s16)(frame) << 2)))[sizeof(struct AnimTex) / sizeof(struct IconGroup4 *)])
#endif

void CTR_CycleTex_LEV(struct AnimTex *animtex, int timer)
{
	int frameCurr;
	struct AnimTex *curAnimTex = animtex;

	// Termination is determined by pointer to First AnimTex
#ifdef CTR_NATIVE
	while (curAnimTex && !CTR_CycleTex_End(curAnimTex, animtex))
#else
	while (*(int *)curAnimTex != (int)animtex)
#endif
	{
#ifdef CTR_NATIVE
		if (curAnimTex->numFrames <= 0 || curAnimTex->frameSkip < 0 || curAnimTex->frameSkip > 31)
			return;
#endif
		// which texture to draw this frame
		frameCurr = (s32)((u32)timer + (u32)(s32)curAnimTex->frameOffset);

		// allow frames to skip updating (like 60fps hacks)
		frameCurr = frameCurr >> curAnimTex->frameSkip;

		// loop back to index[0] after finished cycle
		frameCurr = frameCurr % curAnimTex->numFrames;
#ifdef CTR_NATIVE
		if (frameCurr < 0)
			frameCurr += curAnimTex->numFrames;
#endif

		// save result
		curAnimTex->frameCurr = frameCurr;

		// Save new frame
		// For levels, this is just a pointer
		curAnimTex->ptrActiveTex = (int *)CYCLE_TEX_FRAME(curAnimTex, frameCurr);

		// Go to next AnimTex, which comes after this AnimTex's ptrarray
		curAnimTex = (struct AnimTex *)&(ANIMTEX_GETARRAY(curAnimTex))[curAnimTex->numFrames];
	}
}

void CTR_CycleTex_Model(struct AnimTex *animtex, int timer)
{
	int frameCurr;
	struct AnimTex *curAnimTex = animtex;

	// Termination is determined by pointer to First AnimTex
#ifdef CTR_NATIVE
	while (curAnimTex && !CTR_CycleTex_End(curAnimTex, animtex))
#else
	while (*(int *)curAnimTex != (int)animtex)
#endif
	{
#ifdef CTR_NATIVE
		if (curAnimTex->numFrames <= 0 || curAnimTex->frameSkip < 0 || curAnimTex->frameSkip > 31)
			return;
#endif
		// which texture to draw this frame
		frameCurr = (s32)((u32)timer + (u32)(s32)curAnimTex->frameOffset);

		// allow frames to skip updating (like 60fps hacks)
		frameCurr = frameCurr >> curAnimTex->frameSkip;

		// loop back to index[0] after finished cycle
		frameCurr = frameCurr % curAnimTex->numFrames;
#ifdef CTR_NATIVE
		if (frameCurr < 0)
			frameCurr += curAnimTex->numFrames;
#endif

		// save result
		curAnimTex->frameCurr = frameCurr;

		// Save new frame
		// For Model, this is a pointer to a pointer
#ifdef CTR_NATIVE
		struct IconGroup4 *frame = CYCLE_TEX_FRAME(curAnimTex, frameCurr);
		if (!curAnimTex->ptrActiveTex)
			return;
		memcpy(curAnimTex->ptrActiveTex, &frame, sizeof(frame));
#else
		*curAnimTex->ptrActiveTex = (int)CYCLE_TEX_FRAME(curAnimTex, frameCurr);
#endif

		// Go to next AnimTex, which comes after this AnimTex's ptrarray
		curAnimTex = (struct AnimTex *)&(ANIMTEX_GETARRAY(curAnimTex))[curAnimTex->numFrames];
	}
}

void CTR_CycleTex_AllModels(u32 numModels, struct Model **pModelArray, int timer)
{
	struct Model *pModel;
	struct ModelHeader *pHeader;
	struct ModelHeader *pHeaderEnd;
	s32 headerCount;

	if (pModelArray == NULL)
	{
		return;
	}

	if (numModels == 0)
	{
		return;
	}

	while (true)
	{
		pModel = *pModelArray;
		if (pModel == NULL)
		{
			return;
		}

		headerCount = pModel->numHeaders;
		// NOTE(aalhendi): Keep the short-lived count distinct from the header end pointer for retail register allocation.
		CTR_PSX_KEEP_VALUE(headerCount);
		pHeader = pModel->headers;
		pHeaderEnd = pHeader + headerCount;
		while (pHeader < pHeaderEnd)
		{
			if ((pHeader->animtex != NULL) && ((pHeader->flags & 2) == 0))
			{
				CTR_CycleTex_Model(pHeader->animtex, timer);
			}
			pHeader++;
		}

		numModels--;
		if (numModels == 0)
		{
			return;
		}

		pModelArray++;
	}
}

void CTR_CycleTex_2p3p4pWumpaHUD(u32 *ptrActiveTex, u32 *ptrArray, int numFrames)
{
	ptrArray[0] = ptrActiveTex[0];
	ptrArray += numFrames;
	ptrArray--;
	ptrActiveTex[0] = CtrGpu_PrimToOTLink24(ptrArray);
}
