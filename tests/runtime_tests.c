#include <assert.h>
#include <string.h>

int main(void)
{
    unsigned char bytes[8] = {0U};
    assert(memset(bytes + 1, 0x1A5, 6U) == bytes + 1);
    assert(bytes[0] == 0U && bytes[1] == 0xA5U && bytes[6] == 0xA5U && bytes[7] == 0U);
    const unsigned char source[] = {0x80U, 0xFFU, 0U, 3U};
    assert(memcpy(bytes + 1, source, sizeof(source)) == bytes + 1);
    assert(memcmp(bytes + 1, source, sizeof(source)) == 0);
    assert(memcmp(source, bytes, 1U) > 0 && memcmp(bytes, source, 1U) < 0);
    assert(memmove(bytes + 2, bytes + 1, 4U) == bytes + 2);
    assert(memcmp(bytes + 2, source, 4U) == 0);
    assert(memmove(bytes, bytes + 2, 4U) == bytes);
    assert(memcmp(bytes, source, 4U) == 0);
    assert(memmove(bytes, bytes, sizeof(bytes)) == bytes);
    assert(memset(bytes, 0, 0U) == bytes && bytes[0] == 0x80U);
    assert(memcpy(bytes, source, 0U) == bytes);
    assert(memcmp(bytes, source, 0U) == 0);
    return 0;
}
