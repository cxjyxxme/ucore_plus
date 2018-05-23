#include <kio.h>
#include <console.h>
#include <sync.h>
#include <trap.h>
#include <stdio.h>
#include <spinlock.h>
#include <stdarg.h>
#include <unistd.h>
#include <mod.h>

/* *
 * cputch - writes a single character @c to stdout, and it will
 * increace the value of counter pointed by @cnt.
 * */
static void cputch(int c, int *cnt, int fd)
{
	cons_putc(c);
	(*cnt)++;
}

static spinlock_s kprintf_lock = { 0 };

/* *
 * vkprintf - format a string and writes it to stdout
 *
 * The return value is the number of characters which would be
 * written to stdout.
 *
 * Call this function if you are already dealing with a va_list.
 * Or you probably want kprintf() instead.
 * */
int vkprintf(const char *fmt, va_list ap)
{
	int cnt = 0;
	spinlock_acquire(&kprintf_lock);
	vprintfmt((void *)cputch, NO_FD, &cnt, fmt, ap);
	spinlock_release(&kprintf_lock);
	return cnt;
}

/* *
 * kprintf - formats a string and writes it to stdout
 *
 * The return value is the number of characters which would be
 * written to stdout.
 * */
int kprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int cnt = vkprintf(fmt, ap);
	va_end(ap);
	return cnt;
}

EXPORT_SYMBOL(kprintf);

/* *
 * vcprintf - format a string and writes it to stdout
 *
 * The return value is the number of characters which would be
 * written to stdout.
 *
 * Call this function if you are already dealing with a va_list.
 * Or you probably want cprintf() instead.
 * */
int
vcprintf(const char *fmt, va_list ap) {
    int cnt = 0;
    vprintfmt((void*)cputch, NO_FD, &cnt, fmt, ap);
    return cnt;
}

/* *
 * cprintf - formats a string and writes it to stdout
 *
 * The return value is the number of characters which would be
 * written to stdout.
 * */
int
cprintf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int cnt = vkprintf(fmt, ap);
	va_end(ap);
	return cnt;
}