/* Minimal newlib hooks. There is no heap: any malloc reaches _sbrk and traps (NFR-MEM-1). */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

void *_sbrk(ptrdiff_t incr)
{
    (void)incr;
    __asm volatile("bkpt #0");   /* debugger stop; in Renode logged as a breakpoint */
    for (;;) {}
}
