#include <platform/native_config.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* Shared with the input facade's axis encoding. */
#define NATIVE_CONFIG_MAP_AXIS 0x4000
#define NATIVE_CONFIG_MAP_INVERSE 0x8000

int NativeConfig_ControllerBinding(const char *name)
{
    if (SDL_strcasecmp(name, "none") == 0) return -1;
    if (strncmp(name, "axis:", 5) == 0)
    {
        const char *axisName = name + 5;
        int inverse = *axisName == '-';
        if (inverse) axisName++;
        SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(axisName);
        if (axis == SDL_GAMEPAD_AXIS_INVALID) return -2;
        return axis | NATIVE_CONFIG_MAP_AXIS | (inverse ? NATIVE_CONFIG_MAP_INVERSE : 0);
    }
    SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name);
    return button == SDL_GAMEPAD_BUTTON_INVALID ? -2 : (int)button;
}

int NativeConfig_ValidateBindings(const struct NativeConfig *config, char *error, size_t errorBytes)
{
    for (unsigned i = 0; i < NATIVE_CONFIG_KEYS; i++)
        if (SDL_strcasecmp(config->keyboard[i], "none") != 0 && SDL_GetScancodeFromName(config->keyboard[i]) == SDL_SCANCODE_UNKNOWN)
        {
            if (error != NULL && errorBytes != 0) snprintf(error, errorBytes, "Unknown keyboard key '%s' for input.keyboard.%s", config->keyboard[i], NativeConfig_ControlName(i));
            return 0;
        }
    for (unsigned i = 0; i < NATIVE_CONFIG_CONTROLS; i++)
    {
        int binding = NativeConfig_ControllerBinding(config->controller[i]);
        if (binding < -1 || (binding >= 0 && i >= NATIVE_CONFIG_KEYS && !(binding & NATIVE_CONFIG_MAP_AXIS)))
        {
            if (error != NULL && errorBytes != 0) snprintf(error, errorBytes, "Invalid controller binding '%s' for input.controller.%s", config->controller[i], NativeConfig_ControlName(i));
            return 0;
        }
    }
    return 1;
}
