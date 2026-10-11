#ifndef PLATFORM_NATIVE_MEMORY_H
#define PLATFORM_NATIVE_MEMORY_H

#include <macros.h>

struct Mempack;
struct Mempack **Platform_GetActiveMempackSlot(void);
struct Mempack *Platform_GetMempackPools(void);

void Platform_ConfigureMempackArena(void);
void Platform_RepairResidentPointers(s32 activeMempackIndex);
void *Platform_GetMempackBacking(void);
int Platform_GetMempackBackingSize(void);

#endif
