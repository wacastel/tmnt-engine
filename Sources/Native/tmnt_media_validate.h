#ifndef TMNT_MEDIA_VALIDATE_H
#define TMNT_MEDIA_VALIDATE_H
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Accepts a UTF-8 directory with or without a trailing slash. */
bool tmnt_validate_media(const char *directory, char *error, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
