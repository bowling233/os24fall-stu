#include "string.h"
#include "stdint.h"
#include "printk.h"

void *memset(void *dest, int c, uint64_t n) {
#ifdef DEBUG
    // Log("memset: dest %p c %d n %lx", dest, c, n);
#endif
    char *s = (char *)dest;
    for (uint64_t i = 0; i < n; ++i) {
        s[i] = c;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, uint64_t n) {
#ifdef DEBUG
    // Log("memcpy: dest %p src %p n %lx", dest, src, n);
#endif
    char *d = (char *)dest;
    const char *s = (const char *)src;
    for (uint64_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}
