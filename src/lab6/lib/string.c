#include "string.h"
#include "stdint.h"
#include "printk.h"

void *memset(void *dest, int c, uint64_t n) {
    char *s = (char *)dest;
    for (uint64_t i = 0; i < n; ++i) {
        s[i] = c;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, uint64_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    for (uint64_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, uint64_t n) {
    const char *p1 = (const char *)s1;
    const char *p2 = (const char *)s2;
    for (uint64_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

int strlen(const char *s) {
    int len = 0;
    while (s[len]) {
        ++len;
    }
    return len;
}
