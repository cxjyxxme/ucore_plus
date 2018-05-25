#include <types.h>
#include <mmu.h>
#include <memlayout.h>
#include <clock.h>
#include <trap.h>
#include <stdio.h>
#include <kdebug.h>
#include <assert.h>
#include <sync.h>
#include <monitor.h>
#include <console.h>
#include <vmm.h>
#include <proc.h>
#include <sched.h>
#include <unistd.h>
#include <syscall.h>
#include <error.h>
#include <kio.h>
#include <mp.h>
#include <sbi.h>
#include <arch.h>

#define TICK_NUM 30

void idt_init(void)
{
    extern void __alltraps(void);
    /* Set sscratch register to 0, indicating to exception vector that we are
     * presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
    /* Allow kernel to access user memory */
    set_csr(sstatus, SSTATUS_SUM);
    /* Allow keyboard interrupt */
    set_csr(sie, MIP_SSIP);
}

bool trap_in_kernel(struct trapframe * tf)
{
	return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf)
{
    kprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    kprintf("  status   0x%08x\n", tf->status);
    kprintf("  epc      0x%08x\n", tf->epc);
    kprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    kprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr)
{
    kprintf("  zero     0x%08x\n", gpr->zero);
    kprintf("  ra       0x%08x\n", gpr->ra);
    kprintf("  sp       0x%08x\n", gpr->sp);
    kprintf("  gp       0x%08x\n", gpr->gp);
    kprintf("  tp       0x%08x\n", gpr->tp);
    kprintf("  t0       0x%08x\n", gpr->t0);
    kprintf("  t1       0x%08x\n", gpr->t1);
    kprintf("  t2       0x%08x\n", gpr->t2);
    kprintf("  s0       0x%08x\n", gpr->s0);
    kprintf("  s1       0x%08x\n", gpr->s1);
    kprintf("  a0       0x%08x\n", gpr->a0);
    kprintf("  a1       0x%08x\n", gpr->a1);
    kprintf("  a2       0x%08x\n", gpr->a2);
    kprintf("  a3       0x%08x\n", gpr->a3);
    kprintf("  a4       0x%08x\n", gpr->a4);
    kprintf("  a5       0x%08x\n", gpr->a5);
    kprintf("  a6       0x%08x\n", gpr->a6);
    kprintf("  a7       0x%08x\n", gpr->a7);
    kprintf("  s2       0x%08x\n", gpr->s2);
    kprintf("  s3       0x%08x\n", gpr->s3);
    kprintf("  s4       0x%08x\n", gpr->s4);
    kprintf("  s5       0x%08x\n", gpr->s5);
    kprintf("  s6       0x%08x\n", gpr->s6);
    kprintf("  s7       0x%08x\n", gpr->s7);
    kprintf("  s8       0x%08x\n", gpr->s8);
    kprintf("  s9       0x%08x\n", gpr->s9);
    kprintf("  s10      0x%08x\n", gpr->s10);
    kprintf("  s11      0x%08x\n", gpr->s11);
    kprintf("  t3       0x%08x\n", gpr->t3);
    kprintf("  t4       0x%08x\n", gpr->t4);
    kprintf("  t5       0x%08x\n", gpr->t5);
    kprintf("  t6       0x%08x\n", gpr->t6);
}

static inline void print_pgfault(struct trapframe *tf)
{
    // The page fault test is in kernel anyway, so print a 'K/' here
    kprintf("page falut at 0x%08x: K/", tf->badvaddr);
    if (tf->cause == CAUSE_LOAD_PAGE_FAULT) {
        kprintf("R\n");
    } else if (tf->cause == CAUSE_STORE_PAGE_FAULT) {
        kprintf("W\n");
    } else {
        kprintf("0x%08x\n", tf->cause);
    }
}

static int pgfault_handler(struct trapframe *tf)
{
	extern struct mm_struct *check_mm_struct;
	struct mm_struct *mm;
	if (check_mm_struct != NULL) {
		assert(current == idleproc);
		mm = check_mm_struct;
	} else {
		if (current == NULL) {
			print_trapframe(tf);
			print_pgfault(tf);
			panic("unhandled page fault.\n");
		}
		mm = current->mm;
	}
	return do_pgfault(mm, 3, tf->badvaddr);
}

static void interrupt_handler(struct trapframe *tf) {
    intptr_t cause = (tf->cause << 1) >> 1;
    extern void dev_stdin_write(char c);
    switch (cause) {
        case IRQ_U_SOFT:
            kprintf("User software interrupt\n");
            break;
        case IRQ_S_SOFT:
            kprintf("Supervisor software interrupt\n");
            serial_intr();
            dev_stdin_write(cons_getc());
            break;
        case IRQ_H_SOFT:
            kprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_SOFT:
            kprintf("Machine software interrupt\n");
            break;
        case IRQ_U_TIMER:
            kprintf("User timer interrupt\n");
            break;
        case IRQ_S_TIMER:
            // "All bits besides SSIP and USIP in the sip register are
            // read-only." -- privileged spec1.9.1, 4.1.4, p59
            // In fact, Call sbi_set_timer will clear STIP, or you can clear it
            // directly.
            // clear_csr(sip, SIP_STIP);
            clock_set_next_event();
            ++ticks;
            run_timer_list();

            serial_intr();
            dev_stdin_write(cons_getc());
            break;
        case IRQ_H_TIMER:
            kprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_TIMER:
            kprintf("Machine software interrupt\n");
            break;
        case IRQ_U_EXT:
            kprintf("User external interrupt\n");
            break;
        case IRQ_S_EXT:
            kprintf("Supervisor external interrupt\n");
            break;
        case IRQ_H_EXT:
            kprintf("Hypervisor external interrupt\n");
            break;
        case IRQ_M_EXT:
            kprintf("Machine external interrupt\n");
            break;
        default:
            print_trapframe(tf);
            break;
    }
}

static void exception_handler(struct trapframe *tf) {
    int ret;
    switch (tf->cause) {
        case CAUSE_MISALIGNED_FETCH:
            kprintf("Instruction address misaligned\n");
            break;
        case CAUSE_FETCH_ACCESS:
            kprintf("Instruction access fault\n");
            break;
        case CAUSE_ILLEGAL_INSTRUCTION:
            kprintf("Illegal instruction\n");
            break;
        case CAUSE_BREAKPOINT:
            kprintf("Breakpoint\n");
            break;
        case CAUSE_MISALIGNED_LOAD:
            kprintf("Load address misaligned\n");
            break;
        case CAUSE_LOAD_ACCESS:
            kprintf("Load access fault\n");
            break;
        case CAUSE_MISALIGNED_STORE:
            kprintf("AMO address misaligned\n");
            break;
        case CAUSE_STORE_ACCESS:
            kprintf("Store/AMO access fault\n");
            break;
        case CAUSE_USER_ECALL:
            // kprintf("Environment call from U-mode\n");
            tf->epc += 4;
            syscall();
            break;
        case CAUSE_SUPERVISOR_ECALL:
            kprintf("Environment call from S-mode\n");
            tf->epc += 4;
            syscall();
            break;
        case CAUSE_HYPERVISOR_ECALL:
            kprintf("Environment call from H-mode\n");
            break;
        case CAUSE_MACHINE_ECALL:
            kprintf("Environment call from M-mode\n");
            break;
        case CAUSE_FETCH_PAGE_FAULT:
            kprintf("Instruction page fault\n");
            break;
        case CAUSE_LOAD_PAGE_FAULT:
            if ((ret = pgfault_handler(tf)) != 0) {
                kprintf("Load page fault\n");
                print_trapframe(tf);
                if (current == NULL) {
                    panic("handle pgfault failed. ret=%d\n", ret);
                } else {
                    if (trap_in_kernel(tf)) {
                        panic("handle pgfault failed in kernel mode. ret=%d\n",
                              ret);
                    }
                    kprintf("killed by kernel.\n");
                    panic("handle user mode pgfault failed. ret=%d\n", ret);
                    do_exit(-E_KILLED);
                }
            }
            break;
        case CAUSE_STORE_PAGE_FAULT:
            if ((ret = pgfault_handler(tf)) != 0) {
                kprintf("Store/AMO page fault\n");
                print_trapframe(tf);
                if (current == NULL) {
                    panic("handle pgfault failed. ret=%d\n", ret);
                } else {
                    if (trap_in_kernel(tf)) {
                        panic("handle pgfault failed in kernel mode. ret=%d\n",
                              ret);
                    }
                    kprintf("killed by kernel.\n");
                    panic("handle user mode pgfault failed. ret=%d\n", ret);
                    do_exit(-E_KILLED);
                }
            }
            break;
        default:
            print_trapframe(tf);
            break;
    }
}

static void trap_dispatch(struct trapframe *tf)
{
    if ((intptr_t)tf->cause < 0) {
        // interrupts
        interrupt_handler(tf);
    } else {
        // exceptions
        exception_handler(tf);
    }
}

void trap(struct trapframe *tf)
{
	// used for previous projects
	if (current == NULL) {
		trap_dispatch(tf);
	} else {
		// keep a trapframe chain in stack
		struct trapframe *otf = current->tf;
		current->tf = tf;

		bool in_kernel = trap_in_kernel(tf);

		trap_dispatch(tf);

		current->tf = otf;
		if (!in_kernel) {
			may_killed();
			if (current->need_resched) {
				schedule();
			}
		}
	}
}

int ucore_in_interrupt()
{
	return 0;
}
