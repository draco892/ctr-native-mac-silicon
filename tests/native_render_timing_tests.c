#include <platform/native_render_timing.h>
#include <math.h>
#include <stdio.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(void)
{
    struct NativeRenderClock clock;
    CHECK(NativeRenderClock_Init(&clock, UINT64_C(1000000000)));
    CHECK(NativeRenderClock_WaitTicks(&clock, 0) == 0);
    NativeRenderClock_Presented(&clock, 0); /* Zero is a valid counter value. */
    CHECK(clock.time.frameIndex == 1 && clock.time.deltaSeconds == 0);
    uint64_t period = NativeRenderClock_WaitTicks(&clock, 0);
    double seconds = (double)period / 1e9;
    CHECK(seconds >= 1.0 / 30.0 && seconds < 1.0 / 29.9);

    /* Work and retail VSync count towards the deadline; no second full wait. */
    CHECK(NativeRenderClock_WaitTicks(&clock, period / 2) == period - period / 2);
    CHECK(NativeRenderClock_WaitTicks(&clock, period) == 0);
    NativeRenderClock_Presented(&clock, period);
    CHECK(fabs(clock.time.deltaSeconds - seconds) < 1e-12);
    CHECK(clock.time.deltaSeconds == clock.time.animationDeltaSeconds);

    /* A long load/debugger pause cannot create a burst of catch-up renders. */
    uint64_t stalled = period + UINT64_C(2000000000);
    CHECK(NativeRenderClock_WaitTicks(&clock, stalled) == 0);
    NativeRenderClock_Presented(&clock, stalled);
    CHECK(clock.time.deltaSeconds == 2.0 && clock.time.animationDeltaSeconds == 0.25);
    CHECK(fabs(clock.time.elapsedSeconds - (2.0 + seconds)) < 1e-12);
    CHECK(NativeRenderClock_WaitTicks(&clock, stalled) == period);
    CHECK(NativeRenderClock_WaitTicks(&clock, stalled + period - 1) == 1);

    /* Counter discontinuities and a renderer restart discard stale timing. */
    CHECK(NativeRenderClock_WaitTicks(&clock, 1) == 0);
    NativeRenderClock_Presented(&clock, 1);
    CHECK(clock.time.frameIndex == 1 && clock.time.deltaSeconds == 0 && clock.time.elapsedSeconds == 0);
    CHECK(NativeRenderClock_Init(&clock, 1000));
    NativeRenderClock_Presented(&clock, 100);
    CHECK(NativeRenderClock_WaitTicks(&clock, 100) == 34);
    CHECK(NativeRenderClock_Init(&clock, 0) == 0);
    CHECK(NativeRenderClock_WaitTicks(&clock, 0) == 0);
    NativeRenderClock_Presented(&clock, 100);
    CHECK(clock.time.frameIndex == 0);
    CHECK(NativeRenderClock_Init(&clock, UINT64_MAX) == 0);
    CHECK(NativeRenderClock_Init(NULL, 1000) == 0);
    const unsigned caps[] = {1, 15, 20, 30, 60, 144, 240};
    for (unsigned i = 0; i < sizeof(caps) / sizeof(*caps); i++)
    {
        CHECK(NativeRenderClock_InitFPS(&clock, UINT64_C(1000000000), caps[i]));
        NativeRenderClock_Presented(&clock, 0);
        uint64_t wait = NativeRenderClock_WaitTicks(&clock, 0);
        CHECK((double)wait / 1e9 >= 1.0 / caps[i]);
        CHECK((double)wait / 1e9 < 1.0 / caps[i] + 1e-9);
    }
    CHECK(!NativeRenderClock_InitFPS(&clock, 1000, 0));
    CHECK(!NativeRenderClock_InitFPS(&clock, 1000, 241));
    puts("native render timing tests passed");
    return 0;
}
