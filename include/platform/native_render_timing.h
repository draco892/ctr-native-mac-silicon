#ifndef NATIVE_RENDER_TIMING_H
#define NATIVE_RENDER_TIMING_H

#include <stdint.h>

/* Presentation time only. Never feed this clock into retail physics, RCNT,
 * VBlank callbacks, audio scheduling, or deterministic replay state. */
struct NativeRenderTime
{
    double deltaSeconds; /* Actual interval between completed presentations. */
    double animationDeltaSeconds; /* Capped at 250 ms after host stalls. */
    double elapsedSeconds;
    uint64_t frameIndex;
};

struct NativeRenderClock
{
    uint64_t frequency, interval, origin, lastPresent;
    struct NativeRenderTime time;
};

/* Keep the existing NTSC two-VBlank cadence (~29.909 FPS, below 30). */
int NativeRenderClock_Init(struct NativeRenderClock *clock, uint64_t frequency);
int NativeRenderClock_InitFPS(struct NativeRenderClock *clock, uint64_t frequency, unsigned maxFps);
uint64_t NativeRenderClock_WaitTicks(const struct NativeRenderClock *clock, uint64_t now);
void NativeRenderClock_Presented(struct NativeRenderClock *clock, uint64_t now);

#endif
