#include <common.h>

// does not really touch voiceline
void Voiceline_PoolInit(void)
{
	s32 index;

	sdata->criticalSectionCount = 0;

	sdata->numBackup_ChannelStats = 0;

	sdata->ptrCseqHeader = 0;

	Bank_ResetAllocator();

	Audio_SetDefaults();

	LIST_Clear(&sdata->channelFree);
	LIST_Clear(&sdata->channelTaken);

	LIST_Init(&sdata->channelFree, &sdata->channelStatsPrev[0].link.item, sizeof(sdata->channelStatsPrev[0]), NUM_SFX_CHANNELS);

	SpuSetReverbVoice(0, 0xffffff);

	// initialize all members in sound list
	for (index = 0; index < NUM_SFX_CHANNELS; index++)
	{
		struct ChannelStats *stats = &sdata->channelStatsPrev[index];
		struct ChannelAttr *curr;
		sdata->ChannelUpdateFlags[index] = 0;

		SpuSetVoiceADSRAttr(index, 0, 0xf, 0x7f, 2, 0xf, 5, 1, 3);

		stats->flags = 0;
		stats->channelID = index;

		stats->ad = 0x80ff;
		stats->sr = 0x1fc2;

		curr = &sdata->channelAttrCur[index];

		curr->spuStartAddr = (void *)-1;

		curr->ad = 0x80ff;
		curr->sr = 0x1fc2;

		curr->pitch = -1;
		curr->reverb = -1;
		curr->audioL = -1;
		curr->audioR = -1;
	}

	for (index = 0; index < 2; index++)
	{
		struct Song *pool = &sdata->songPool[index];

		// not playing
		pool->flags = 0;

		pool->songPoolIndex = index;
	}

	for (index = 0; index < NUM_SFX_CHANNELS; index++)
	{
		struct SongSeq *seq = &sdata->songSeq[index];

		// not playing
		seq->flags = 0;

		seq->soundID = index;
	}
}

