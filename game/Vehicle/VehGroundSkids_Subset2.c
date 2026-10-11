#include <ctr_effect_work.h>

void VehGroundSkids_Subset2(struct VehGroundSkidsScratch *scratch, const SVECTOR *v1, const SVECTOR *v2, const SVECTOR *v3)
{
	// NOTE(aalhendi): Retail deliberately subtracts the low halfwords before
	// projection; keep the 16-bit wraparound visible to both targets.
	scratch->projected[0].vx = (s16)(u16)(((u32)(u16)v1->vx - (u32)(u16)scratch->origin.x) << 2);
	scratch->projected[0].vy = (s16)(u16)(((u32)(u16)v1->vy - (u32)(u16)scratch->origin.y) << 2);
	scratch->projected[0].vz = (s16)(u16)(((u32)(u16)v1->vz - (u32)(u16)scratch->origin.z) << 2);

	scratch->projected[1].vx = (s16)(u16)(((u32)(u16)v2->vx - (u32)(u16)scratch->origin.x) << 2);
	scratch->projected[1].vy = (s16)(u16)(((u32)(u16)v2->vy - (u32)(u16)scratch->origin.y) << 2);
	scratch->projected[1].vz = (s16)(u16)(((u32)(u16)v2->vz - (u32)(u16)scratch->origin.z) << 2);

	scratch->projected[2].vx = (s16)(u16)(((u32)(u16)v3->vx - (u32)(u16)scratch->origin.x) << 2);
	scratch->projected[2].vy = (s16)(u16)(((u32)(u16)v3->vy - (u32)(u16)scratch->origin.y) << 2);
	scratch->projected[2].vz = (s16)(u16)(((u32)(u16)v3->vz - (u32)(u16)scratch->origin.z) << 2);
}

