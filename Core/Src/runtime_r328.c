/* Freestanding GCC may emit these calls for aggregate initialization/copies,
 * even with -fno-builtin. No heap or hosted C runtime is required. */
#include <stddef.h>
#include <stdint.h>

void *memset(void *destination, int value, size_t size)
{
    unsigned char *out = destination;
    for (size_t i = 0U; i < size; ++i)
    {
        out[i] = (unsigned char)value;
    }
    return destination;
}
void *memcpy(void *destination, const void *source, size_t size)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0U; i < size; ++i)
    {
        out[i] = in[i];
    }
    return destination;
}
void *memmove(void *destination, const void *source, size_t size)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    if ((uintptr_t)out <= (uintptr_t)in)
    {
        return memcpy(destination, source, size);
    }
    while (size != 0U)
    {
        --size;
        out[size] = in[size];
    }
    return destination;
}
int memcmp(const void *left, const void *right, size_t size)
{
    const unsigned char *a = left, *b = right;
    for (size_t i = 0U; i < size; ++i)
    {
        if (a[i] != b[i])
        {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}
