#ifndef NATIVE_CONFIG_H
#define NATIVE_CONFIG_H

#include <stddef.h>

#define NATIVE_CONFIG_KEYS 16
#define NATIVE_CONFIG_CONTROLS 20
#define NATIVE_CONFIG_NAME_BYTES 32
#define NATIVE_CONFIG_MAX_BYTES 16384

struct NativeConfig
{
    int width, height, fullscreen, maxFps, bilinearFiltering, interpolation;
    int keyboardSlot, axisDeadzone;
    char keyboard[NATIVE_CONFIG_KEYS][NATIVE_CONFIG_NAME_BYTES];
    char controller[NATIVE_CONFIG_CONTROLS][NATIVE_CONFIG_NAME_BYTES];
};

extern struct NativeConfig gNativeConfig;
const char *NativeConfig_ControlName(unsigned index);
void NativeConfig_Defaults(struct NativeConfig *config);
/* Atomic: invalid JSON/schema never modifies the caller's configuration. */
int NativeConfig_Parse(const char *json, size_t bytes, struct NativeConfig *config, char *error, size_t errorBytes);
/* Returns 1 loaded, 0 missing, -1 invalid/unreadable. */
int NativeConfig_Load(const char *path, struct NativeConfig *config, char *error, size_t errorBytes);
/* SDL name validation without opening a window or a controller. */
int NativeConfig_ValidateBindings(const struct NativeConfig *config, char *error, size_t errorBytes);
int NativeConfig_ControllerBinding(const char *name);

#endif
