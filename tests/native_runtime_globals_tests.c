#include <string.h>
#ifdef memmove
#undef memmove
#endif
#include <common.h>
#include <platform/native_credits.h>
#include <platform/native_gpu_links.h>
#include <stdio.h>
#include <stdlib.h>

// Exercise real aggregate declarations and production pool/list/audio logic.
// Only the allocator and CD device boundary are substituted.
struct sData sdata_static;
static struct GameTracker tracker;
static _Alignas(max_align_t) u8 arena[16384];
static size_t used;
static void *readDestination;
static int readSectors, readCount;
static CdlCB readCallback;

void *MEMPACK_AllocMem(s32 size, const char *name)
{
    (void)name;
    size_t offset = (used + _Alignof(max_align_t) - 1) & ~(_Alignof(max_align_t) - 1);
    if (size < 0 || (size_t)size > sizeof(arena) - offset)
        abort();
    used = offset + (size_t)size;
    return arena + offset;
}
void CDSYS_SetMode_StreamData(void) {}
int CdPosToInt(CdlLOC *p) { return p->sector; }
CdlLOC *CdIntToPos(int sector, CdlLOC *p)
{
    memset(p, 0, sizeof(*p));
    p->sector = (u8)sector;
    return p;
}
int CdControl(u8 command, u8 *param, u8 *result)
{
    (void)command; (void)param; (void)result;
    return 1;
}
int CdRead(int sectors, u32 *buffer, int mode)
{
    (void)mode;
    readSectors = sectors;
    readDestination = buffer;
    readCount++;
    return 1;
}
int CdReadSync(int mode, u8 *result) { (void)mode; (void)result; return 0; }
CdlCB CdReadCallback(CdlCB callback)
{
    CdlCB old = readCallback;
    readCallback = callback;
    return old;
}

#include "../game/LIST.c"
#include "../game/JitPool.c"
#include "../game/LOAD/LOAD_Howl.c"
#include "../game/Vehicle/VehTurbo_ProcessBucket.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s at %d\n", #c, __LINE__); return 1; } } while (0)

static int Pools(void)
{
    struct JitPool pool;
    // Deliberately odd payload size requires padding between records.
    JitPool_Init(&pool, 7, (int)sizeof(struct Item) + 3, "test");
    const size_t slotAlignment = CTR_RUNTIME_ABI(sizeof(u32), _Alignof(max_align_t));
    CHECK(pool.itemSize % slotAlignment == 0);
    CHECK(pool.free.count == 7 && pool.taken.count == 0);
    struct Item *items[7];
    for (int i = 0; i < 7; i++)
    {
        items[i] = JitPool_Add(&pool);
        CHECK(items[i] != NULL);
        CHECK((uintptr_t)items[i] % slotAlignment == 0);
        if (sizeof(void *) == 8) CHECK((uintptr_t)items[i] > UINT32_MAX);
        memset((u8 *)items[i] + sizeof(*items[i]), i + 1, 3);
    }
    CHECK(JitPool_Add(&pool) == NULL);
    CHECK(pool.free.count == 0 && pool.taken.count == 7);
    JitPool_Remove(&pool, items[3]);
    CHECK(JitPool_Add(&pool) == items[3]);
    for (int i = 0; i < 7; i++)
        CHECK(*((u8 *)items[i] + sizeof(*items[i])) == i + 1);
    JitPool_Clear(&pool);
    CHECK(pool.free.count == 7 && pool.taken.count == 0);
    struct JitPool oscillators;
    JitPool_Init(&oscillators, 4, sizeof(struct ParticleOscillator), "oscillators");
    for (int i = 0; i < 4; i++)
    {
        struct ParticleOscillator *osc = JitPool_Add(&oscillators);
        CHECK(osc != NULL);
        osc->config.range.max = (s16)(100 + i);
    }
    CHECK(JitPool_Add(&oscillators) == NULL);
    return 0;
}

static int AudioRetry(void)
{
    CdlFILE file = {0};
    file.size = 8192;
    void *destination = arena + 8192;
    CHECK(LOAD_HowlSectorChainStart(&file, destination, 1, 2));
    CHECK(readCount == 1 && readSectors == 2 && readDestination == destination);
    CHECK((CdlFILE *)sdata->howlChainParams[0] == &file);
    CHECK((void *)sdata->howlChainParams[1] == destination);
    CHECK(readCallback == LOAD_HowlCallback);
    readCallback(CdlDiskError, NULL);
    CHECK(!LOAD_HowlSectorChainEnd());
    CHECK(readCount == 2 && readDestination == destination && readSectors == 2);
    readCallback(CdlComplete, NULL);
    CHECK(LOAD_HowlSectorChainEnd());
    CHECK(readCallback == NULL);
    return 0;
}

static int Turbo(void)
{
    const size_t bytes = sizeof(struct Instance) + 4 * sizeof(struct InstDrawPerPlayer);
    struct Instance *primary = calloc(1, bytes), *secondary = calloc(1, bytes), *driverInst = calloc(1, bytes);
    CHECK(primary && secondary && driverInst);
    struct Driver driver = {0};
    driver.instSelf = driverInst;
    struct Turbo turbo = {0};
    turbo.inst = secondary;
    turbo.driver = &driver;
    struct Thread thread = {0};
    thread.inst = primary;
    thread.object = &turbo;
    tracker.numPlyrCurrGame = 4;
    for (int i = 0; i < 4; i++)
    {
        primary->idpp[i].instFlags = secondary->idpp[i].instFlags = DRAW_SUCCESSFUL | HIDE_MODEL;
        driverInst->idpp[i].instFlags = i == 2 ? PUSHBUFFER_EXISTS : 0;
        driverInst->idpp[i].otRangeNormal = (CtrRuntimeAddress)(arena + i * 4);
        driverInst->idpp[i].otRangeSecondary = (CtrRuntimeAddress)(arena + 4096 + i * 4);
        driverInst->idpp[i].depthOffset[0] = (s16)(-30 - i);
        driverInst->idpp[i].depthOffset[1] = (s16)(40 + i);
    }
    VehTurbo_ProcessBucket(&thread);
    for (int i = 0; i < 4; i++)
    {
        CHECK((primary->idpp[i].instFlags & HIDE_MODEL) != 0);
        CHECK(primary->idpp[i].instFlags == secondary->idpp[i].instFlags);
        if (i == 2)
        {
            CHECK((primary->idpp[i].instFlags & DRAW_SUCCESSFUL) != 0);
            CHECK(primary->idpp[i].otRangeNormal == 0);
        }
        else
        {
            CHECK((primary->idpp[i].instFlags & DRAW_SUCCESSFUL) == 0);
            CHECK(primary->idpp[i].otRangeNormal == driverInst->idpp[i].otRangeNormal);
            CHECK(secondary->idpp[i].otRangeSecondary == driverInst->idpp[i].otRangeSecondary);
            CHECK(primary->idpp[i].depthOffset[0] == -30 - i);
            CHECK(secondary->idpp[i].depthOffset[1] == 40 + i);
        }
    }
    free(primary); free(secondary); free(driverInst);
    return 0;
}

static int CreditsAndGpu(void)
{
    u8 file[32] = {0}, saved[32];
    CTR_WriteU32LE(file, sizeof(file));
    CTR_WriteU16LE(file + 4, 3);
    CTR_WriteU32LE(file + 8, 20);
    CTR_WriteU32LE(file + 12, 25);
    CTR_WriteU32LE(file + 16, 20); // shared string
    memcpy(file + 20, "name\0other\0", 11);
    memcpy(saved, file, sizeof(file));
    char *strings[3]; u32 count;
    CHECK(NativeCredits_Decode(file, sizeof(file), strings, 3, &count));
    CHECK(count == 3 && strings[0] == strings[2]);
    CHECK(strcmp(strings[1], "other") == 0);
    CHECK(memcmp(saved, file, sizeof(file)) == 0);
    char *old = strings[0];
    CTR_WriteU32LE(file + 16, sizeof(file));
    CHECK(!NativeCredits_Decode(file, sizeof(file), strings, 3, &count));
    CHECK(count == 0 && strings[0] == old);
    CTR_WriteU32LE(file + 16, 8); // offset cannot point into its own table
    CHECK(!NativeCredits_Decode(file, sizeof(file), strings, 3, &count));
    CHECK(!NativeCredits_Decode(saved, sizeof(saved), strings, 2, &count));
    NativeGpuLinks_Reset();
    CHECK(NativeGpuLinks_RegisterRange(arena, sizeof(arena), NULL));
    u32 packet = 0x12000000;
    setaddr(&packet, arena + 512);
    CHECK(nextPrim(&packet) == arena + 512);
    CHECK((packet >> 24) == 0x12);
    return 0;
}

int main(void)
{
    sdata->gGT = &tracker;
    CHECK(Pools() == 0);
    CHECK(AudioRetry() == 0);
    CHECK(Turbo() == 0);
    CHECK(CreditsAndGpu() == 0);
    puts("native runtime globals: pools, audio retry, turbo, credits and GPU links passed");
    return 0;
}
