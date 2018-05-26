#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <stdio.h>
#include <assert.h>
#include <sched_RR.h>
#include <sched_MLFQ.h>
#include <sched_mpRR.h>
#include <kio.h>
#ifdef ARCH_RISCV64
#include <smp.h>
#endif
#include <mp.h>
#include <trap.h>
#include <sysconf.h>
#include <spinlock.h>

#ifndef ARCH_RISCV64
/* we may use lock free list */
struct __timer_list_t{
	list_entry_t tl;
	spinlock_s lock;
};
static struct __timer_list_t __timer_list;
#define timer_list __timer_list.tl

static DEFINE_PERCPU_NOINIT(struct run_queue, runqueues);
#endif

static spinlock_s sched_lock;
static struct sched_class *sched_class;

//static struct run_queue *rq;

#ifdef ARCH_RISCV64
extern struct cpu cpus[];

static const int MAX_MOVE_PROC_NUM = 100;

static inline void move_run_queue(int src_cpu_id, int dst_cpu_id, struct proc_struct *proc) {
    struct run_queue *s_rq = &cpus[src_cpu_id].rqueue;
    struct run_queue *d_rq = &cpus[dst_cpu_id].rqueue;
    sched_class->dequeue(s_rq, proc);

    proc->cpu_affinity = dst_cpu_id; 
    sched_class->enqueue(d_rq, proc);
}

static inline int min(int a, int b) {
    if (a < b) return a;
    else return b;
}

static inline void load_balance()
{
    for (int i = 0; i < NCPU; ++i) {
        spinlock_acquire(&cpus[i].rqueue_lock);
    }
    int lcpu_count = NCPU;
    double load_sum = 0, load_max = -1, my_load = 0;
    int max_id = 0;
    
   
    for (int i = 0; i < lcpu_count; ++i) {
        double load = sched_class->get_load(&(cpus[i].rqueue));
        load_sum += load;
        if (load > load_max) {
            load_max = load;
            max_id = i;
        }
    }
    load_sum /= lcpu_count;
    
    {
        load_max = sched_class->get_load(&cpus[max_id].rqueue);
        my_load = sched_class->get_load(&cpus[myid()].rqueue);
        int needs = min((int)(load_max - load_sum), (int)(load_sum - my_load));
        needs = min(needs, MAX_MOVE_PROC_NUM);
        if (needs > 3 && myid() != max_id) {
            //kprintf("===========%d %d %d======\n", myid(), max_id, needs);
            struct proc_struct* procs_moved[MAX_MOVE_PROC_NUM];//TODO: max proc num in rq
            int num = sched_class->get_proc(&cpus[max_id].rqueue, procs_moved, needs);
            for (int i = 0; i < num; ++i)
                move_run_queue(max_id, myid(), procs_moved[i]);
        }
    }
    for (int i = 0; i < NCPU; ++i) {
        spinlock_release(&cpus[i].rqueue_lock);
    }
}
#endif

static inline void sched_class_enqueue(struct proc_struct *proc)
{
	if (proc != idleproc) {
#ifdef ARCH_RISCV64
		struct run_queue *rq = &mycpu()->rqueue;
		if(proc->flags & PF_PINCPU){
			assert(proc->cpu_affinity >= 0 
					&& proc->cpu_affinity < NCPU);
			rq = &cpus[proc->cpu_affinity].rqueue;
		}
		assert(proc->cpu_affinity == myid());

        	spinlock_acquire(&mycpu()->rqueue_lock);
		sched_class->enqueue(rq, proc);
        	spinlock_release(&mycpu()->rqueue_lock);
#else
		//TODO load balance
		struct run_queue *rq = get_cpu_ptr(runqueues);
		if(proc->flags & PF_PINCPU){
			assert(proc->cpu_affinity >= 0
					&& proc->cpu_affinity < sysconf.lcpu_count);
			rq = per_cpu_ptr(runqueues, proc->cpu_affinity);
		}
		//XXX lock
		sched_class->enqueue(rq, proc);
#endif
	}
	else panic("sched");
}

static inline void sched_class_dequeue(struct proc_struct *proc)
{
#ifdef ARCH_RISCV64
	struct run_queue *rq = &mycpu()->rqueue;
#else
	struct run_queue *rq = get_cpu_ptr(runqueues);
#endif
	sched_class->dequeue(rq, proc);
}

static inline struct proc_struct *sched_class_pick_next(void)
{
#ifdef ARCH_RISCV64
	struct run_queue *rq = &mycpu()->rqueue;
#else
	struct run_queue *rq = get_cpu_ptr(runqueues);
#endif
	return sched_class->pick_next(rq);
}

static void sched_class_proc_tick(struct proc_struct *proc)
{
	spinlock_acquire(&sched_lock);
	if (proc != idleproc) {
#ifdef ARCH_RISCV64
		struct run_queue *rq = &mycpu()->rqueue;
		spinlock_acquire(&mycpu()->rqueue_lock);
		sched_class->proc_tick(rq, proc);
		spinlock_release(&mycpu()->rqueue_lock);
#else
		struct run_queue *rq = get_cpu_ptr(runqueues);
		sched_class->proc_tick(rq, proc);
#endif
	} else {
		proc->need_resched = 1;
	}
	spinlock_release(&sched_lock);
}

//static struct run_queue __rq[NCPU];

void sched_init(void)
{
#ifdef ARCH_RISCV64
	int id = myid();
	struct run_queue *rq0 = &cpus[id].rqueue;
	list_init(&(rq0->rq_link));
	list_init(&(cpus[id].timer_list.tl));
	spinlock_init(&cpus[id].rqueue_lock);
	rq0->max_time_slice = 8;

	int i;
	for (i = 0; i < NCPU; i++) {
		if(i == id)
			continue;
		struct run_queue *rqi = &cpus[i].rqueue;
		list_init(&(rqi->rq_link));
        	list_init(&(cpus[i].timer_list.tl));
        	spinlock_init(&cpus[i].rqueue_lock);
		rqi->max_time_slice = rq0->max_time_slice;
	}
#else
	list_init(&timer_list);

	//rq = __rq;
	//list_init(&(__rq[0].rq_link));
	struct run_queue *rq0 = get_cpu_ptr(runqueues);
	list_init(&(rq0->rq_link));
	rq0->max_time_slice = 8;

	int i;
	for (i = 1; i < sysconf.lcpu_count; i++) {
		struct run_queue *rqi = per_cpu_ptr(runqueues, i);
		list_add_before(&(rq0->rq_link),
				&(rqi->rq_link));
		rqi->max_time_slice = rq0->max_time_slice;
	}
#endif

#ifdef UCONFIG_SCHEDULER_MLFQ
	sched_class = &MLFQ_sched_class;
#elif defined UCONFIG_SCHEDULER_RR
	sched_class = &RR_sched_class;
#else
	sched_class = &MPRR_sched_class;
#endif
#ifdef ARCH_RISCV64
	for (i = 0; i < NCPU; i++) {
		struct run_queue *rqi = &cpus[i].rqueue;
		sched_class->init(rqi);
	}
#else
	for (i = 0; i < sysconf.lcpu_count; i++) {
		struct run_queue *rqi = per_cpu_ptr(runqueues, i);
		sched_class->init(rqi);
	}
#endif

	kprintf("sched class: %s\n", sched_class->name);
}

void stop_proc(struct proc_struct *proc, uint32_t wait)
{
	bool intr_flag;
	local_intr_save(intr_flag);
	spinlock_acquire(&sched_lock);
	proc->state = PROC_SLEEPING;
	proc->wait_state = wait;
#ifdef ARCH_RISCV64
	spinlock_acquire(&mycpu()->rqueue_lock);
#endif
	if (!list_empty(&(proc->run_link))) {
		sched_class_dequeue(proc);
	}
#ifdef ARCH_RISCV64
	spinlock_release(&mycpu()->rqueue_lock);
#endif
	spinlock_acquire(&sched_lock);
	local_intr_restore(intr_flag);
}

void wakeup_proc(struct proc_struct *proc)
{
	assert(proc->state != PROC_ZOMBIE);
	bool intr_flag;
	local_intr_save(intr_flag);
	spinlock_acquire(&sched_lock);
	{
		if (proc->state != PROC_RUNNABLE) {
			proc->state = PROC_RUNNABLE;
			proc->wait_state = 0;
			if (proc != current) {
#ifdef ARCH_RISCV64
				assert(proc->pid >= NCPU);
#else
				assert(proc->pid >= sysconf.lcpu_count);
#endif
				proc->cpu_affinity = myid();
				sched_class_enqueue(proc);
			}
		} else {
			warn("wakeup runnable process.\n");
		}
	}
	spinlock_release(&sched_lock);
	local_intr_restore(intr_flag);
}

int try_to_wakeup(struct proc_struct *proc)
{
	assert(proc->state != PROC_ZOMBIE);
	int ret;
	bool intr_flag;
	local_intr_save(intr_flag);
	spinlock_acquire(&sched_lock);
	{
		if (proc->state != PROC_RUNNABLE) {
			proc->state = PROC_RUNNABLE;
			proc->wait_state = 0;
			if (proc != current) {
				proc->cpu_affinity = myid();
				sched_class_enqueue(proc);
			}
			ret = 1;
		} else {
			ret = 0;
		}
		struct proc_struct *next = proc;
		while ((next = next_thread(next)) != proc) {
			if (next->state == PROC_SLEEPING
			    && next->wait_state == WT_SIGNAL) {
				next->state = PROC_RUNNABLE;
				next->wait_state = 0;
				if (next != current) {
					next->cpu_affinity = myid();
					sched_class_enqueue(next);
				}
			}
		}
	}
	spinlock_release(&sched_lock);
	local_intr_restore(intr_flag);
	return ret;
}

#include <vmm.h>

void schedule(void)
{
	/* schedule in irq ctx is not allowed */
	assert(!ucore_in_interrupt());
	bool intr_flag;
	struct proc_struct *next;

	local_intr_save(intr_flag);
	spinlock_acquire(&sched_lock);
#ifdef ARCH_RISCV64
	int lcpu_count = NCPU;
#else
	int lcpu_count = sysconf.lcpu_count;
#endif
	{
		current->need_resched = 0;
#ifdef ARCH_RISCV64
		load_balance();

        spinlock_acquire(&mycpu()->rqueue_lock);
#else
		if (current->state == PROC_RUNNABLE
		    && current->pid >= lcpu_count) {
			sched_class_enqueue(current);
		}
#endif
		next = sched_class_pick_next();
		if (next != NULL)
			sched_class_dequeue(next);
		else
			next = idleproc;
#ifdef ARCH_RISCV64
		spinlock_release(&mycpu()->rqueue_lock);
#endif
		next->runs++;
		spinlock_release(&sched_lock);
		if (next != current)
			proc_run(next);
	}
	local_intr_restore(intr_flag);
}

#ifdef ARCH_RISCV64
static void __add_timer(timer_t * timer, int cpu_id)
{
	assert(timer->expires > 0 && timer->proc != NULL);
	assert(list_empty(&(timer->timer_link)));
	list_entry_t *le = list_next(&cpus[cpu_id].timer_list.tl);
	while (le != &cpus[cpu_id].timer_list.tl) {
		timer_t *next = le2timer(le, timer_link);
		if (timer->expires < next->expires) {
			next->expires -= timer->expires;
			break;
		}
		timer->expires -= next->expires;
		le = list_next(le);
	}
	list_add_before(le, &(timer->timer_link));
}

void add_timer(timer_t * timer)
{
	bool intr_flag;
	spin_lock_irqsave(&mycpu()->timer_list.lock, intr_flag);
	{
		__add_timer(timer, myid());
	}
	spin_unlock_irqrestore(&mycpu()->timer_list.lock, intr_flag);
}

static void __del_timer(timer_t * timer, int cpu_id)
{
	if (!list_empty(&(timer->timer_link))) {
		if (timer->expires != 0) {
			list_entry_t *le =
				list_next(&(timer->timer_link));
			if (le != &cpus[cpu_id].timer_list.tl) {
				timer_t *next =
					le2timer(le, timer_link);
				next->expires += timer->expires;
			}
		}
		list_del_init(&(timer->timer_link));
	}
}

void del_timer(timer_t * timer)
{

	bool intr_flag;
	spin_lock_irqsave(&mycpu()->timer_list.lock, intr_flag);
	{
		__del_timer(timer, myid());
	}
	spin_unlock_irqrestore(&mycpu()->timer_list.lock, intr_flag);
}

void run_timer_list(void)
{
	bool intr_flag;
	spin_lock_irqsave(&mycpu()->timer_list.lock, intr_flag);
	{
		list_entry_t *le = list_next(&mycpu()->timer_list.tl);
		if (le != &mycpu()->timer_list.tl) {
			timer_t *timer = le2timer(le, timer_link);
			assert(timer->expires != 0);
			timer->expires--;
			while (timer->expires == 0) {
				le = list_next(le);
				if (__ucore_is_linux_timer(timer)) {
					struct __ucore_linux_timer *lt =
					    &(timer->linux_timer);

					spin_unlock_irqrestore(&mycpu()->timer_list.lock, intr_flag);
					if (lt->function)
						(lt->function) (lt->data);
					spin_lock_irqsave(&mycpu()->timer_list.lock, intr_flag);

					__del_timer(timer, timer->proc->cpu_affinity);
					kfree(timer);
					continue;
				}
				struct proc_struct *proc = timer->proc;
				if (proc->wait_state != 0) {
					assert(proc->wait_state &
					       WT_INTERRUPTED);
				} else {
					warn("process %d's wait_state == 0.\n",
					     proc->pid);
				}

				wakeup_proc(proc);

				__del_timer(timer, timer->proc->cpu_affinity);
				if (le == &mycpu()->timer_list.tl) {
					break;
				}
				timer = le2timer(le, timer_link);
			}
		}
		sched_class_proc_tick(current);
	}
	spin_unlock_irqrestore(&mycpu()->timer_list.lock, intr_flag);
}

void post_switch(void)
{
    struct proc_struct* prev = mycpu()->prev;
    spinlock_acquire(&prev->lock);
    if (prev->state == PROC_RUNNABLE && prev->pid >= NCPU)
    {
        prev->cpu_affinity = myid();
        sched_class_enqueue(prev);
    }
    spinlock_release(&prev->lock);
}
#else
static void __add_timer(timer_t * timer)
{
	assert(timer->expires > 0 && timer->proc != NULL);
	assert(list_empty(&(timer->timer_link)));
	list_entry_t *le = list_next(&timer_list);
	while (le != &timer_list) {
		timer_t *next = le2timer(le, timer_link);
		if (timer->expires < next->expires) {
			next->expires -= timer->expires;
			break;
		}
		timer->expires -= next->expires;
		le = list_next(le);
	}
	list_add_before(le, &(timer->timer_link));
}

void add_timer(timer_t * timer)
{
	bool intr_flag;
	spin_lock_irqsave(&__timer_list.lock, intr_flag);
	{
		__add_timer(timer);
	}
	spin_unlock_irqrestore(&__timer_list.lock, intr_flag);
}

static void __del_timer(timer_t * timer)
{
	if (!list_empty(&(timer->timer_link))) {
		if (timer->expires != 0) {
			list_entry_t *le =
				list_next(&(timer->timer_link));
			if (le != &timer_list) {
				timer_t *next =
					le2timer(le, timer_link);
				next->expires += timer->expires;
			}
		}
		list_del_init(&(timer->timer_link));
	}
}

void del_timer(timer_t * timer)
{

	bool intr_flag;
	spin_lock_irqsave(&__timer_list.lock, intr_flag);
	{
		__del_timer(timer);
	}
	spin_unlock_irqrestore(&__timer_list.lock, intr_flag);
}

void run_timer_list(void)
{
	bool intr_flag;
	spin_lock_irqsave(&__timer_list.lock, intr_flag);
	{
		list_entry_t *le = list_next(&timer_list);
		if (le != &timer_list) {
			timer_t *timer = le2timer(le, timer_link);
			assert(timer->expires != 0);
			timer->expires--;
			while (timer->expires == 0) {
				le = list_next(le);
				if (__ucore_is_linux_timer(timer)) {
					struct __ucore_linux_timer *lt =
					    &(timer->linux_timer);

					spin_unlock_irqrestore(&__timer_list.lock, intr_flag);
					if (lt->function)
						(lt->function) (lt->data);
					spin_lock_irqsave(&__timer_list.lock, intr_flag);

					__del_timer(timer);
					kfree(timer);
					continue;
				}
				struct proc_struct *proc = timer->proc;
				if (proc->wait_state != 0) {
          //TODO: This seems to prevent kernel-thread mutex with timeout from
          //working, but I don't know what I' doing.
					//assert(proc->wait_state & WT_INTERRUPTED);
				} else {
					warn("process %d's wait_state == 0.\n",
					     proc->pid);
				}

				wakeup_proc(proc);

				__del_timer(timer);
				if (le == &timer_list) {
					break;
				}
				timer = le2timer(le, timer_link);
			}
		}
		sched_class_proc_tick(current);
	}
	spin_unlock_irqrestore(&__timer_list.lock, intr_flag);
}
#endif
