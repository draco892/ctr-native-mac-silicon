#ifndef PLATFORM_NATIVE_COLLISION_WORK_H
#define PLATFORM_NATIVE_COLLISION_WORK_H
#include <macros.h>
struct ScratchpadStruct;
struct CameraScratchWork;
struct ScratchpadStruct *NativeCollisionWork_Get(void);
struct CameraScratchWork *NativeCameraWork_Get(void);
// Visit object-pointer slots and image callbacks separately. Caller controls
// rebasing; visitors never dereference any saved pointer value.
typedef void (*NativeCollisionPointerVisitor)(void *user,void *slot,u32 width,int image);
void NativeCollisionWork_VisitPointers(struct ScratchpadStruct *work,NativeCollisionPointerVisitor visit,void *user);
void NativeCollisionWork_VisitHostPointers(NativeCollisionPointerVisitor visit,void *user);
#endif
