#include <SDL3/SDL.h>
#include <common.h>
#include <platform.h>
#include <platform/native_frame_scheduler.h>
#include <platform/native_renderer.h>
#include <platform/native_replay_scheduler.h>
#include <platform/native_log.h>
#include <stdlib.h>

static struct NativeFrameScheduler s_frameScheduler;
static int s_independentFrameTiming, s_preparedFrameValid;

int Platform_UsesIndependentFrameTiming(void)
{
    return s_independentFrameTiming;
}

void Platform_ResetFrameScheduler(void)
{
    s_preparedFrameValid = 0;
    s_frameScheduler.frequency = 0;
}

int Platform_FrameSchedulerExternalVSync(void)
{
    if (!s_independentFrameTiming) return 0;
    // Synchronous retail loaders/video paths own their explicit VBlank waits.
    // Discard cached packets before they can free/change their backing objects.
    Platform_ResetFrameScheduler();
    return 1;
}

static void NativeFrameLoop_Present(struct GameTracker *tracker, struct GamepadSystem *pads)
{
    if (!s_preparedFrameValid || NativeRenderer_PresentationWaitTicks(SDL_GetPerformanceCounter()) != 0)
        return;
    NativeRenderer_SetScheduledPresentation(1);
    Platform_BeginFrame();
    MainFrame_RenderFrame(tracker, pads);
    Platform_EndFrame();
    NativeRenderer_SetScheduledPresentation(0);
}

void Platform_CommitPreparedFrame(struct GameTracker *tracker, struct GamepadSystem *pads)
{
    s_preparedFrameValid = 1;
    NativeFrameLoop_Present(tracker, pads);
}

void Platform_WaitForGameTick(struct GameTracker *tracker, struct GamepadSystem *pads, int allowIndependentTiming)
{
#ifdef CTR_INTERNAL
    allowIndependentTiming = allowIndependentTiming && !NativeReplayScheduler_UsesRetailTiming();
#endif
    if (!allowIndependentTiming)
    {
        if (s_independentFrameTiming)
        {
            // The legacy facade's deadline was suspended while this clock ran.
            s_nextVBlankCounter = 0;
            s_vblankRemainder = 0;
        }
        s_independentFrameTiming = 0;
        Platform_ResetFrameScheduler();
        return;
    }
    s_independentFrameTiming = 1;

    for (;;)
    {
        Platform_PollHostEvents();
        // A save-state hotkey can invalidate the clock while this wait is active.
        if (s_frameScheduler.frequency == 0)
        {
            if (!NativeFrameScheduler_Init(&s_frameScheduler, SDL_GetPerformanceFrequency(), SDL_GetPerformanceCounter()))
            {
                Platform_Log("[CTR Scheduler] invalid host performance clock\n");
                exit(1);
            }
        }
        uint64_t now = SDL_GetPerformanceCounter();
        unsigned event = NativeFrameScheduler_Poll(&s_frameScheduler, now);
        if (event & NATIVE_FRAME_EVENT_VBLANK)
            Native_EmitVBlank();
        if (event & NATIVE_FRAME_EVENT_UPDATE)
        {
            // The update can reset buffers, switch levels or restore a state.
            s_preparedFrameValid = 0;
            return;
        }
        NativeFrameLoop_Present(tracker, pads);
        now = SDL_GetPerformanceCounter();
        uint64_t wait = NativeFrameScheduler_WaitTicks(&s_frameScheduler, now);
        if (s_preparedFrameValid)
        {
            uint64_t renderWait = NativeRenderer_PresentationWaitTicks(now);
            if (renderWait < wait) wait = renderWait;
        }
        // Wake frequently for close/fullscreen/save hotkeys, even with a 1 FPS cap.
        uint64_t maxWait = s_frameScheduler.frequency / 200;
        if (maxWait == 0) maxWait = 1;
        if (wait > maxWait) wait = maxWait;
        if (wait != 0)
            SDL_DelayPrecise((uint64_t)((double)wait * 1e9 / (double)s_frameScheduler.frequency));
    }
}
