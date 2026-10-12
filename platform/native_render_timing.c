#include <platform/native_render_timing.h>
#include <string.h>

#define NTSC_GPU_CLOCK_HZ UINT64_C(53693175)
#define FRAME_GPU_CYCLES UINT64_C(1795238) /* 2 * 3413 * 263 */

int NativeRenderClock_Init(struct NativeRenderClock *clock, uint64_t frequency)
{
    if (clock == NULL) return 0;
    memset(clock, 0, sizeof(*clock));
    if (frequency == 0 || frequency > UINT64_MAX / FRAME_GPU_CYCLES) return 0;
    clock->frequency = frequency;
    uint64_t numerator = frequency * FRAME_GPU_CYCLES;
    /* Round up so presentation never exceeds the target, even on a coarse clock. */
    clock->interval = numerator / NTSC_GPU_CLOCK_HZ + (numerator % NTSC_GPU_CLOCK_HZ != 0);
    return 1;
}

int NativeRenderClock_InitFPS(struct NativeRenderClock *clock, uint64_t frequency, unsigned maxFps)
{
    if (clock == NULL) return 0;
    memset(clock, 0, sizeof(*clock));
    if (frequency == 0 || maxFps == 0 || maxFps > 240) return 0;
    clock->frequency = frequency;
    clock->interval = frequency / maxFps + (frequency % maxFps != 0);
    return 1;
}

uint64_t NativeRenderClock_WaitTicks(const struct NativeRenderClock *clock, uint64_t now)
{
    if (clock == NULL || clock->frequency == 0 || clock->time.frameIndex == 0 || now < clock->lastPresent) return 0;
    uint64_t elapsed = now - clock->lastPresent;
    return elapsed < clock->interval ? clock->interval - elapsed : 0;
}

void NativeRenderClock_Presented(struct NativeRenderClock *clock, uint64_t now)
{
    if (clock == NULL || clock->frequency == 0) return;
    if (clock->time.frameIndex == 0 || now < clock->lastPresent)
    {
        /* A fresh/restarted host clock has no previous presentation interval. */
        clock->origin = now;
        memset(&clock->time, 0, sizeof(clock->time));
    }
    else
    {
        clock->time.deltaSeconds = (double)(now - clock->lastPresent) / (double)clock->frequency;
        clock->time.animationDeltaSeconds = clock->time.deltaSeconds > 0.25 ? 0.25 : clock->time.deltaSeconds;
        clock->time.elapsedSeconds = (double)(now - clock->origin) / (double)clock->frequency;
    }
    clock->lastPresent = now;
    clock->time.frameIndex++;
    /* The next deadline starts here, not at an overdue target. No catch-up burst. */
}
