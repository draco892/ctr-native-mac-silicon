#include <common.h>

#ifdef CTR_NATIVE
#include <platform/native_model_library.h>
CTR_STATIC_ASSERT(sizeof(((struct GameTracker *)0)->modelPtr) /
    sizeof(((struct GameTracker *)0)->modelPtr[0]) == NATIVE_MODEL_LIBRARY_SLOTS);
#endif

enum LibraryOfModelsConstants
{
	// NOTE(aalhendi): Retail leaves the final modelPtr slot untouched.
	LIBRARY_OF_MODELS_CLEAR_COUNT = 0xe2,
};

void LibraryOfModels_Store(struct GameTracker *gGT, u32 numModels, struct Model **ptrModelArray)
{
	while (numModels != 0)
	{
		struct Model *m = *ptrModelArray;
		if (m == NULL)
		{
			return;
		}
		if (m->id != -1)
		{
#ifdef CTR_NATIVE
			if ((u16)m->id >= NATIVE_MODEL_LIBRARY_SLOTS)
			{
				Platform_LogError("[CTR Native] Model ID outside library bounds\n");
				CTR_TRAP();
			}
#endif
			gGT->modelPtr[m->id] = m;
		}
		numModels--;
		ptrModelArray++;
	}
}

void LibraryOfModels_Clear(struct GameTracker *gGT)
{
	s32 i;

	for (i = 0; i < LIBRARY_OF_MODELS_CLEAR_COUNT; i++)
	{
		gGT->modelPtr[i] = 0;
	}
}
