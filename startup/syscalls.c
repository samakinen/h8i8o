/* SPDX-License-Identifier: GPL-3.0-or-later */
/* newlib-nano system calls for the H8i8o: the heap break, and stubs.
 *
 * _sbrk grows the heap up to the stack reserve (link.ld) and records the
 * high-water mark, which the firmware reports over RTT. Nothing else does
 * I/O through newlib.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

extern char _heap_start, _heap_limit;

static char *g_brk = &_heap_start;
size_t g_heap_high_water = 0;

void *_sbrk(ptrdiff_t increment)
{
    char *previous = g_brk;
    if (g_brk + increment > &_heap_limit) {
        errno = ENOMEM;
        return (void *)-1;
    }
    g_brk += increment;
    if ((size_t)(g_brk - &_heap_start) > g_heap_high_water) {
        g_heap_high_water = (size_t)(g_brk - &_heap_start);
    }
    return previous;
}

void *__dso_handle = 0;

#define KEEP_SYM __attribute__((used))
KEEP_SYM int _close(int fd) { (void)fd; return -1; }
KEEP_SYM int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
KEEP_SYM int _isatty(int fd) { (void)fd; return 1; }
KEEP_SYM off_t _lseek(int fd, off_t offset, int whence) { (void)fd; (void)offset; (void)whence; return 0; }
KEEP_SYM int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return 0; }
KEEP_SYM int _write(int fd, const char *buf, int len) { (void)fd; (void)buf; return len; }
KEEP_SYM void _exit(int status) { (void)status; for (;;) {} }
KEEP_SYM int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
KEEP_SYM int _getpid(void) { return 1; }
