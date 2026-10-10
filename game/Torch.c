#include <common.h>

#include <ctr_effect_work.h>

static u32 Torch_ReadWord(const void *base, int offset)
{
	return *(const u32 *)(const void *)((const char *)base + offset);
}

#ifndef CTR_NATIVE
static s32 Torch_ReadS32(const void *base, int offset)
{
	return *(const s32 *)(const void *)((const char *)base + offset);
}

static s8 Torch_ReadS8(const void *base, int offset)
{
	return *(const s8 *)(const void *)((const char *)base + offset);
}

static u8 Torch_ReadU8(const void *base, int offset)
{
	return *(const u8 *)(const void *)((const char *)base + offset);
}

#endif

static struct TorchScratch *Torch_Scratch(void)
{
#if defined(CTR_NATIVE_HOST64)
	return NativeTorchWork_Get();
#else
	return CTR_SCRATCHPAD_PTR(struct TorchScratch, 0);
#endif
}

static struct TorchPointSource Torch_Point(enum TorchRingIndex ring, enum TorchRingPoint point)
{
	return (struct TorchPointSource){ring, point};
}

static struct TorchRingScratch *Torch_Ring(enum TorchRingIndex ring)
{
	return &Torch_Scratch()->rings[ring];
}

static u32 *Torch_RingPointWordPtr(struct TorchPointSource source)
{
	return (u32 *)(void *)((u8 *)Torch_Ring(source.ring) + source.point);
}

static u32 Torch_ReadRingPointWord(struct TorchPointSource source)
{
	return *Torch_RingPointWordPtr(source);
}

static void Torch_WriteRingPointWord(struct TorchPointSource source, u32 value)
{
	*Torch_RingPointWordPtr(source) = value;
}

static s16 Torch_ReadRingPointX(enum TorchRingIndex ring, enum TorchRingPoint point)
{
	return (s16)(u16)Torch_ReadRingPointWord(Torch_Point(ring, point));
}

static s16 Torch_ReadRingPointY(enum TorchRingIndex ring, enum TorchRingPoint point)
{
	return (s16)(Torch_ReadRingPointWord(Torch_Point(ring, point)) >> 16);
}

static u32 Torch_PackXY(s32 x, s32 y)
{
	return (u32)(u16)x | ((u32)(u16)y << 16);
}

static void Torch_LoadViewAsLightMatrix(const struct PushBuffer *pb)
{
	CTC2(Torch_ReadWord(&pb->matrix_ViewProj, 0x00), 8);
	CTC2(Torch_ReadWord(&pb->matrix_ViewProj, 0x04), 9);
	CTC2(Torch_ReadWord(&pb->matrix_ViewProj, 0x08), 10);
	CTC2(Torch_ReadWord(&pb->matrix_ViewProj, 0x0c), 11);
	CTC2(Torch_ReadWord(&pb->matrix_ViewProj, 0x10), 12);
	CTC2(Torch_ReadWord(&pb->matrix_ViewProj, 0x14), 13);
	CTC2(Torch_ReadWord(&pb->matrix_ViewProj, 0x18), 14);
	CTC2(Torch_ReadWord(&pb->matrix_ViewProj, 0x1c), 15);

	CTC2(0x1000, 0);
	CTC2(0, 1);
	CTC2(0, 3);
	CTC2(0x1000, 4);
	CTC2(0, 5);
	CTC2(0, 6);
	CTC2(0, 7);
}

static void Torch_LoadRadiusVectors(u32 radius)
{
	u32 diagonal = (radius * 0xb50u) >> 12;

	MTC2(radius, 0);
	MTC2(radius << 16, 2);
	MTC2(diagonal | (diagonal << 16), 4);
}

static s32 Torch_ClampSignedCoord(s32 value, u16 max)
{
	if (value < 0)
	{
		return 0;
	}

	if ((s32)(value - (s32)max) >= 0)
	{
		return max;
	}

	return value;
}

static u16 Torch_ClampPackedCoord(u16 coord, u16 max)
{
	u32 value = coord;

	if ((s32)(value - max) >= 0)
	{
		value = max;
	}

	return (u16)value;
}

static void Torch_WriteUvPair(enum TorchUvSlot slot, struct TorchPointSource source)
{
	struct TorchScratch *scratch = Torch_Scratch();
	u32 point = Torch_ReadRingPointWord(source);
	u16 x = Torch_ClampPackedCoord((u16)point, scratch->maxX);
	u16 y = Torch_ClampPackedCoord((u16)(point >> 16), scratch->maxY);
	s32 u = (s32)x - (s32)(u8)scratch->tileUBase + scratch->rectX;
	s32 v = (s32)y + scratch->rectYWithSwapchain;

	switch (slot)
	{
	case TORCH_UV_SLOT_0:
		scratch->uv0.u = (u8)u;
		scratch->uv0.v = (u8)v;
		break;

	case TORCH_UV_SLOT_1:
		scratch->uv1.u = (u8)u;
		scratch->uv1.v = (u8)v;
		break;

	case TORCH_UV_SLOT_2:
		scratch->uv23.u0 = (u8)u;
		scratch->uv23.v0 = (u8)v;
		break;

	case TORCH_UV_SLOT_3:
		scratch->uv23.u1 = (u8)u;
		scratch->uv23.v1 = (u8)v;
		break;
	}
}

static void Torch_LinkPrimitive(u32 *tagWord, const void *packet, u32 *ot, u32 tag)
{
	CtrGpu_LinkPacket24(ot, tagWord, packet, tag);
}

static u32 *Torch_EmitFT3(u32 *prim, u32 *ot, struct TorchPointSource uv0, struct TorchPointSource uv1, struct TorchPointSource uv2,
				          struct TorchPointSource xy0, struct TorchPointSource xy1, struct TorchPointSource xy2)
{
	POLY_FT3 *poly = (POLY_FT3 *)prim;
	struct TorchScratch *scratch = Torch_Scratch();

	Torch_WriteUvPair(TORCH_UV_SLOT_0, uv0);
	Torch_WriteUvPair(TORCH_UV_SLOT_1, uv1);
	Torch_WriteUvPair(TORCH_UV_SLOT_2, uv2);

	CtrGpu_WriteColorCode(&poly->r0, scratch->color | 0x24000000);
	CtrGpu_WritePackedXY(&poly->x0, Torch_ReadRingPointWord(xy0));
	CtrGpu_WritePackedUVWord(&poly->u0, scratch->uv0.word);
	CtrGpu_WritePackedXY(&poly->x1, Torch_ReadRingPointWord(xy1));
	CtrGpu_WritePackedUVWord(&poly->u1, scratch->uv1.word);
	CtrGpu_WritePackedXY(&poly->x2, Torch_ReadRingPointWord(xy2));
	CtrGpu_WritePackedUVWord(&poly->u2, scratch->uv23.word);
	Torch_LinkPrimitive(&poly->tag, poly, ot, 0x07000000);

	return (u32 *)(poly + 1);
}

static u32 *Torch_EmitFT4(u32 *prim, u32 *ot, struct TorchPointSource uv0, struct TorchPointSource uv1, struct TorchPointSource uv2,
				          struct TorchPointSource uv3, struct TorchPointSource xy0, struct TorchPointSource xy1, struct TorchPointSource xy2,
				          struct TorchPointSource xy3)
{
	POLY_FT4 *poly = (POLY_FT4 *)prim;
	struct TorchScratch *scratch = Torch_Scratch();
	u32 uv23;

	Torch_WriteUvPair(TORCH_UV_SLOT_0, uv0);
	Torch_WriteUvPair(TORCH_UV_SLOT_1, uv1);
	Torch_WriteUvPair(TORCH_UV_SLOT_2, uv2);
	Torch_WriteUvPair(TORCH_UV_SLOT_3, uv3);
	uv23 = scratch->uv23.word;

	CtrGpu_WriteColorCode(&poly->r0, scratch->color | 0x2c000000);
	CtrGpu_WritePackedXY(&poly->x0, Torch_ReadRingPointWord(xy0));
	CtrGpu_WritePackedUVWord(&poly->u0, scratch->uv0.word);
	CtrGpu_WritePackedXY(&poly->x1, Torch_ReadRingPointWord(xy1));
	CtrGpu_WritePackedUVWord(&poly->u1, scratch->uv1.word);
	CtrGpu_WritePackedXY(&poly->x2, Torch_ReadRingPointWord(xy2));
	CtrGpu_WritePackedUVWord(&poly->u2, uv23);
	CtrGpu_WritePackedXY(&poly->x3, Torch_ReadRingPointWord(xy3));
	CtrGpu_WritePackedUVWord(&poly->u3, uv23 >> 16);
	Torch_LinkPrimitive(&poly->tag, poly, ot, 0x09000000);

	return (u32 *)(poly + 1);
}

static struct TorchCardRegs Torch_Subset1_BuildCard(s32 centerX, s32 centerY, u32 sxy0, u32 sxy1)
{
	struct TorchCardRegs regs;
	s32 xRadius = (u16)sxy0;
	s32 yRadius = (s32)sxy1 >> 16;
	u32 packedCenterY = (u32)centerY << 16;

	regs.left = ((u32)(centerX - xRadius) & 0xffff) | packedCenterY;
	regs.right = ((u32)(centerX + xRadius) & 0xffff) | packedCenterY;
	regs.top = ((u32)(centerY - yRadius) << 16) | (u32)(u16)centerX;
	regs.bottom = ((u32)(centerY + yRadius) << 16) | (u32)(u16)centerX;

	return regs;
}

static void Torch_Subset2_StoreCard(const struct TorchCardRegs *regs, s32 centerX, s32 centerY, enum TorchRingIndex ringIndex)
{
	u32 sxy2 = MFC2(14);
	s32 radiusX = (u16)sxy2;
	s32 radiusY = (s32)sxy2 >> 16;

	struct TorchRingScratch *ring = Torch_Ring(ringIndex);

	ring->left = regs->left;
	ring->right = regs->right;
	ring->top = regs->top;
	ring->bottom = regs->bottom;
	ring->topLeft = Torch_PackXY(centerX - radiusX, centerY - radiusY);
	ring->topRight = Torch_PackXY(centerX + radiusX, centerY - radiusY);
	ring->bottomLeft = Torch_PackXY(centerX - radiusX, centerY + radiusY);
	ring->bottomRight = Torch_PackXY(centerX + radiusX, centerY + radiusY);
}

static void Torch_Subset3_SetTpage(s32 x, s32 y)
{
	struct TorchScratch *scratch = Torch_Scratch();
	u16 maxX = scratch->maxX;
	u16 maxY = scratch->maxY;
	u32 tile;

	x = Torch_ClampSignedCoord(x, maxX);
	x += scratch->rectX;

	y = Torch_ClampSignedCoord(y, maxY);
	y += scratch->rectYWithSwapchain;

	tile = ((u32)x & 0x3ff) >> 6;
	scratch->tileUBase = (u16)(tile << 6);
	scratch->uv1.tpage = (u16)(tile | (((u32)y & 0x100) >> 4) | 0x100);
}

static u32 *Torch_Subset4_EmitFT3(u32 *prim, u32 *ot, enum TorchRingPoint pointA, enum TorchRingPoint pointB)
{
	return Torch_EmitFT3(prim, ot, Torch_Point(TORCH_RING_0, TORCH_POINT_CENTER), Torch_Point(TORCH_RING_2, pointA), Torch_Point(TORCH_RING_2, pointB),
	                     Torch_Point(TORCH_RING_0, TORCH_POINT_CENTER), Torch_Point(TORCH_RING_1, pointA), Torch_Point(TORCH_RING_1, pointB));
}

static u32 *Torch_Subset5_EmitFT3(u32 *prim, u32 *ot, enum TorchRingPoint pointA, enum TorchRingPoint pointB)
{
	return Torch_EmitFT3(prim, ot, Torch_Point(TORCH_RING_0, TORCH_POINT_CENTER), Torch_Point(TORCH_RING_2, pointA), Torch_Point(TORCH_RING_1, pointB),
	                     Torch_Point(TORCH_RING_0, TORCH_POINT_CENTER), Torch_Point(TORCH_RING_1, pointA), Torch_Point(TORCH_RING_2, pointB));
}

static u32 *Torch_Subset6_EmitFT3(u32 *prim, u32 *ot, enum TorchRingPoint pointA, enum TorchRingPoint pointB)
{
	return Torch_EmitFT3(prim, ot, Torch_Point(TORCH_RING_0, TORCH_POINT_CENTER), Torch_Point(TORCH_RING_1, pointA), Torch_Point(TORCH_RING_1, pointB),
	                     Torch_Point(TORCH_RING_0, TORCH_POINT_CENTER), Torch_Point(TORCH_RING_2, pointA), Torch_Point(TORCH_RING_2, pointB));
}

static u32 *Torch_Subset7_EmitFT4(u32 *prim, u32 *ot, enum TorchRingPoint pointA, enum TorchRingPoint pointB)
{
	return Torch_EmitFT4(prim, ot, Torch_Point(TORCH_RING_0, pointA), Torch_Point(TORCH_RING_0, pointB), Torch_Point(TORCH_RING_2, pointA),
	                     Torch_Point(TORCH_RING_2, pointB), Torch_Point(TORCH_RING_0, pointA), Torch_Point(TORCH_RING_0, pointB),
	                     Torch_Point(TORCH_RING_1, pointA), Torch_Point(TORCH_RING_1, pointB));
}

static u32 *Torch_Subset8_EmitFT4(u32 *prim, u32 *ot, enum TorchRingPoint pointA, enum TorchRingPoint pointB)
{
	return Torch_EmitFT4(prim, ot, Torch_Point(TORCH_RING_0, pointA), Torch_Point(TORCH_RING_0, pointB), Torch_Point(TORCH_RING_2, pointA),
	                     Torch_Point(TORCH_RING_1, pointB), Torch_Point(TORCH_RING_0, pointA), Torch_Point(TORCH_RING_0, pointB),
	                     Torch_Point(TORCH_RING_1, pointA), Torch_Point(TORCH_RING_2, pointB));
}

static u32 *Torch_Subset9_EmitFT4(u32 *prim, u32 *ot, enum TorchRingPoint pointA, enum TorchRingPoint pointB)
{
	return Torch_EmitFT4(prim, ot, Torch_Point(TORCH_RING_1, pointA), Torch_Point(TORCH_RING_1, pointB), Torch_Point(TORCH_RING_0, pointA),
	                     Torch_Point(TORCH_RING_0, pointB), Torch_Point(TORCH_RING_2, pointA), Torch_Point(TORCH_RING_2, pointB),
	                     Torch_Point(TORCH_RING_0, pointA), Torch_Point(TORCH_RING_0, pointB));
}

static int Torch_IsCardVisible(const struct TorchCardRegs *regs, u32 screenSize)
{
	u32 bounds;

	if ((((s32)regs->top >> 16) - 0x18) <= 0)
	{
		return 0;
	}

	bounds = ~((regs->left - screenSize) | (regs->right - screenSize) | (regs->top - screenSize) | (regs->bottom - screenSize)) |
	         (regs->left & regs->right & regs->top & regs->bottom);

	if ((s32)bounds < 0)
	{
		return 0;
	}

	return (s32)(bounds << 16) >= 0;
}

static u32 *Torch_EmitParticle(u32 *prim, u32 *ot)
{
	Torch_Subset3_SetTpage(Torch_ReadRingPointX(TORCH_RING_0, TORCH_POINT_TOP), Torch_ReadRingPointY(TORCH_RING_0, TORCH_POINT_TOP));
	prim = Torch_Subset6_EmitFT3(prim, ot, TORCH_POINT_TOP_RIGHT, TORCH_POINT_TOP);
	prim = Torch_Subset9_EmitFT4(prim, ot, TORCH_POINT_TOP, TORCH_POINT_TOP_RIGHT);

	Torch_Subset3_SetTpage(Torch_ReadRingPointX(TORCH_RING_0, TORCH_POINT_CENTER), Torch_ReadRingPointY(TORCH_RING_0, TORCH_POINT_TOP_RIGHT));
	prim = Torch_Subset6_EmitFT3(prim, ot, TORCH_POINT_TOP_RIGHT, TORCH_POINT_RIGHT);
	prim = Torch_Subset9_EmitFT4(prim, ot, TORCH_POINT_RIGHT, TORCH_POINT_TOP_RIGHT);

	Torch_Subset3_SetTpage(Torch_ReadRingPointX(TORCH_RING_0, TORCH_POINT_CENTER), Torch_ReadRingPointY(TORCH_RING_0, TORCH_POINT_TOP));
	prim = Torch_Subset6_EmitFT3(prim, ot, TORCH_POINT_RIGHT, TORCH_POINT_BOTTOM_RIGHT);
	prim = Torch_Subset9_EmitFT4(prim, ot, TORCH_POINT_RIGHT, TORCH_POINT_BOTTOM_RIGHT);
	prim = Torch_Subset6_EmitFT3(prim, ot, TORCH_POINT_BOTTOM_RIGHT, TORCH_POINT_BOTTOM);
	prim = Torch_Subset9_EmitFT4(prim, ot, TORCH_POINT_BOTTOM, TORCH_POINT_BOTTOM_RIGHT);

	Torch_Subset3_SetTpage(Torch_ReadRingPointX(TORCH_RING_0, TORCH_POINT_BOTTOM_LEFT), Torch_ReadRingPointY(TORCH_RING_0, TORCH_POINT_TOP));
	prim = Torch_Subset5_EmitFT3(prim, ot, TORCH_POINT_BOTTOM_LEFT, TORCH_POINT_BOTTOM);
	prim = Torch_Subset8_EmitFT4(prim, ot, TORCH_POINT_BOTTOM_LEFT, TORCH_POINT_BOTTOM);

	Torch_Subset3_SetTpage(Torch_ReadRingPointX(TORCH_RING_0, TORCH_POINT_LEFT), Torch_ReadRingPointY(TORCH_RING_0, TORCH_POINT_LEFT));
	prim = Torch_Subset4_EmitFT3(prim, ot, TORCH_POINT_LEFT, TORCH_POINT_BOTTOM_LEFT);
	prim = Torch_Subset7_EmitFT4(prim, ot, TORCH_POINT_LEFT, TORCH_POINT_BOTTOM_LEFT);

	Torch_Subset3_SetTpage(Torch_ReadRingPointX(TORCH_RING_0, TORCH_POINT_LEFT), Torch_ReadRingPointY(TORCH_RING_0, TORCH_POINT_TOP_LEFT));
	prim = Torch_Subset4_EmitFT3(prim, ot, TORCH_POINT_TOP_LEFT, TORCH_POINT_LEFT);
	prim = Torch_Subset7_EmitFT4(prim, ot, TORCH_POINT_TOP_LEFT, TORCH_POINT_LEFT);

	Torch_Subset3_SetTpage(Torch_ReadRingPointX(TORCH_RING_0, TORCH_POINT_TOP_LEFT), Torch_ReadRingPointY(TORCH_RING_0, TORCH_POINT_TOP));
	prim = Torch_Subset5_EmitFT3(prim, ot, TORCH_POINT_TOP_LEFT, TORCH_POINT_TOP);
	prim = Torch_Subset8_EmitFT4(prim, ot, TORCH_POINT_TOP_LEFT, TORCH_POINT_TOP);

	return prim;
}

void Torch_Main(void *particleList_heatWarp, struct PushBuffer *pb, struct PrimMem *primMem, u8 numPlyr, int swapchainIndex)
{
	struct Particle *firstParticle = particleList_heatWarp;
	struct TorchScratch *scratch = Torch_Scratch();
	u32 *prim = (u32 *)primMem->cursor;

	// NOTE(aalhendi): PSX-backfeed blocker: retail saves callee registers and pointer cursors in scratchpad 0x00-0x38.
	// Native C keeps host-width pointers as locals, while preserving retail data temporaries from 0x44 up.
	if (firstParticle != NULL)
	{
		int playerPassesLeft = (s32)(s8)numPlyr - 1;
		int particlesLeft = 12;

		// Native particle cursors remain typed locals; this retail slot is unused.
#ifndef CTR_NATIVE
		scratch->firstParticlePtr32 = (u32)(u32)firstParticle;
#endif
		scratch->swapchainIndex = (u32)swapchainIndex;
		scratch->uv0.clut = 0;

		while (1)
		{
			struct Particle *particle;
			u32 screenSize;
			u32 *otBase;

			Torch_LoadViewAsLightMatrix(pb);

			CTC2((u32)(s32)pb->rect.w << 15, 24);
			CTC2((u32)(s32)pb->rect.h << 15, 25);
			CTC2((u32)pb->distanceToScreen_PREV, 26);

			scratch->screenWFP = (u32)(s32)pb->rect.w << 15;
			scratch->screenHFP = (u32)(s32)pb->rect.h << 15;
			scratch->rectX = pb->rect.x;
			scratch->rectYWithSwapchain = (s16)(pb->rect.y + scratch->swapchainIndex);
			scratch->maxX = (u16)(pb->rect.w - 1);
			scratch->maxY = (u16)(pb->rect.h - 1);

#ifdef CTR_NATIVE
			screenSize = (u16)pb->rect.w | ((u32)(u16)pb->rect.h << 16);
#else
			screenSize = Torch_ReadWord(pb, 0x20);
#endif
			otBase = pb->ptrOT;
			particle = firstParticle;

			while (particle != NULL)
			{
				u32 color;
				u32 radius0;
				u32 radius1;
				u32 radius2;
				s32 viewZ;
				u32 centerSxy;
				u32 gteFlag;
				s32 centerX;
				s32 centerY;
				struct TorchCardRegs card;

#ifdef CTR_NATIVE
				MTC2(Torch_PackXY(particle->axis[PARTICLE_AXIS_POS_X].startVal >> 8, particle->axis[PARTICLE_AXIS_POS_Y].startVal >> 8), 0);
				MTC2((u32)(particle->axis[PARTICLE_AXIS_POS_Z].startVal >> 8), 1);
				radius0 = (u8)((u32)particle->axis[3].startVal >> 8);
				radius1 = (u8)((u32)particle->axis[4].startVal >> 8);
				radius2 = (u8)((u32)particle->axis[5].startVal >> 8);
				gte_llv0bk_b();
				color = (u8)((u32)particle->axis[PARTICLE_AXIS_COLOR_R].startVal >> 8) |
				    ((u32)(u8)((u32)particle->axis[PARTICLE_AXIS_COLOR_G].startVal >> 8) << 8) |
				    ((u32)(u8)((u32)particle->axis[PARTICLE_AXIS_COLOR_B].startVal >> 8) << 16);
#else
				MTC2(Torch_PackXY(Torch_ReadS32(particle, 0x24) >> 8, Torch_ReadS32(particle, 0x2c) >> 8), 0);
				MTC2((u32)(Torch_ReadS32(particle, 0x34) >> 8), 1);

				radius0 = Torch_ReadU8(particle, 0x3d);
				radius1 = Torch_ReadU8(particle, 0x45);
				radius2 = Torch_ReadU8(particle, 0x4d);

				gte_llv0bk_b();
				color = (u32)Torch_ReadU8(particle, 0x5d) | ((u32)Torch_ReadU8(particle, 0x65) << 8) | ((u32)Torch_ReadU8(particle, 0x6d) << 16);
#endif
				scratch->color = color;

				viewZ = (s32)MFC2(27);
				MTC2((u32)viewZ, 1);
				MTC2((u32)viewZ, 3);
				MTC2((u32)viewZ, 5);
				CTC2(0x1000, 2);
				CTC2(scratch->screenWFP, 24);
				CTC2(scratch->screenHFP, 25);
				MTC2(Torch_PackXY((u16)MFC2(25), MFC2(26)), 0);

				gte_rtps_b();

				centerSxy = MFC2(14);
				gteFlag = CFC2(31);
				centerX = (u16)centerSxy;
				centerY = (s32)centerSxy >> 16;

				if ((s32)(gteFlag << 14) >= 0)
				{
					u32 sxy0;
					u32 sxy1;

					Torch_WriteRingPointWord(Torch_Point(TORCH_RING_0, TORCH_POINT_CENTER), centerSxy);
					CTC2(0x0a00, 2);
					CTC2(0, 24);
					CTC2(0, 25);
					Torch_LoadRadiusVectors(radius0);
					gte_rtpt_b();

					sxy0 = MFC2(12);
					sxy1 = MFC2(13);

					if ((s32)(sxy0 - 0x80) < 0)
					{
						Torch_LoadRadiusVectors(radius1);
						card = Torch_Subset1_BuildCard(centerX, centerY, sxy0, sxy1);

						if (Torch_IsCardVisible(&card, screenSize))
						{
							s32 otIndex;
							u32 *ot;

							Torch_Subset2_StoreCard(&card, centerX, centerY, TORCH_RING_0);
							gte_rtpt_b();

							sxy0 = MFC2(12);
							sxy1 = MFC2(13);
							Torch_LoadRadiusVectors(radius2);
							card = Torch_Subset1_BuildCard(centerX, centerY, sxy0, sxy1);
							Torch_Subset2_StoreCard(&card, centerX, centerY, TORCH_RING_1);
							gte_rtpt_b();

#ifdef CTR_NATIVE
							otIndex = (viewZ >> 6) + particle->otIndexOffset;
#else
							otIndex = (viewZ >> 6) + Torch_ReadS8(particle, 0x18);
#endif
							if (otIndex < 0)
							{
								otIndex = 0;
							}
							else if ((otIndex - 0x400) >= 0)
							{
								otIndex = 0x3ff;
							}

							ot = (u32 *)(void *)((char *)otBase + (otIndex << 2));
							sxy0 = MFC2(12);
							sxy1 = MFC2(13);
							card = Torch_Subset1_BuildCard(centerX, centerY, sxy0, sxy1);
							Torch_Subset2_StoreCard(&card, centerX, centerY, TORCH_RING_2);
							prim = Torch_EmitParticle(prim, ot);
						}
					}
				}

				particlesLeft--;
				particle = particle->next;
				if (particlesLeft < 1)
				{
					goto done;
				}
			}

			if (playerPassesLeft <= 0)
			{
				break;
			}

			playerPassesLeft--;
			pb++;
		}
	}

done:
	primMem->cursor = prim;
}
