#include <common.h>

#ifdef CTR_NATIVE
#include <platform/native_perf.h>
#ifdef CTR_NATIVE_GAME_SCENE
#include <platform/native_scene_snapshot_game.h>
#endif

// Prepared OT packets remain valid until the next MainFrame_ResetDB. Neither
// entry point runs game logic, builds new packets, or emits VBlank callbacks.
void RenderSubmit(struct GameTracker *gGT)
{
	NativePerf_BeginScope(NATIVE_PERF_BUCKET_RENDER_SUBMIT);
	gGT->bool_DrawOTag_InProgress = 1;
#ifdef CTR_NATIVE_GAME_SCENE
    DrawOTag(NativeSceneGameSnapshot_Presentation(gGT,NativeVisual_GetStats().alpha));
#else
    DrawOTag(&gGT->pushBuffer[0].ptrOT[0x3ff]);
#endif
	NativePerf_EndScope(NATIVE_PERF_BUCKET_RENDER_SUBMIT);
}

void MainFrame_RenderFrame(struct GameTracker *gGT, struct GamepadSystem *gGamepads)
{
	(void)gGamepads;
	RenderSubmit(gGT);
}
#endif
