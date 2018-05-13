#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include <sync.h>
#include <atomic.h>
//#include <proc.h>

typedef struct {
	volatile unsigned int lock;
} spinlock_s;

typedef spinlock_s *spinlock_t;

#define spinlock_init(x) do { (x)->lock = 0; } while (0)

static inline int spinlock_acquire_try(spinlock_t lock)
{
    return test_and_set_bit(0, lock);
}

static inline void spinlock_acquire(spinlock_t lock)
{
    if (spinlock_acquire_try(lock)) {
        int step = 0;
        do {
        	//do_yield();
        	if (++step == 100){
        		step = 0;
        		//do_sleep(10);
        	}
        } while (spinlock_acquire_try(lock));
    }
}

static inline void spinlock_release(spinlock_t lock)
{
    test_and_clear_bit(0, lock);
}

#define spin_lock_irqsave(lock, x)      do { x = __intr_save();spinlock_acquire(lock); } while (0)

#define spin_unlock_irqrestore(lock, x)      do { spinlock_release(lock);__intr_restore(x); } while (0)


#endif
