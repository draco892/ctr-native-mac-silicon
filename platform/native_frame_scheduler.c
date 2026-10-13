#include <platform/native_frame_scheduler.h>
#include <stddef.h>
#include <string.h>

static void NativeFrameScheduler_Advance(struct NativeFrameScheduler *clock)
{
    uint64_t numerator = clock->frequency * NATIVE_FRAME_VBLANK_CYCLES;
    clock->nextVBlank += numerator / NATIVE_FRAME_GPU_HZ;
    clock->remainder += numerator % NATIVE_FRAME_GPU_HZ;
    if (clock->remainder >= NATIVE_FRAME_GPU_HZ)
    {
        clock->nextVBlank++;
        clock->remainder -= NATIVE_FRAME_GPU_HZ;
    }
}

int NativeFrameScheduler_Init(struct NativeFrameScheduler *clock, uint64_t frequency, uint64_t now)
{
    if (clock == NULL) return 0;
    memset(clock, 0, sizeof(*clock));
    if (frequency < 60 || frequency > UINT64_MAX / NATIVE_FRAME_VBLANK_CYCLES / NATIVE_FRAME_MAX_CATCHUP_VBLANKS)
        return 0;
    clock->frequency = frequency;
    clock->lastNow = clock->nextVBlank = now;
    NativeFrameScheduler_Advance(clock);
    return 1;
}

unsigned NativeFrameScheduler_Poll(struct NativeFrameScheduler *clock, uint64_t now)
{
    if (clock == NULL || clock->frequency == 0) return NATIVE_FRAME_EVENT_NONE;
    uint64_t step = clock->frequency * NATIVE_FRAME_VBLANK_CYCLES / NATIVE_FRAME_GPU_HZ;
    if (now < clock->lastNow || (now >= clock->nextVBlank &&
        now - clock->nextVBlank >= step * NATIVE_FRAME_MAX_CATCHUP_VBLANKS))
    {
        /* Drop pathological host debt, preserving an unfinished VBlank pair.
         * No burst of physics/audio/input after a window drag/debugger stop. */
        clock->nextVBlank = now;
        clock->remainder = 0;
        clock->rebases++;
        NativeFrameScheduler_Advance(clock);
    }
    clock->lastNow = now;
    if (now < clock->nextVBlank) return NATIVE_FRAME_EVENT_NONE;
    NativeFrameScheduler_Advance(clock);
    clock->phase ^= 1u;
    return NATIVE_FRAME_EVENT_VBLANK | (clock->phase == 0 ? NATIVE_FRAME_EVENT_UPDATE : 0);
}

uint64_t NativeFrameScheduler_WaitTicks(const struct NativeFrameScheduler *clock, uint64_t now)
{
    if (clock == NULL || clock->frequency == 0 || now >= clock->nextVBlank) return 0;
    return clock->nextVBlank - now;
}
