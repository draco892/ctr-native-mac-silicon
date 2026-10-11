/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/include/psx/strings.h
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#ifndef STRINGS_H
#define STRINGS_H

#if defined(CTR_NATIVE)
#include <ctype.h>
#include <string.h>
#include <stdio.h>

// Bound native UI formatting while retaining the retail PSYQ call contract.
#define CTR_SNPRINTF(dst, size, ...) snprintf((dst), (size), __VA_ARGS__)
#else
void *memcpy(void *dst, const void *src, u32 count);
void *memmove(void *dst, const void *src, u32 count);
void *memset(void *dst, int value, u32 count);
char *strcat(char *dst, const char *src);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, u32 count);
int strcmp(const char *lhs, const char *rhs);
int strncmp(const char *lhs, const char *rhs, u32 count);
u32 strlen(const char *text);
int sprintf(char *dst, const char *format, ...);
#define CTR_SNPRINTF(dst, size, ...) sprintf((dst), __VA_ARGS__)
int tolower(int value);
#endif

#endif
