#ifndef __ARCH_UM_INCLUDE_ATOMIC_H
#define __ARCH_UM_INCLUDE_ATOMIC_H

#include <types.h>

/* Atomic operations that C can't guarantee us. Useful for resource counting etc.. */

#if (__riscv_xlen == 64)
#define __AMO(op) "amo" #op ".d"
#elif (__riscv_xlen == 32)
#define __AMO(op) "amo" #op ".w"
#else
#error "Unexpected BITS_PER_LONG"
#endif

typedef struct {
	volatile int counter;
} atomic_t;

static inline int atomic_read(const atomic_t * v)
    __attribute__ ((always_inline));
static inline void atomic_set(atomic_t * v, int i)
    __attribute__ ((always_inline));
static inline void atomic_add(atomic_t * v, int i)
    __attribute__ ((always_inline));
static inline void atomic_sub(atomic_t * v, int i)
    __attribute__ ((always_inline));
static inline bool atomic_sub_test_zero(atomic_t * v, int i)
    __attribute__ ((always_inline));
static inline void atomic_inc(atomic_t * v) __attribute__ ((always_inline));
static inline void atomic_dec(atomic_t * v) __attribute__ ((always_inline));
static inline bool atomic_inc_test_zero(atomic_t * v)
    __attribute__ ((always_inline));
static inline bool atomic_dec_test_zero(atomic_t * v)
    __attribute__ ((always_inline));
static inline int atomic_add_return(atomic_t * v, int i)
    __attribute__ ((always_inline));
static inline int atomic_sub_return(atomic_t * v, int i)
    __attribute__ ((always_inline));

/* *
 * atomic_read - read atomic variable
 * @v:  pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 * */
static inline int atomic_read(const atomic_t * v)
{
	return v->counter;
}

/* *
 * atomic_set - set atomic variable
 * @v:  pointer of type atomic_t
 * @i:  required value
 *
 * Atomically sets the value of @v to @i.
 * */
static inline void atomic_set(atomic_t * v, int i)
{
	v->counter = i;
}

/* *
 * atomic_add - add integer to atomic variable
 * @v:  pointer of type atomic_t
 * @i:  integer value to add
 *
 * Atomically adds @i to @v.
 * */
static inline void atomic_add(atomic_t * v, int i)
{
	__asm__ __volatile__ (
		__AMO(add) " zero, %1, %0"
		: "+A" (v->counter)
		: "r" (i));
}

/* *
 * atomic_sub - subtract integer from atomic variable
 * @v:  pointer of type atomic_t
 * @i:  integer value to subtract
 *
 * Atomically subtracts @i from @v.
 * */
static inline void atomic_sub(atomic_t * v, int i)
{
	atomic_add(v, -i);
}

/* *
 * atomic_sub_test_zero - subtract value from variable and test result
 * @v:  pointer of type atomic_t
 * @i:  integer value to subtract
 *
 * Atomically subtracts @i from @v and
 * returns true if the result is zero, or false for all other cases.
 * */
static inline bool atomic_sub_test_zero(atomic_t * v, int i)
{
	return (atomic_sub_return(v, i) == 0);
}

/* *
 * atomic_inc - increment atomic variable
 * @v:  pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 * */
static inline void atomic_inc(atomic_t * v)
{
	atomic_add(v, 1);
}

/* *
 * atomic_dec - decrement atomic variable
 * @v:  pointer of type atomic_t
 *
 * Atomically decrements @v by 1.
 * */
static inline void atomic_dec(atomic_t * v)
{
	atomic_sub(v, 1);
}

/* *
 * atomic_inc_test_zero - increment and test
 * @v:  pointer of type atomic_t
 *
 * Atomically increments @v by 1 and
 * returns true if the result is zero, or false for all other cases.
 * */
static inline bool atomic_inc_test_zero(atomic_t * v)
{
	return (atomic_add_return(v, 1) == 0);
}

/* *
 * atomic_dec_test_zero - decrement and test
 * @v:  pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other cases.
 * */
static inline bool atomic_dec_test_zero(atomic_t * v)
{
	return (atomic_sub_return(v, 1) == 0);
}

/* *
 * atomic_add_return - add integer and return
 * @i:  integer value to add
 * @v:  pointer of type atomic_t
 *
 * Atomically adds @i to @v and returns @i + @v
 * Requires Modern 486+ processor
 * */
static inline int atomic_add_return(atomic_t * v, int i)
{
	register int c;

	__asm__ __volatile__ (
		__AMO(add) " %0, %2, %1"
		: "=r" (c), "+A" (v->counter)
		: "r" (i));
	return (c + i);
}

/* *
 * atomic_sub_return - subtract integer and return
 * @v:  pointer of type atomic_t
 * @i:  integer value to subtract
 *
 * Atomically subtracts @i from @v and returns @v - @i
 * */
static inline int atomic_sub_return(atomic_t * v, int i)
{
	return atomic_add_return(v, -i);
}

static inline void set_bit(int nr, volatile void *addr)
    __attribute__ ((always_inline));
static inline void clear_bit(int nr, volatile void *addr)
    __attribute__ ((always_inline));
static inline void change_bit(int nr, volatile void *addr)
    __attribute__ ((always_inline));
static inline bool test_and_set_bit(int nr, volatile void *addr)
    __attribute__ ((always_inline));
static inline bool test_and_clear_bit(int nr, volatile void *addr)
    __attribute__ ((always_inline));
static inline bool test_and_change_bit(int nr, volatile void *addr)
    __attribute__ ((always_inline));
static inline bool test_bit(int nr, volatile void *addr)
    __attribute__ ((always_inline));

#define BIT_MASK(nr) (1UL << ((nr) % __riscv_xlen))
#define BIT_WORD(nr) ((nr) / __riscv_xlen)

#define __test_and_op_bit(op, mod, nr, addr)                         \
    ({                                                               \
        unsigned long __res, __mask;                                 \
        __mask = BIT_MASK(nr);                                       \
        __asm__ __volatile__(__AMO(op) " %0, %2, %1"                 \
                             : "=r"(__res), "+A"(addr[BIT_WORD(nr)]) \
                             : "r"(mod(__mask)));                    \
        ((__res & __mask) != 0);                                     \
    })

#define __op_bit(op, mod, nr, addr)                 \
    __asm__ __volatile__(__AMO(op) " zero, %1, %0"  \
                         : "+A"(addr[BIT_WORD(nr)]) \
                         : "r"(mod(BIT_MASK(nr))))

/* Bitmask modifiers */
#define __NOP(x) (x)
#define __NOT(x) (~(x))

/* *
 * set_bit - Atomically set a bit in memory
 * @nr:     the bit to set
 * @addr:   the address to start counting from
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 * */
static inline void set_bit(int nr, volatile void *addr)
{
	__op_bit(or, __NOP, nr, ((volatile unsigned long *)addr));
}

/* *
 * clear_bit - Atomically clears a bit in memory
 * @nr:     the bit to clear
 * @addr:   the address to start counting from
 * */
static inline void clear_bit(int nr, volatile void *addr)
{
    __op_bit(and, __NOT, nr, ((volatile unsigned long *)addr));
}

/* *
 * change_bit - Atomically toggle a bit in memory
 * @nr:     the bit to change
 * @addr:   the address to start counting from
 * */
static inline void change_bit(int nr, volatile void *addr)
{
	 __op_bit (xor, __NOP, nr, ((volatile unsigned long *)addr));
}

/* *
 * test_and_set_bit - Atomically set a bit and return its old value
 * @nr:     the bit to set
 * @addr:   the address to count from
 * */
static inline bool test_and_set_bit(int nr, volatile void *addr)
{
	return __test_and_op_bit(or, __NOP, nr, ((volatile unsigned long *)addr));
}

/* *
 * test_and_clear_bit - Atomically clear a bit and return its old value
 * @nr:     the bit to clear
 * @addr:   the address to count from
 * */
static inline bool test_and_clear_bit(int nr, volatile void *addr)
{
	return __test_and_op_bit(and, __NOT, nr, ((volatile unsigned long *)addr));
}

/* *
 * test_and_change_bit - Atomically change a bit and return its old value
 * @nr:     the bit to change
 * @addr:   the address to count from
 * */
static inline bool test_and_change_bit(int nr, volatile void *addr)
{
	return __test_and_op_bit(xor, __NOP, nr, ((volatile unsigned long *)addr));
}

/* *
 * test_bit - Determine whether a bit is set
 * @nr:     the bit to test
 * @addr:   the address to count from
 * */
static inline bool test_bit(int nr, volatile void *addr)
{
	return (((*(volatile unsigned long *)addr) >> nr) & 1);
}

#endif /* !__ARCH_UM_INCLUDE_ATOMIC_H */
