#ifndef __STRING_H__
#define __STRING_H__

#include "stdint.h"

void *memset(void *dest, int c, uint64_t n);
void *memcpy(void *dst, const void *src, uint64_t);
int memcmp(const void *s1, const void *s2, uint64_t n);
int strlen(const char *s);
char *strcpy(char *dest, const char *src);
int strcmp(const char *s1, const char *s2);

#endif
