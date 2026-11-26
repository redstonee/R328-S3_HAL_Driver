#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

/* Stand in for linker-defined bounds without changing the production hook. */
unsigned char test_heap[64] __attribute__((aligned(8)));
__asm__(".global __heap_start__\n"
        ".set __heap_start__, test_heap\n"
        ".global __heap_end__\n"
        ".set __heap_end__, test_heap + 64\n");

void *_sbrk(ptrdiff_t increment);

int main(void)
{
    assert(_sbrk(0) == test_heap);
    assert(_sbrk(1) == test_heap);
    assert(_sbrk(7) == test_heap + 1);
    assert(_sbrk(56) == test_heap + 8);
    errno = 0;
    assert(_sbrk(1) == (void *)-1 && errno == ENOMEM);
    assert(_sbrk(0) == test_heap + sizeof(test_heap));
    assert(_sbrk(-63) == test_heap + sizeof(test_heap));
    assert(_sbrk(-1) == test_heap + 1);
    errno = 0;
    assert(_sbrk(-1) == (void *)-1 && errno == ENOMEM);
    assert(_sbrk(0) == test_heap);
    errno = 0;
    assert(_sbrk(PTRDIFF_MIN) == (void *)-1 && errno == ENOMEM);
    errno = 0;
    assert(_sbrk(PTRDIFF_MAX) == (void *)-1 && errno == ENOMEM);
    assert(_sbrk(0) == test_heap);
    /* Failures must leave the whole interval available for a later request. */
    assert(_sbrk(sizeof(test_heap)) == test_heap);
    assert(_sbrk(-(ptrdiff_t)sizeof(test_heap)) == test_heap + sizeof(test_heap));
    return 0;
}
