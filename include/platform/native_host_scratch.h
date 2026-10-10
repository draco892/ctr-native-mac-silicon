#ifndef NATIVE_HOST_SCRATCH_H
#define NATIVE_HOST_SCRATCH_H
#include <macros.h>
enum NativeHostScratchKey { NATIVE_HOST_SCRATCH_RENDER_BUCKET, NATIVE_HOST_SCRATCH_PARTICLES, NATIVE_HOST_SCRATCH_COUNT };
// Host workspaces never overlap the 1 KiB retail byte scratchpad. No allocation.
void NativeHostScratch_Reset(void);
void *NativeHostScratch_Get(enum NativeHostScratchKey key,size_t bytes,size_t alignment);
void *NativeHostScratch_Storage(void);
size_t NativeHostScratch_StorageSize(void);
void *NativeScratchpad_At(size_t offset,size_t bytes,size_t alignment);
void *NativeScratchpad_GetChecked(size_t offset,size_t bytes,size_t alignment);
#endif
