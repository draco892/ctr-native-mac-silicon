#include <psx/libgte.h>
#include <psx/libgpu.h>

void (*drawsync_callback)(void) = NULL;

CtrRuntimeAddress DrawSyncCallback(void (*func)(void))
{
    CtrRuntimeAddress old = (CtrRuntimeAddress)drawsync_callback;
    drawsync_callback = func;
    return old;
}
