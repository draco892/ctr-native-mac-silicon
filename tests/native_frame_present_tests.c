#include <string.h>
#include <common.h>
#include <platform/native_perf.h>
#include <stdio.h>
#include <stdlib.h>

// Common retail globals are defined for a unity translation unit.
#include "../game/MAIN/MainFrame_Present.c"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

struct sData sdata_static;
static struct GameTracker tracker, expected;
static struct sData expectedData;
static struct GamepadSystem pads, expectedPads;
static u32 ot[1024], expectedOT[1024];
static int draws, scopes;

void NativePerf_BeginScope(enum NativePerfBucket bucket)
{
	CHECK(bucket == NATIVE_PERF_BUCKET_RENDER_SUBMIT);
	scopes++;
}
void NativePerf_EndScope(enum NativePerfBucket bucket)
{
	CHECK(bucket == NATIVE_PERF_BUCKET_RENDER_SUBMIT && scopes == 1);
	scopes--;
}
void DrawOTag(void *packets)
{
	CHECK(packets == &ot[1023]);
	CHECK(tracker.bool_DrawOTag_InProgress == 1);
	draws++;
}

int main(void)
{
	tracker.pushBuffer[0].ptrOT = ot;
	tracker.timer = 81;
	tracker.elapsedTimeMS = 32;
	tracker.frameTimer_VsyncCallback = 162;
	tracker.frameTimer_notPaused = 160;
	tracker.frameTimer_MainFrame_ResetDB = 81;
	tracker.vSync_between_drawSync = 2;
	tracker.clockDurationStall = 123;
	tracker.gameMode1 = PAUSE_ALL;
	tracker.pushBuffer_UI.fadeFromBlack_currentValue = 768;
	tracker.pushBuffer_UI.fade_step = 100;
	sdata->gGT = &tracker;
	sdata->vsyncTillFlip = 2;
	sdata->frameCounter = 81;
	sdata->finalLapTextTimer[0] = 40;
	sdata->trafficLightsTimer_prevFrame = 960;
	for (unsigned i = 0; i < 1024; i++)
		ot[i] = i;
	expected = tracker;
	expected.bool_DrawOTag_InProgress = 1; // The only allowed tracker write.
	expectedData = sdata_static;
	expectedPads = pads;
	memcpy(expectedOT, ot, sizeof(ot));
	for (int i = 0; i < 120; i++)
	{
		MainFrame_RenderFrame(&tracker, &pads);
		CHECK(memcmp(&tracker, &expected, sizeof(tracker)) == 0);
		CHECK(memcmp(&sdata_static, &expectedData, sizeof(sdata_static)) == 0);
		CHECK(memcmp(&pads, &expectedPads, sizeof(pads)) == 0);
		CHECK(memcmp(ot, expectedOT, sizeof(ot)) == 0);
		CHECK(scopes == 0);
	}
	CHECK(draws == 120);
	puts("Repeated native presentation preserves simulation, input, and packet state.");
	return 0;
}
