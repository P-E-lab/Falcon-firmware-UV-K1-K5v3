#ifndef MEM_STATS_H
#define MEM_STATS_H

#include <stdint.h>
#include <sys/types.h>

/*
 * _sbrk — heap syscall for newlib malloc.
 *
 * Uses linker symbols to locate the heap base (_end) and the top of RAM
 * (_estack). Detects heap/stack collision, sets errno = ENOMEM and returns
 * (caddr_t)-1 when the allocation would intrude on the stack region.
 */
caddr_t _sbrk(int incr);

#endif /* MEM_STATS_H */
