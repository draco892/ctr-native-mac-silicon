#include <common.h>

#define VEH_TURBO_FIELD(cursor, type, offset) (*(type *)((cursor) + (offset)))

void VehTurbo_ProcessBucket(struct Thread *turboThread)
{
#if defined(CTR_NATIVE_HOST64)
    for (; turboThread; turboThread = turboThread->siblingThread)
    {
        struct Turbo *turbo = turboThread->object;
        struct InstDrawPerPlayer *primary = INST_GETIDPP(turboThread->inst);
        struct InstDrawPerPlayer *secondary = INST_GETIDPP(turbo->inst);
        struct InstDrawPerPlayer *driver = INST_GETIDPP(turbo->driver->instSelf);
        for (int i = 0; i < GAME_TRACKER->numPlyrCurrGame; i++)
        {
            if ((driver[i].instFlags & PUSHBUFFER_EXISTS) == 0)
            {
                primary[i].instFlags &= driver[i].instFlags | ~DRAW_SUCCESSFUL;
                secondary[i].instFlags &= driver[i].instFlags | ~DRAW_SUCCESSFUL;
                primary[i].otRangeNormal = secondary[i].otRangeNormal = driver[i].otRangeNormal;
                primary[i].otRangeSecondary = secondary[i].otRangeSecondary = driver[i].otRangeSecondary;
                primary[i].depthOffset[0] = secondary[i].depthOffset[0] = driver[i].depthOffset[0];
                primary[i].depthOffset[1] = secondary[i].depthOffset[1] = driver[i].depthOffset[1];
            }
        }
    }
#else
	register struct GameTracker *initialGameTracker CTR_PSX_REGISTER("$11");
	register struct GameTracker *loopGameTracker CTR_PSX_REGISTER("$9");
	struct Turbo *turbo;
	register struct Instance *primaryBase CTR_PSX_REGISTER("$5");
	register struct Instance *driverBase CTR_PSX_REGISTER("$2");
	register u32 primary CTR_PSX_REGISTER("$7");
	register u32 secondary CTR_PSX_REGISTER("$6");
	register u32 driver CTR_PSX_REGISTER("$5");
	register u32 clearMask CTR_PSX_REGISTER("$10");
	u32 range;
	u16 depth;
	register int i CTR_PSX_REGISTER("$8");

	clearMask = ~DRAW_SUCCESSFUL;
	if (turboThread == NULL)
	{
		return;
	}

	initialGameTracker = GAME_TRACKER;
	do
	{
		turbo = turboThread->object;
		primaryBase = turboThread->inst;
		secondary = (u32)turbo->inst;
		driverBase = turbo->driver->instSelf;

		i = 0;
		if (initialGameTracker->numPlyrCurrGame != 0)
		{
			loopGameTracker = GAME_TRACKER;
			primary = (u32)primaryBase;
			driver = (u32)driverBase;
			do
			{
				if ((VEH_TURBO_FIELD(driver, u32, 0xb8) & PUSHBUFFER_EXISTS) == 0)
				{
					VEH_TURBO_FIELD(secondary, u32, 0xb8) &= VEH_TURBO_FIELD(driver, u32, 0xb8) | clearMask;
					VEH_TURBO_FIELD(primary, u32, 0xb8) &= VEH_TURBO_FIELD(driver, u32, 0xb8) | clearMask;

					range = VEH_TURBO_FIELD(driver, u32, 0xe4);
					VEH_TURBO_FIELD(primary, u32, 0xe4) = range;
					VEH_TURBO_FIELD(secondary, u32, 0xe4) = range;
					range = VEH_TURBO_FIELD(driver, u32, 0xe8);
					VEH_TURBO_FIELD(primary, u32, 0xe8) = range;
					VEH_TURBO_FIELD(secondary, u32, 0xe8) = range;

					depth = VEH_TURBO_FIELD(driver, u16, 0xdc);
					VEH_TURBO_FIELD(primary, u16, 0xdc) = depth;
					VEH_TURBO_FIELD(secondary, u16, 0xdc) = depth;
					depth = VEH_TURBO_FIELD(driver, u16, 0xde);
					VEH_TURBO_FIELD(primary, u16, 0xde) = depth;
					VEH_TURBO_FIELD(secondary, u16, 0xde) = depth;
				}

				primary += sizeof(struct InstDrawPerPlayer);
				secondary += sizeof(struct InstDrawPerPlayer);
				driver += sizeof(struct InstDrawPerPlayer);
				i++;
			} while (i < loopGameTracker->numPlyrCurrGame);
		}

		turboThread = turboThread->siblingThread;
	} while (turboThread != NULL);
#endif
}

#undef VEH_TURBO_FIELD
