#ifndef NATIVE_SCENE_LAYOUT_H
#define NATIVE_SCENE_LAYOUT_H
#include <stdint.h>
/* Outer OTs can be discontiguous and are allocated in reverse player order.
 * Nested OTs inherit their caller's view; numeric ordering proves nothing. */
struct NativeSceneLayout { const uint32_t *world[4],*ui; unsigned views; };
static inline int NativeSceneLayout_Locate(const struct NativeSceneLayout *layout,const uint32_t *packet,unsigned *view,unsigned *bucket)
{
    uintptr_t address=(uintptr_t)packet;
    for(unsigned i=0;i<layout->views && i<4;i++) {
        uintptr_t base=(uintptr_t)layout->world[i];
        if(base && address>=base && address-base<1024*sizeof(uint32_t) && !((address-base)%sizeof(uint32_t))) {
            *view=i;*bucket=(unsigned)((address-base)/sizeof(uint32_t));return 1;
        }
    }
    uintptr_t base=(uintptr_t)layout->ui;
    if(base && address>=base && address-base<5*sizeof(uint32_t) && !((address-base)%sizeof(uint32_t))) {
        *view=4;*bucket=0;return 1;
    }
    return 0;
}
#endif
