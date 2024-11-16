#ifndef __STRING_H__
#define __STRING_H__

#include "stdint.h"

void *memset(void *dest, int c, uint64_t n);
void *memcpy(void *dst, const void *src, uint64_t);

#endif
