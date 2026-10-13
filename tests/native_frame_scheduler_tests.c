#include <platform/native_frame_scheduler.h>
#include <platform/native_render_timing.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed at %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static void Cadence(unsigned fps)
{
    struct NativeFrameScheduler clock;
    struct NativeRenderClock render;
    const uint64_t frequency = 1000000000;
    CHECK(NativeFrameScheduler_Init(&clock, frequency, 0));
    CHECK(NativeRenderClock_InitFPS(&render, frequency, fps));
    uint64_t now = 0;
    unsigned blanks = 0, updates = 0, presents = 0;
    while (now <= 10 * frequency)
    {
        unsigned event = NativeFrameScheduler_Poll(&clock, now);
        if (event & NATIVE_FRAME_EVENT_VBLANK) blanks++;
        if (event & NATIVE_FRAME_EVENT_UPDATE) updates++;
        if (NativeRenderClock_WaitTicks(&render, now) == 0)
        {
            NativeRenderClock_Presented(&render, now);
            presents++;
        }
        uint64_t wait = NativeFrameScheduler_WaitTicks(&clock, now);
        uint64_t renderWait = NativeRenderClock_WaitTicks(&render, now);
        if (renderWait < wait) wait = renderWait;
        CHECK(wait > 0);
        now += wait;
    }
    CHECK(blanks == 598 && updates == 299);
    CHECK(presents >= fps * 10 && presents <= fps * 10 + 1);
    CHECK(clock.rebases == 0);
}

int main(void)
{
    const unsigned caps[] = {1, 10, 20, 30, 60, 75, 120, 144, 240};
    for (unsigned i = 0; i < sizeof(caps) / sizeof(caps[0]); i++) Cadence(caps[i]);
    struct NativeFrameScheduler clock;
    CHECK(!NativeFrameScheduler_Init(NULL, 1000000000, 0));
    CHECK(!NativeFrameScheduler_Init(&clock, 0, 0));
    CHECK(!NativeFrameScheduler_Init(&clock, UINT64_MAX, 0));
    CHECK(NativeFrameScheduler_Init(&clock, 1000000000, 0));
    CHECK(NativeFrameScheduler_Poll(&clock, clock.nextVBlank) == NATIVE_FRAME_EVENT_VBLANK);
    uint64_t resume = clock.nextVBlank + 1000000000;
    CHECK(NativeFrameScheduler_Poll(&clock, resume) == 0);
    CHECK(clock.rebases == 1 && clock.phase == 1);
    CHECK(NativeFrameScheduler_Poll(&clock, clock.nextVBlank) ==
          (NATIVE_FRAME_EVENT_VBLANK | NATIVE_FRAME_EVENT_UPDATE));
    CHECK(NativeFrameScheduler_Init(&clock, 1000000000, 0));
    unsigned catchups = 0, updates = 0;
    while (1)
    {
        unsigned event = NativeFrameScheduler_Poll(&clock, 100000000);
        if (!event) break;
        catchups++;
        if (event & NATIVE_FRAME_EVENT_UPDATE) updates++;
    }
    CHECK(catchups == 5 && updates == 2 && clock.rebases == 0);
    CHECK(NativeFrameScheduler_Poll(&clock, 0) == 0);
    CHECK(clock.rebases == 1);
    CHECK(NativeFrameScheduler_WaitTicks(&clock, 0) > 0);
    puts("Fixed NTSC updates remain independent of 1-240 FPS; bounded catch-up and rebasing pass.");
    return 0;
}
