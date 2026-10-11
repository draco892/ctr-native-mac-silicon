#include <platform/native_credits.h>
#include <string.h>

int NativeCredits_Decode(const void *file, size_t fileSize, char **strings,
                         size_t capacity, u32 *count)
{
    if (!count)
        return 0;
    *count = 0;
    if (!file || fileSize < 8 || CTR_ReadU32LE(file) != fileSize)
        return 0;
    const u8 *bytes = file;
    const s16 n = (s16)CTR_ReadU16LE(bytes + 4);
    if (n < 0 || n > 4096 || (size_t)n > capacity ||
        (size_t)n > (fileSize - 8) / 4 || (n && !strings))
        return 0;
    const size_t tableEnd = 8 + (size_t)n * 4;
    // Validate every string before publishing any pointer.
    for (int i = 0; i < n; i++)
    {
        const u32 offset = CTR_ReadU32LE(bytes + 8 + (size_t)i * 4);
        if (offset < tableEnd || offset >= fileSize ||
            !memchr(bytes + offset, 0, fileSize - offset))
            return 0;
    }
    for (int i = 0; i < n; i++)
        strings[i] = (char *)file + CTR_ReadU32LE(bytes + 8 + (size_t)i * 4);
    *count = (u32)n;
    return 1;
}
