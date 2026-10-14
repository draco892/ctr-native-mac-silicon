#include <platform/native_config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NativeConfig gNativeConfig;
static const char *const configControlNames[NATIVE_CONFIG_CONTROLS] = {
    "square", "circle", "triangle", "cross", "l1", "l2", "l3", "r1", "r2", "r3",
    "start", "select", "left", "right", "up", "down",
    "axis_left_x", "axis_left_y", "axis_right_x", "axis_right_y"
};

const char *NativeConfig_ControlName(unsigned index)
{
    return index < NATIVE_CONFIG_CONTROLS ? configControlNames[index] : NULL;
}

void NativeConfig_Defaults(struct NativeConfig *config)
{
    static const char *const keys[NATIVE_CONFIG_KEYS] = {
        "X", "V", "Z", "C", "Left Shift", "Left Ctrl", "[", "Right Shift", "Right Ctrl", "]",
        "Return", "Space", "Left", "Right", "Up", "Down"
    };
    static const char *const pads[NATIVE_CONFIG_CONTROLS] = {
        "x", "b", "y", "a", "leftshoulder", "axis:lefttrigger", "leftstick",
        "rightshoulder", "axis:righttrigger", "rightstick", "start", "back",
        "dpleft", "dpright", "dpup", "dpdown", "axis:leftx", "axis:lefty", "axis:rightx", "axis:righty"
    };
    memset(config, 0, sizeof(*config));
#ifdef USE_16BY9
    config->width = 1280; config->height = 720;
#else
    config->width = 800; config->height = 600;
#endif
    config->maxFps = 30;
    config->interpolation = 0;
    config->axisDeadzone = 500;
    for (unsigned i = 0; i < NATIVE_CONFIG_KEYS; i++) strcpy(config->keyboard[i], keys[i]);
    for (unsigned i = 0; i < NATIVE_CONFIG_CONTROLS; i++) strcpy(config->controller[i], pads[i]);
}

struct ConfigParser
{
    const char *text;
    size_t bytes, at;
    char *error;
    size_t errorBytes;
    struct NativeConfig config;
};

static int ConfigFail(struct ConfigParser *p, const char *message)
{
    if (p->error != NULL && p->errorBytes != 0)
        snprintf(p->error, p->errorBytes, "%s (byte %zu)", message, p->at);
    return 0;
}

static void ConfigSpace(struct ConfigParser *p)
{
    while (p->at < p->bytes && (p->text[p->at] == ' ' || p->text[p->at] == '\t' ||
           p->text[p->at] == '\r' || p->text[p->at] == '\n')) p->at++;
}

static int ConfigTake(struct ConfigParser *p, char c)
{
    ConfigSpace(p);
    if (p->at == p->bytes || p->text[p->at] != c) return ConfigFail(p, "Unexpected JSON token");
    p->at++;
    return 1;
}

/* Config names use ASCII. JSON escapes, including ASCII \u escapes, are decoded. */
static int ConfigString(struct ConfigParser *p, char *out, size_t capacity)
{
    size_t count = 0;
    if (!ConfigTake(p, '"')) return 0;
    while (p->at < p->bytes)
    {
        unsigned char c = (unsigned char)p->text[p->at++];
        if (c == '"') { out[count] = 0; return 1; }
        if (c < 32 || c > 126) return ConfigFail(p, "Config names must use printable ASCII");
        if (c == '\\')
        {
            if (p->at == p->bytes) return ConfigFail(p, "Incomplete JSON escape");
            c = (unsigned char)p->text[p->at++];
            if (c == 'u')
            {
                unsigned code = 0;
                for (unsigned i = 0; i < 4; i++)
                {
                    if (p->at == p->bytes) return ConfigFail(p, "Incomplete Unicode escape");
                    unsigned char h = (unsigned char)p->text[p->at++];
                    unsigned digit;
                    if (h >= '0' && h <= '9') digit = h - '0';
                    else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') digit = h - 'A' + 10;
                    else return ConfigFail(p, "Invalid Unicode escape");
                    code = code * 16 + digit;
                }
                if (code < 32 || code > 126) return ConfigFail(p, "Config names must use printable ASCII");
                c = (unsigned char)code;
            }
            else if (c != '"' && c != '\\' && c != '/') return ConfigFail(p, "Unsupported escape in config name");
        }
        if (count + 1 >= capacity) return ConfigFail(p, "Config name is too long");
        out[count++] = (char)c;
    }
    return ConfigFail(p, "Unterminated JSON string");
}

static int ConfigInteger(struct ConfigParser *p, int minimum, int maximum, int *out)
{
    ConfigSpace(p);
    if (p->at == p->bytes || p->text[p->at] < '0' || p->text[p->at] > '9') return ConfigFail(p, "Expected a nonnegative integer");
    size_t start = p->at;
    unsigned value = 0;
    while (p->at < p->bytes && p->text[p->at] >= '0' && p->text[p->at] <= '9')
    {
        value = value * 10 + (unsigned)(p->text[p->at++] - '0');
        if (value > (unsigned)maximum) return ConfigFail(p, "Numeric setting exceeds its allowed range");
    }
    if (p->at - start > 1 && p->text[start] == '0') return ConfigFail(p, "Leading zeros are not valid JSON");
    if (value < (unsigned)minimum) return ConfigFail(p, "Numeric setting is below its allowed range");
    *out = (int)value;
    return 1;
}

static int ConfigBoolean(struct ConfigParser *p, int *out)
{
    ConfigSpace(p);
    if (p->bytes - p->at >= 4 && memcmp(p->text + p->at, "true", 4) == 0) { p->at += 4; *out = 1; return 1; }
    if (p->bytes - p->at >= 5 && memcmp(p->text + p->at, "false", 5) == 0) { p->at += 5; *out = 0; return 1; }
    return ConfigFail(p, "Expected true or false");
}

enum ConfigGroup { CONFIG_ROOT, CONFIG_VIDEO, CONFIG_INPUT, CONFIG_KEYBOARD, CONFIG_CONTROLLER };
static int ConfigObject(struct ConfigParser *p, enum ConfigGroup group);

static int ConfigProperty(struct ConfigParser *p, enum ConfigGroup group, const char *key, unsigned *id)
{
    struct NativeConfig *c = &p->config;
    if (group == CONFIG_ROOT)
    {
        if (strcmp(key, "video") == 0) { *id = 0; return ConfigObject(p, CONFIG_VIDEO); }
        if (strcmp(key, "input") == 0) { *id = 1; return ConfigObject(p, CONFIG_INPUT); }
    }
    else if (group == CONFIG_VIDEO)
    {
        if (strcmp(key, "width") == 0) { *id = 0; return ConfigInteger(p, 320, 7680, &c->width); }
        if (strcmp(key, "height") == 0) { *id = 1; return ConfigInteger(p, 240, 4320, &c->height); }
        if (strcmp(key, "max_fps") == 0) { *id = 2; return ConfigInteger(p, 1, 240, &c->maxFps); }
        if (strcmp(key, "fullscreen") == 0) { *id = 3; return ConfigBoolean(p, &c->fullscreen); }
        if (strcmp(key, "bilinear_filtering") == 0) { *id = 4; return ConfigBoolean(p, &c->bilinearFiltering); }
        if (strcmp(key, "interpolation") == 0) { *id = 5; return ConfigBoolean(p, &c->interpolation); }
    }
    else if (group == CONFIG_INPUT)
    {
        if (strcmp(key, "keyboard") == 0) { *id = 0; return ConfigObject(p, CONFIG_KEYBOARD); }
        if (strcmp(key, "controller") == 0) { *id = 1; return ConfigObject(p, CONFIG_CONTROLLER); }
        if (strcmp(key, "keyboard_slot") == 0) { *id = 2; return ConfigInteger(p, 0, 3, &c->keyboardSlot); }
        if (strcmp(key, "axis_deadzone") == 0) { *id = 3; return ConfigInteger(p, 0, 32766, &c->axisDeadzone); }
    }
    else
    {
        unsigned count = group == CONFIG_KEYBOARD ? NATIVE_CONFIG_KEYS : NATIVE_CONFIG_CONTROLS;
        for (unsigned i = 0; i < count; i++) if (strcmp(key, configControlNames[i]) == 0)
        {
            *id = i;
            return ConfigString(p, group == CONFIG_KEYBOARD ? c->keyboard[i] : c->controller[i], NATIVE_CONFIG_NAME_BYTES);
        }
    }
    if (p->error != NULL && p->errorBytes != 0) snprintf(p->error, p->errorBytes, "Unknown setting '%s' (byte %zu)", key, p->at);
    return 0;
}

static int ConfigObject(struct ConfigParser *p, enum ConfigGroup group)
{
    unsigned seen = 0;
    if (!ConfigTake(p, '{')) return 0;
    ConfigSpace(p);
    if (p->at < p->bytes && p->text[p->at] == '}') { p->at++; return 1; }
    for (;;)
    {
        char key[NATIVE_CONFIG_NAME_BYTES]; unsigned id = 0;
        if (!ConfigString(p, key, sizeof(key)) || !ConfigTake(p, ':') || !ConfigProperty(p, group, key, &id)) return 0;
        if (seen & (1u << id)) return ConfigFail(p, "Duplicate setting");
        seen |= 1u << id;
        ConfigSpace(p);
        if (p->at < p->bytes && p->text[p->at] == '}') { p->at++; return 1; }
        if (!ConfigTake(p, ',')) return 0;
    }
}

int NativeConfig_Parse(const char *json, size_t bytes, struct NativeConfig *config, char *error, size_t errorBytes)
{
    if (error != NULL && errorBytes != 0) error[0] = 0;
    if (json == NULL || config == NULL || bytes > NATIVE_CONFIG_MAX_BYTES)
    {
        if (error != NULL && errorBytes != 0) snprintf(error, errorBytes, "Invalid config buffer or file exceeds 16 KiB");
        return 0;
    }
    struct ConfigParser parser = {.text = json, .bytes = bytes, .error = error, .errorBytes = errorBytes};
    NativeConfig_Defaults(&parser.config);
    if (!ConfigObject(&parser, CONFIG_ROOT)) return 0;
    ConfigSpace(&parser);
    if (parser.at != bytes) return ConfigFail(&parser, "Unexpected data after JSON object");
    *config = parser.config;
    return 1;
}

int NativeConfig_Load(const char *path, struct NativeConfig *config, char *error, size_t errorBytes)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        if (errno == ENOENT) return 0;
        if (error != NULL && errorBytes != 0) snprintf(error, errorBytes, "Cannot read config: %s", strerror(errno));
        return -1;
    }
    char buffer[NATIVE_CONFIG_MAX_BYTES + 1];
    size_t bytes = fread(buffer, 1, sizeof(buffer), file);
    int failed = ferror(file);
    fclose(file);
    if (failed)
    {
        if (error != NULL && errorBytes != 0) snprintf(error, errorBytes, "Failed reading config file");
        return -1;
    }
    return NativeConfig_Parse(buffer, bytes, config, error, errorBytes) ? 1 : -1;
}
