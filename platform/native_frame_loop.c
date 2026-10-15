#include <SDL3/SDL.h>
#include <common.h>
#include <platform.h>
#include <platform/native_frame_scheduler.h>
#include <platform/native_renderer.h>
#include <platform/native_replay_scheduler.h>
#include <platform/native_log.h>
#include <stdlib.h>
#ifdef CTR_NATIVE_GAME_SCENE
#include <platform/native_scene_snapshot_game.h>
#endif

static struct NativeFrameScheduler s_frameScheduler;
static int s_independentFrameTiming, s_preparedFrameValid;
static uint64_t s_visualTickCounter;

void Platform_BeginVisualCapture(struct GameTracker *tracker)
{
#ifdef CTR_NATIVE_VISUAL_INTERPOLATION
    static struct { SVec3 pos,rot; } previousCamera[4];
    static int previousPlayers;
    int enabled=s_independentFrameTiming && gNativeConfig.maxFps>30 && gNativeConfig.interpolation &&
        sdata->Loading.stage==LOAD_IDLE && tracker->numPlyrCurrGame>=1 && tracker->numPlyrCurrGame<=4 &&
        !(tracker->gameMode1 & LOADING);
    uint64_t signature=(uint64_t)(uintptr_t)tracker->level1 ^ ((uint64_t)(unsigned)tracker->levelID<<32) ^
        (unsigned)tracker->gameMode1 ^ ((uint64_t)(unsigned)tracker->numPlyrCurrGame<<56) ^ (uint64_t)(uintptr_t)sdata->ptrActiveMenu;
    for(int i=0;i<tracker->numPlyrCurrGame && i<4;i++) {
        const struct PushBuffer *camera=&tracker->pushBuffer[i];
        signature=signature*UINT64_C(1099511628211) ^ tracker->cameraDC[i].mode ^
            ((uint64_t)tracker->cameraDC[i].nearOrFar<<32) ^ (uint64_t)(uintptr_t)tracker->cameraDC[i].driverToFollow ^
            ((uint64_t)(unsigned short)camera->rect.w<<16) ^ (unsigned short)camera->rect.h ^
            ((uint64_t)(unsigned short)camera->rect.x<<48) ^ ((uint64_t)(unsigned short)camera->rect.y<<32);
        if(previousPlayers==tracker->numPlyrCurrGame) {
            const short *now=&camera->pos.x,*last=&previousCamera[i].pos.x;
            const short *rot=&camera->rot.x,*lastRot=&previousCamera[i].rot.x;
            for(int axis=0;axis<3;axis++) {
                int angle=((rot[axis]-lastRot[axis]+2048)&4095)-2048;
                if(abs(now[axis]-last[axis])>1024 || abs(angle)>512) NativeVisual_Reset();
            }
        }
        previousCamera[i].pos=camera->pos; previousCamera[i].rot=camera->rot;
    }
    previousPlayers=tracker->numPlyrCurrGame;
    NativeVisual_BeginCapture(enabled,signature);
    for(int i=0;i<tracker->numPlyrCurrGame && i<4;i++) {
        const struct PushBuffer *pb=&tracker->pushBuffer[i];
        struct NativeVisualProjection camera={.h=(float)pb->distanceToScreen_PREV,.maxRatio=4,
            .offset={(float)(pb->rect.w/2),(float)(pb->rect.h/2)}};
        for(unsigned row=0;row<3;row++) {
            float translation=0;
            for(unsigned col=0;col<3;col++) {
                camera.matrix[row][col]=pb->matrix_ViewProj.m[row][col]/4096.0f;
                translation-=camera.matrix[row][col]*(float)pb->matrix_Camera.t[col];
            }
            camera.translation[row]=floorf(translation);
        }
        NativeVisual_Camera((unsigned)i,&camera);
    }
#ifdef CTR_NATIVE_GAME_SCENE
    NativeSceneGameSnapshot_Begin(tracker,enabled,signature^NativeVisual_ResetEpoch());
#endif
#else
    (void)tracker;
#endif
}

int Platform_UsesIndependentFrameTiming(void)
{
    return s_independentFrameTiming;
}

void Platform_ResetFrameScheduler(void)
{
    s_preparedFrameValid = 0;
    s_frameScheduler.frequency = 0;
    NativeVisual_Reset();
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
    float alpha=1;
    if(s_independentFrameTiming && s_frameScheduler.frequency && SDL_GetPerformanceCounter()>=s_visualTickCounter)
        alpha=(float)((double)(SDL_GetPerformanceCounter()-s_visualTickCounter)*NATIVE_FRAME_GPU_HZ /
            ((double)s_frameScheduler.frequency*NATIVE_FRAME_VBLANK_CYCLES*2));
    NativeVisual_EnableWorldSampling(0);
    NativeVisual_BeginScenePresentation(alpha);
    Platform_BeginFrame();
    MainFrame_RenderFrame(tracker, pads);
    Platform_EndFrame();
    NativeVisual_EndPresentation();
    NativeRenderer_SetScheduledPresentation(0);
}

void Platform_CommitPreparedFrame(struct GameTracker *tracker, struct GamepadSystem *pads)
{
    s_preparedFrameValid = 1;
    NativeVisual_EndCapture();
#ifdef CTR_NATIVE_GAME_SCENE
    NativeSceneGameSnapshot_Seal();
#endif
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
        uint64_t deadline=s_frameScheduler.nextVBlank, rebases=s_frameScheduler.rebases;
        unsigned event = NativeFrameScheduler_Poll(&s_frameScheduler, now);
        if(s_frameScheduler.rebases!=rebases) NativeVisual_Reset();
        if (event & NATIVE_FRAME_EVENT_VBLANK)
            Native_EmitVBlank();
        if (event & NATIVE_FRAME_EVENT_UPDATE)
        {
            s_visualTickCounter=deadline;
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
