#ifndef CTR_SCRATCHPAD_H
#define CTR_SCRATCHPAD_H

#include <macros.h>

#define CTR_SCRATCHPAD_ADDR 0x1f800000u
#define CTR_SCRATCHPAD_SIZE 0x400u

// NOTE(aalhendi): Use these helpers when porting retail scratchpad temporaries.
// On PS1 this is hardware scratchpad RAM at 0x1f800000. On ctr-native, use the
// runtime base set by Platform_InitScratchpad; retail absolute addresses are
// translated back to offsets from the native buffer.
#if defined(CTR_NATIVE)
#include <platform/native_host_scratch.h>
extern u8 *gCTRNativeScratchpadBase;
#define CTR_SCRATCHPAD_BASE                 (gCTRNativeScratchpadBase)
#define CTR_SCRATCHPAD_ADDR_PTR(type, addr) ((type *)NativeScratchpad_GetChecked((u32)(addr)-CTR_SCRATCHPAD_ADDR,sizeof(type),_Alignof(type)))
#define CTR_SCRATCHPAD_PTR(type, offset) ((type *)NativeScratchpad_GetChecked((offset),sizeof(type),_Alignof(type)))
#define CTR_SCRATCHPAD_END(type) ((type *)NativeScratchpad_GetChecked(CTR_SCRATCHPAD_SIZE,0,_Alignof(type)))
#else
#define CTR_SCRATCHPAD_BASE                 ((u8 *)CTR_SCRATCHPAD_ADDR)
#define CTR_SCRATCHPAD_ADDR_PTR(type, addr) ((type *)(void *)(addr))
#endif

#ifndef CTR_NATIVE
#define CTR_SCRATCHPAD_PTR(type, offset) ((type *)(void *)(CTR_SCRATCHPAD_BASE + (offset)))
#define CTR_SCRATCHPAD_END(type) CTR_SCRATCHPAD_PTR(type,CTR_SCRATCHPAD_SIZE)
#endif

// Collision/camera host workspaces preserve full-width pointers.
#if defined(CTR_NATIVE)
#include <platform/native_collision_work.h>
#define CTR_COLLISION_WORK() NativeCollisionWork_Get()
#define CTR_CAMERA_WORK() NativeCameraWork_Get()
#else
#define CTR_COLLISION_WORK() CTR_SCRATCHPAD_PTR(struct ScratchpadStruct, 0x108)
#define CTR_CAMERA_WORK() CTR_SCRATCHPAD_PTR(struct CameraScratchWork, 0x108)
#endif
#endif
