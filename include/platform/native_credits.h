#ifndef PLATFORM_NATIVE_CREDITS_H
#define PLATFORM_NATIVE_CREDITS_H
#include <macros.h>

// Credits use an inline four-byte offset table after an eight-byte header.
// The caller owns both the unchanged wire copy and the separate host table.
int NativeCredits_Decode(const void *file, size_t fileSize, char **strings,
                         size_t capacity, u32 *count);
#endif
