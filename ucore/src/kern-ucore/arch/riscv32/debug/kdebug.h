#ifndef __KERN_DEBUG_KDEBUG_H__
#define __KERN_DEBUG_KDEBUG_H__

#include <types.h>
#include <trap.h>

void print_kerninfo(void);
void print_stackframe(void);
void print_debuginfo(uintptr_t eip);

//void debug_init(void);
//void debug_monitor(struct trapframe *tf);
//void debug_list_dr(void);
//int debug_enable_dr(unsigned regnum, uintptr_t addr, unsigned type,
//		    unsigned len);
//int debug_disable_dr(unsigned regnum);

#endif /* !__KERN_DEBUG_KDEBUG_H__ */
