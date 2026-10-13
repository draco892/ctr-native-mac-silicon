#include <SDL3/SDL.h>
#include <string.h>
#include <common.h>
#include <platform/native_render_timing.h>
#include <platform/native_perf.h>
#include <platform/native_log.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed at %d: %s\n", __LINE__, #c); exit(1); } } while (0)

struct sData sdata_static;
static struct GameTracker tracker;
static struct GamepadSystem pads;
static struct NativeRenderClock render;
static uint64_t hostNow, s_nextVBlankCounter, s_vblankRemainder;
static int blanks, inputPolls, audioSteps, draws, scheduled, retailReplay;
static int resetDuringWait;
static u32 ot[1024];

void Platform_ResetFrameScheduler(void);

Uint64 SDL_GetPerformanceCounter(void) { return hostNow; }
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000000; }
void SDL_DelayPrecise(Uint64 ns) { CHECK(ns > 0); hostNow += ns; }
void Platform_PollHostEvents(void)
{
    if (resetDuringWait > 0 && --resetDuringWait == 0)
    {
        resetDuringWait = 0;
        Platform_ResetFrameScheduler();
    }
}
void Platform_Log(const char *format, ...) { (void)format; }
int NativeReplayScheduler_UsesRetailTiming(void) { return retailReplay; }
uint64_t NativeRenderer_PresentationWaitTicks(uint64_t now) { return NativeRenderClock_WaitTicks(&render, now); }
void NativeRenderer_SetScheduledPresentation(int value) { scheduled = value; }
void Platform_BeginFrame(void) {}
void Platform_EndFrame(void)
{
    CHECK(scheduled);
    hostNow += 1000000; // Model a real 1 ms host GPU/swap cost.
    NativeRenderClock_Presented(&render, hostNow);
}
void NativePerf_BeginScope(enum NativePerfBucket bucket) { (void)bucket; }
void NativePerf_EndScope(enum NativePerfBucket bucket) { (void)bucket; }
void DrawOTag(void *packets)
{
    CHECK(packets == &ot[1023]);
    CHECK(audioSteps == blanks && inputPolls == blanks);
    draws++;
}

// Exercise the production VBlank callback and root counter facade.
#include "../platform/native_libapi.c"
#include "../game/MAIN/MainDrawCb.c"
void howl_PlayAudio_Update(void) { audioSteps++; }
int Platform_PollInput(void) { inputPolls++; return 0; }
void GAMEPAD_PollVsync(struct GamepadSystem *system) { CHECK(system == &pads); }
static void Native_EmitVBlank(void)
{
    NativeRCnt_EmitVBlank();
    MainDrawCb_Vsync();
    blanks++;
}
#include "../game/MAIN/MainFrame_Present.c"
#include "../platform/native_frame_loop.c"

static int Run(unsigned fps)
{
    memset(&tracker, 0, sizeof(tracker));
    memset(&sdata_static, 0, sizeof(sdata_static));
    hostNow = 0;
    blanks = inputPolls = audioSteps = draws = scheduled = 0;
    s_rootCounterValue = s_rootCounterBase = 0;
    sdata->gGT = &tracker;
    sdata->gGamepads = &pads;
    tracker.pushBuffer[0].ptrOT = ot;
    Platform_ResetFrameScheduler();
    CHECK(NativeRenderClock_InitFPS(&render, 1000000000, fps));
    int position = 0;
    for (int tick = 0; tick < 150; tick++)
    {
        Platform_WaitForGameTick(&tracker, &pads, 1);
        CHECK(blanks == (tick + 1) * 2);
        CHECK(tracker.frameTimer_VsyncCallback == blanks);
        CHECK(Platform_UsesIndependentFrameTiming());
        position += 32; // Fixture physics step; GPU work must never feed it.
        tracker.timer++;
        sdata->vsyncTillFlip = 2;
        hostNow += 2000000; // Model 2 ms of simulation/packet preparation.
        Platform_CommitPreparedFrame(&tracker, &pads);
    }
    CHECK(position == 4800 && blanks == 300 && audioSteps == 300 && inputPolls == 300);
    CHECK(s_rootCounterValue == 300 * 263 && sdata->rcntTotalUnits == 300 * 263);
    CHECK(hostNow >= 5010000000 && hostNow < 5030000000);
    CHECK(draws > 0 && !scheduled);
    return draws;
}

int main(void)
{
    int low = Run(10), normal = Run(30), high = Run(60), highest = Run(120);
    CHECK(low < normal && normal < high && high < highest);
    CHECK(low >= 49 && low <= 51);
    printf("Presentations with 1 ms GPU work and 2 ms tick work: 10=%d 30=%d 60=%d 120=%d\n", low, normal, high, highest);
    CHECK(high >= normal * 18 / 10 && highest >= normal * 34 / 10);
    int before = draws;
    resetDuringWait = 2; // Model F8 restoring a checkpoint inside the wait loop.
    Platform_WaitForGameTick(&tracker, &pads, 1);
    CHECK(resetDuringWait == 0 && s_frameScheduler.frequency != 0 && !s_preparedFrameValid);
    CHECK(draws == before);
    Platform_ResetFrameScheduler(); // Checkpoint/buffer invalidation.
    Platform_WaitForGameTick(&tracker, &pads, 1);
    CHECK(draws == before);
    CHECK(Platform_FrameSchedulerExternalVSync());
    CHECK(s_frameScheduler.frequency == 0 && !s_preparedFrameValid);
    retailReplay = 1;
    uint64_t previous = hostNow;
    Platform_WaitForGameTick(&tracker, &pads, 1);
    CHECK(hostNow == previous && !Platform_UsesIndependentFrameTiming());
    CHECK(!Platform_FrameSchedulerExternalVSync());
    Platform_CommitPreparedFrame(&tracker, &pads);
    CHECK(hostNow - previous <= 1000000); // No 10 FPS blocking sleep on commit.
    puts("Production frame loop: identical updates/VBlank/audio/input at 10/30/60/120 FPS; invalidation and replay compatibility pass.");
    return 0;
}
