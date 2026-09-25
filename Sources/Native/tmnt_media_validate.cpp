/* Local exact-media boundary for the supplied TMNT set. */
#include "tmnt_media_validate.h"
#include "tmnt_media_identity.h"
#include <CommonCrypto/CommonDigest.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static bool media_error(char *error, size_t capacity, const char *message,
                        const char *filename = "")
{
    if (error && capacity) std::snprintf(error, capacity, "%s%s", message, filename);
    return false;
}

extern "C" bool tmnt_validate_media(const char *directory, char *error, size_t capacity)
{
    if (error && capacity) error[0] = '\0';
    if (!directory || !directory[0])
        return media_error(error, capacity, "No TMNT ROM directory was supplied.");
    std::string base(directory);
    if (base.back() != '/') base += '/';
    for (size_t i = 0; i < TMNT_MEDIA_FILE_COUNT; ++i)
    {
        const TMNTMediaIdentity &identity = TMNT_MEDIA_FILES[i];
        const std::string path = base + identity.name;
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file) return media_error(error, capacity, "Missing or unreadable TMNT ROM: ", identity.name);
        // One extra byte distinguishes an exact match from a matching prefix.
        std::vector<unsigned char> data(static_cast<size_t>(identity.size) + 1);
        const size_t count = std::fread(data.data(), 1, data.size(), file);
        const bool failed = std::ferror(file) != 0;
        std::fclose(file);
        if (failed) return media_error(error, capacity, "Could not read TMNT ROM: ", identity.name);
        if (count != identity.size)
            return media_error(error, capacity, "Incorrect TMNT ROM size: ", identity.name);
        unsigned char digest[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256(data.data(), static_cast<CC_LONG>(count), digest);
        char hex[CC_SHA256_DIGEST_LENGTH * 2 + 1];
        for (size_t j = 0; j < CC_SHA256_DIGEST_LENGTH; ++j)
            std::snprintf(hex + j * 2, 3, "%02x", static_cast<unsigned>(digest[j]));
        if (std::strcmp(hex, identity.sha256))
            return media_error(error, capacity, "Incorrect TMNT ROM SHA-256: ", identity.name);
    }
    return true;
}
