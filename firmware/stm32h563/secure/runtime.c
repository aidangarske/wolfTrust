#include <stddef.h>

void* memset(void* dst, int value, size_t size)
{
    unsigned char* out = (unsigned char*)dst;
    size_t i;

    for (i = 0; i < size; ++i) {
        out[i] = (unsigned char)value;
    }

    return dst;
}

void* memcpy(void* dst, const void* src, size_t size)
{
    unsigned char* out = (unsigned char*)dst;
    const unsigned char* in = (const unsigned char*)src;
    size_t i;

    for (i = 0; i < size; ++i) {
        out[i] = in[i];
    }

    return dst;
}
