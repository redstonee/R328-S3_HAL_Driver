#include "r328_core.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

extern unsigned char __heap_start__, __heap_end__;
static uintptr_t heap_break;

/* newlib owns malloc/free; this hook only manages its backing RAM interval.
 * Use the allocator from CPU0 foreground, never from interrupt callbacks. */
void *_sbrk(ptrdiff_t increment)
{
    const uintptr_t begin = (uintptr_t)&__heap_start__;
    const uintptr_t end = (uintptr_t)&__heap_end__;
    uint32_t irq_state = R328_IRQ_Save();
    if (heap_break == 0U)
    {
        heap_break = begin;
    }
    uintptr_t previous = heap_break;
    if (increment >= 0)
    {
        uintptr_t growth = (uintptr_t)increment;
        if (growth > end - heap_break)
        {
            errno = ENOMEM;
            R328_IRQ_Restore(irq_state);
            return (void *)-1;
        }
        heap_break += growth;
    }
    else
    {
        /* Avoid negating PTRDIFF_MIN, which would overflow ptrdiff_t. */
        uintptr_t shrink = (uintptr_t)(-(increment + 1)) + 1U;
        if (shrink > heap_break - begin)
        {
            errno = ENOMEM;
            R328_IRQ_Restore(irq_state);
            return (void *)-1;
        }
        heap_break -= shrink;
    }
    R328_IRQ_Restore(irq_state);
    return (void *)previous;
}
