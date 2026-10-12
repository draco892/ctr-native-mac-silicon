#define _CRT_SECURE_NO_WARNINGS
#define SDL_MAIN_HANDLED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include "platform/native_win32.h"
#else
#include <unistd.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define _EnterCriticalSection(x)
#define EnterCriticalSection(x)
#define ExitCriticalSection()

#include "platform/native_assets.h"
#include "platform/native_log.h"
#include "platform/native_memory.h"
#include "platform/native_perf.h"
#include "platform/native_replay_scheduler.h"
#include "platform/native_savestate.h"

#include <platform.h>

#include "game/game_unity.h"

// The host64 executable is opt-in until real gameplay integration is verified.
#if !defined(CTR_ENABLE_EXPERIMENTAL_HOST64_GAME)
CTR_STATIC_ASSERT(sizeof(void *) == 4);
#endif

#include "game/zGlobal_RDATA.c"
#include "game/zGlobal_DATA.c"
#include "game/zGlobal_SDATA.c"

#undef RECT

#include "platform/native_disc_image.c"
#include "platform/native_assets.c"
#include "platform/native_config.c"
#include "platform/native_config_sdl.c"
#include "platform/native_audio.c"
#include "platform/native_host_scratch.c"
#include "platform/native_terrain_work.c"
#include "platform/native_collision_work.c"
#include "platform/native_effect_work.c"
#include "platform/native_memory.c"
#include "platform/native_mempack_arena.c"
#include "platform/native_checkpoint_relocation.c"
#include "platform/native_checkpoint.c"
#include "platform/native_checkpoint_file.c"
#include "platform/native_cd.c"
#include "platform/native_gpu_links.c"
#include "platform/native_gpu.c"
#include "platform/native_gte_core.c"
#include "platform/native_glad.c"
#include "platform/native_input.c"
#include "platform/native_lng.c"
#include "platform/native_credits.c"
#include "platform/native_ptrmap.c"
#include "platform/native_asset_readers.c"
#include "platform/native_asset_loading.c"
#include "platform/native_model_library.c"
#include "platform/native_model_animation.c"
#include "platform/native_model_vertices.c"
#include "platform/native_model_transform.c"
#include "platform/native_model_matrix.c"
#include "platform/native_model_projection.c"
#include "platform/native_model_commands.c"
#include "platform/native_vram.c"
#include "platform/native_model_draw.c"
#include "platform/native_raster.c"
#include "platform/native_instance_transform.c"
#include "platform/native_mesh_geometry.c"
#include "platform/native_terrain_material.c"
#include "platform/native_scene_camera.c"
#include "platform/native_scene_visibility.c"
#include "platform/native_resident.c"
#include "platform/native_resident_graph.c"
#include "platform/native_scene_assets.c"
#include "platform/native_vertex_animation.c"
#include "platform/native_scene_geometry.c"
#include "platform/native_scene_render.c"
#include "platform/native_scene_gpu.c"
#include "platform/native_inline_c.c"
#include "platform/native_libapi.c"
#include "platform/native_libetc.c"
#include "platform/native_libgte.c"
#include "platform/native_libgpu.c"
#include "platform/native_gpu_callbacks.c"
#include "platform/native_libpad.c"
#include "platform/native_libspu.c"
#include "platform/native_log.c"
#include "platform/native_memcard.c"
#include "platform/native_memcard_adapter.c"
#include "platform/native_perf.c"
#include "platform/native_platform.c"
#include "platform/native_replay_scheduler.c"
#include "platform/native_render_timing.c"
#include "platform/native_renderer.c"
#include "platform/native_savestate.c"
#include "platform/native_state.c"
#include "platform/native_str.c"

#ifndef CC
#if defined(__GNUC__)
#if _WIN32
#ifndef __clang__
#define CC "MINGW-GCC"
#else
#define CC "MINGW-CLANG"
#endif
#else
#ifndef __clang__
#define CC "GCC"
#else
#define CC "CLANG"
#endif
#endif
#elif defined(_MSC_VER)
#define CC "MSVC"
#else
#define CC "Unknown"
#endif
#endif

#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "0.0.0-dev"
#endif

#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif

static int NativeConsole_ShouldPauseOnError(void)
{
#if defined(_WIN32)
	DWORD consoleProcesses[2];
	DWORD consoleProcessCount;

	if (GetConsoleWindow() == NULL)
		return 0;

	consoleProcessCount = GetConsoleProcessList(consoleProcesses, (DWORD)(sizeof(consoleProcesses) / sizeof(consoleProcesses[0])));
	return (consoleProcessCount == 1) && (consoleProcesses[0] == GetCurrentProcessId());
#else
	return 0;
#endif
}

static s32 NativeConsole_Return(const u32 result)
{
	if ((result != 0) && NativeConsole_ShouldPauseOnError())
	{
		fflush(stdout);
		fflush(stderr);
		fprintf(stderr, "\n[CTR Native] Press Enter to close this window...");
		fflush(stderr);

		while (getchar() != '\n' && !feof(stdin))
		{
		}
	}

	return (s32)result;
}

// TODO(aalhendi): just make an argparser?
static int NativeArg_IsVersion(const char *arg)
{
	return (arg != NULL) && ((strcmp(arg, "--version") == 0) || (strcmp(arg, "-v") == 0));
}


int main(int argc, char *argv[])
{
	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if (NativeArg_IsVersion(argv[argIndex]))
		{
			printf("CTR Native %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
			return 0;
		}
	}
	const char *configPath = NULL;
	int checkConfig = 0;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--check-config") == 0) checkConfig = 1;
		else if (strcmp(argv[i], "--config") == 0)
		{
			if (configPath != NULL || i + 1 == argc || strncmp(argv[i + 1], "--", 2) == 0)
			{
				fprintf(stderr, "[CTR Config] --config requires one file path.\n");
				return 1;
			}
			configPath = argv[++i];
		}
	}

	printf("[CTR Native] Starting...\n");
	fflush(stdout);

	const char *sdlBasePath = SDL_GetBasePath();
	printf("[CTR Native] SDL base path: %s\n", sdlBasePath ? sdlBasePath : "(null)");
	fflush(stdout);

	if (!NativeAssets_Init(sdlBasePath))
	{
		fprintf(stderr, "[CTR Native] Failed to initialize asset paths.\n");
		return NativeConsole_Return(1);
	}
	/* Resolve explicit relative paths before switching to the assets base dir. */
	char defaultConfigPath[1024], configError[256];
	int explicitConfig = configPath != NULL;
	if (configPath == NULL)
	{
		if (!NativePath_Join(defaultConfigPath, sizeof(defaultConfigPath), NativeStr8_FromCString(NativeAssets_GetBaseDir()), NATIVE_STR8_LIT("config.json")))
		{
			fprintf(stderr, "[CTR Config] Config path is too long.\n");
			return 1;
		}
		configPath = defaultConfigPath;
	}
	NativeConfig_Defaults(&gNativeConfig);
	int configLoaded = NativeConfig_Load(configPath, &gNativeConfig, configError, sizeof(configError));
	if (configLoaded < 0 || (configLoaded == 0 && (explicitConfig || checkConfig)))
	{
		fprintf(stderr, "[CTR Config] %s: %s\n", configPath, configLoaded == 0 ? "File not found" : configError);
		return 1;
	}
	if (!NativeConfig_ValidateBindings(&gNativeConfig, configError, sizeof(configError)))
	{
		fprintf(stderr, "[CTR Config] %s: %s\n", configPath, configError);
		return 1;
	}
	printf("[CTR Config] %s: %s; %dx%d %s, max_fps=%d, keyboard.cross=%s\n", configPath,
	       configLoaded ? "loaded" : "missing, using defaults", gNativeConfig.width, gNativeConfig.height,
	       gNativeConfig.fullscreen ? "fullscreen" : "windowed", gNativeConfig.maxFps, gNativeConfig.keyboard[3]);
	if (checkConfig) return 0;

	printf("[CTR Native] Version: %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
	printf("[CTR Native] Built with: " CC "\n");
	printf("[CTR Native] Base: %s\n", NativeAssets_GetBaseDir());
	printf("[CTR Native] Assets: %s\n", NativeAssets_GetAssetDir());
	fflush(stdout);

	if (chdir(NativeAssets_GetBaseDir()) != 0)
	{
		fprintf(stderr, "[CTR Native] Failed to enter base directory: %s\n", NativeAssets_GetBaseDir());
		return NativeConsole_Return(1);
	}

	if (!NativeAssets_Validate())
	{
		return NativeConsole_Return(1);
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_PrepareReportFromArgs(argc, argv) != 0)
	{
		return NativeConsole_Return(1);
	}
#endif

#ifdef USE_16BY9
	printf("[CTR Native] Widescreen\n");
#else
	printf("[CTR Native] 4:3\n");
#endif
	Platform_Init("Crash Team Racing", gNativeConfig.width, gNativeConfig.height);

#if defined(CTR_INTERNAL)
	if (NativePerf_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		NativeSceneAssets_Reset(&gNativeSceneAssets);
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#endif

	Platform_InitScratchpad();
	Platform_RepairResidentPointers(0);

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		NativeSceneAssets_Reset(&gNativeSceneAssets);
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#else
	(void)argc;
	(void)argv;
#endif

	const int result = CTR_Main();

	NativeSceneAssets_Reset(&gNativeSceneAssets);
	Platform_Shutdown();
	return NativeConsole_Return(result);
}
