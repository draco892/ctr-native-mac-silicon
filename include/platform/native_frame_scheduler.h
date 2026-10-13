#ifndef NATIVE_FRAME_SCHEDULER_H
#define NATIVE_FRAME_SCHEDULER_H

#include <stdint.h>

#define NATIVE_FRAME_VBLANK_CYCLES UINT64_C(897619)
#define NATIVE_FRAME_GPU_HZ UINT64_C(53693175)
#define NATIVE_FRAME_MAX_CATCHUP_VBLANKS 8u

enum NativeFrameEvent
{
    NATIVE_FRAME_EVENT_NONE = 0,
    NATIVE_FRAME_EVENT_VBLANK = 1,
    NATIVE_FRAME_EVENT_UPDATE = 2
};

/* Host-only accumulator. Never serialize its deadlines into game checkpoints.
 * Each Poll consumes at most one VBlank, allowing updates between catch-up pairs. */
struct NativeFrameScheduler
{
    uint64_t frequency, nextVBlank, remainder, lastNow, rebases;
    unsigned phase;
};

int NativeFrameScheduler_Init(struct NativeFrameScheduler *clock, uint64_t frequency, uint64_t now);
unsigned NativeFrameScheduler_Poll(struct NativeFrameScheduler *clock, uint64_t now);
uint64_t NativeFrameScheduler_WaitTicks(const struct NativeFrameScheduler *clock, uint64_t now);

#endif
