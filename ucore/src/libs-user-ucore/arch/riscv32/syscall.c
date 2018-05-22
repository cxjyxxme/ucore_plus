#include <unistd.h>
#include <types.h>
#include <stdarg.h>
#include <syscall.h>
#include <mboxbuf.h>
#include <stat.h>
#include <dirent.h>

#define MAX_ARGS            5

uint32_t syscall(int num, ...)
{
	va_list ap;
	va_start(ap, num);
	uint32_t a[MAX_ARGS];
	int i;
	for (i = 0; i < MAX_ARGS; i++) {
		a[i] = va_arg(ap, uint32_t);
	}
	va_end(ap);

	uint32_t ret;
    asm volatile (
        "lw a0, %1\n"
        "lw a1, %2\n"
        "lw a2, %3\n"
        "lw a3, %4\n"
        "lw a4, %5\n"
        "lw a5, %6\n"
        "ecall\n"
        "sw a0, %0"
        : "=m" (ret)
        : "m" (num),
          "m" (a[0]),
          "m" (a[1]),
          "m" (a[2]),
          "m" (a[3]),
          "m" (a[4])
        : "memory"
      );
	return ret;
}
