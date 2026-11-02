/*
 * mem_stats.c — guarded newlib allocation hook.
 *
 * This module provides:
 *   - A safe _sbrk() that checks for heap/stack collision and sets ENOMEM.
 * The unused runtime statistics screen has been removed; the allocator
 * hook is kept for newlib compatibility.
 *
 * Linker symbols used:
 *   _end    — first byte after .bss; the heap begins here.
 *   _estack — top of RAM (= ORIGIN(RAM) + LENGTH(RAM)); initial stack pointer.
 */

#include "app/mem_stats.h"

#include <errno.h>
#include <stdint.h>

/* Linker-provided symbols.  Their *addresses* are the values we need. */
extern char _end;    /* heap base: first byte after .bss            */
extern char _estack; /* top of RAM / initial stack pointer           */

/* Safety margin kept between heap break and stack pointer (bytes). */
#define HEAP_STACK_GUARD 64u

/* ---- internal state ---------------------------------------------------- */

static char *heap_ptr = NULL; /* current heap break                       */

/* ---- _sbrk ------------------------------------------------------------- */

caddr_t _sbrk(int incr)
{
    if (heap_ptr == NULL) {
        heap_ptr = &_end;
    }

    char *new_ptr = heap_ptr + incr;

    /* Read the current stack pointer into a local variable. */
    register char *sp asm("sp");

    /*
     * Reject the allocation if the new heap break would intrude on the stack
     * region (including the guard).  Use min(sp, _estack) as the limit —
     * normally sp < _estack (stack grows downward from _estack), but cap at
     * _estack in case sp is somehow invalid.
     */
    char *stack_limit = (sp > &_estack) ? &_estack : sp;

    if (new_ptr + (int)HEAP_STACK_GUARD >= stack_limit) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }

    char *prev = heap_ptr;
    heap_ptr = new_ptr;

    return (caddr_t)prev;
}
