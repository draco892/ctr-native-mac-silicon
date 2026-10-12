#include <platform/native_config.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #expr, error); return 1; } } while (0)

int main(void)
{
    char error[256] = "";
    struct NativeConfig config, original;
    NativeConfig_Defaults(&config);
    CHECK(config.width == 800 && config.height == 600 && config.maxFps == 30 && config.fullscreen == 0);
    CHECK(strcmp(config.keyboard[3], "C") == 0 && strcmp(config.controller[3], "a") == 0);
    const char *custom = "{\"video\":{\"width\":1920,\"height\":1080,\"fullscreen\":true,\"max_fps\":20,\"bilinear_filtering\":true},"
                         "\"input\":{\"keyboard_slot\":2,\"axis_deadzone\":3000,\"keyboard\":{\"cross\":\"\\u0059\"},"
                         "\"controller\":{\"cross\":\"b\",\"axis_left_x\":\"axis:-leftx\"}}}";
    CHECK(NativeConfig_Parse(custom, strlen(custom), &config, error, sizeof(error)));
    CHECK(config.width == 1920 && config.height == 1080 && config.fullscreen && config.maxFps == 20 && config.bilinearFiltering);
    CHECK(config.keyboardSlot == 2 && config.axisDeadzone == 3000 && strcmp(config.keyboard[3], "Y") == 0);
    CHECK(strcmp(config.keyboard[0], "X") == 0 && strcmp(config.controller[16], "axis:-leftx") == 0);
    original = config;
    const char *invalid[] = {
        "", "[]", "null", "{", "{}garbage", "{}\0trailing",
        "{\"video\":{\"width\":0}}", "{\"video\":{\"width\":7681}}",
        "{\"video\":{\"height\":4321}}", "{\"video\":{\"height\":239}}",
        "{\"video\":{\"max_fps\":0}}", "{\"video\":{\"max_fps\":241}}",
        "{\"video\":{\"max_fps\":30.0}}", "{\"video\":{\"max_fps\":3e1}}",
        "{\"video\":{\"max_fps\":030}}", "{\"video\":{\"max_fps\":-1}}",
        "{\"video\":{\"fullscreen\":1}}", "{\"video\":{\"fullscreen\":truefalse}}",
        "{\"video\":{\"width\":800,\"width\":900}}", "{\"input\":{},\"input\":{}}",
        "{\"video\":{\"widht\":800}}", "{\"other\":{}}", "{\"video\":{\"video\":{}}}",
        "{\"input\":{\"keyboard_slot\":4}}", "{\"input\":{\"axis_deadzone\":32767}}",
        "{\"input\":{\"keyboard\":{\"cross\":null}}}", "{\"input\":{\"keyboard\":{\"cross\":\"\\u0000\"}}}",
        "{\"input\":{\"keyboard\":{\"cross\":\"\\q\"}}}", "{\"input\":{\"keyboard\":{\"cross\":\"\\u123\"}}}",
        "{\"input\":{\"keyboard\":{\"cross\":\"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\"}}}",
        "{\"video\":{},}", "{\"video\":[]}", "{\"video\":{\"width\":9999999999999999999999999999}}"
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
    {
        /* Explicitly include the embedded NUL in this case. */
        size_t length = i == 5 ? 11 : strlen(invalid[i]);
        CHECK(!NativeConfig_Parse(invalid[i], length, &config, error, sizeof(error)));
        CHECK(error[0] != 0 && memcmp(&config, &original, sizeof(config)) == 0);
    }
    for (size_t i = 0; i < strlen(custom); i++)
        CHECK(!NativeConfig_Parse(custom, i, &config, error, sizeof(error)));
    CHECK(NativeConfig_Parse("{}", 2, &config, error, sizeof(error)) && config.maxFps == 30 && !config.fullscreen);
    CHECK(!NativeConfig_Parse("{}", NATIVE_CONFIG_MAX_BYTES + 1, &config, error, sizeof(error)));
    CHECK(!NativeConfig_Parse(NULL, 0, &config, error, sizeof(error)));
    puts("native config tests passed");
    return 0;
}
