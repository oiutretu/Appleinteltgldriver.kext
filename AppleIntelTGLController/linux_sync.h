/*
 * linux_sync.h - Linux synchronization primitives for macOS
 * 
 * This header provides macOS equivalents for Linux kernel synchronization
 * mechanisms including mutexes, spinlocks, and atomic operations.
 */

#ifndef LINUX_SYNC_H
#define LINUX_SYNC_H

#include "linux_types.h"
#include <IOKit/IOLocks.h>
#include <libkern/OSAtomic.h>

#ifdef __cplusplus
extern "C" {
#endif


/* READ_ONCE/WRITE_ONCE - prevent compiler optimization */
#ifdef __cplusplus
#define READ_ONCE(x) (*static_cast<volatile __typeof__(x)*>(&(x)))
#define WRITE_ONCE(x, val) (*static_cast<volatile __typeof__(x)*>(&(x)) = (val))
#else
#define READ_ONCE(x) (*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val) ((*(volatile typeof(x) *)&(x)) = (val))
#endif


struct mutex {
    IOLock *lock;
};

#define __MUTEX_INITIALIZER(name) { NULL }

static inline void mutex_init(struct mutex *m)
{
    m->lock = IOLockAlloc();
}

static inline void mutex_destroy(struct mutex *m)
{
    if (m->lock) {
        IOLockFree(m->lock);
        m->lock = NULL;
    }
}

static inline void mutex_lock(struct mutex *m)
{
    IOLockLock(m->lock);
}

static inline int mutex_lock_interruptible(struct mutex *m)
{
    // IOKit doesn't have interruptible locks
    // Always lock and return success
    mutex_lock(m);
    return 0;
}

static inline int mutex_trylock(struct mutex *m)
{
    return IOLockTryLock(m->lock) ? 1 : 0;
}

static inline void mutex_unlock(struct mutex *m)
{
    IOLockUnlock(m->lock);
}

static inline bool mutex_is_locked(struct mutex *m)
{
    // IOKit doesn't provide a way to check lock state
    // Try to lock and immediately unlock if successful
    if (IOLockTryLock(m->lock)) {
        IOLockUnlock(m->lock);
        return false;
    }
    return true;
}


struct recursive_mutex {
    IORecursiveLock *lock;
};

static inline void recursive_mutex_init(struct recursive_mutex *m)
{
    m->lock = IORecursiveLockAlloc();
}

static inline void recursive_mutex_destroy(struct recursive_mutex *m)
{
    if (m->lock) {
        IORecursiveLockFree(m->lock);
        m->lock = NULL;
    }
}

static inline void recursive_mutex_lock(struct recursive_mutex *m)
{
    IORecursiveLockLock(m->lock);
}

static inline void recursive_mutex_unlock(struct recursive_mutex *m)
{
    IORecursiveLockUnlock(m->lock);
}


typedef struct {
    IOSimpleLock *lock;
} spinlock_t;

#define __SPIN_LOCK_UNLOCKED(name) { NULL }

static inline void spin_lock_init(spinlock_t *lock)
{
    lock->lock = IOSimpleLockAlloc();
}

static inline void spin_lock_destroy(spinlock_t *lock)
{
    if (lock->lock) {
        IOSimpleLockFree(lock->lock);
        lock->lock = NULL;
    }
}

static inline void spin_lock(spinlock_t *lock)
{
    IOSimpleLockLock(lock->lock);
}

static inline void spin_unlock(spinlock_t *lock)
{
    IOSimpleLockUnlock(lock->lock);
}

static inline int spin_trylock(spinlock_t *lock)
{
    return IOSimpleLockTryLock(lock->lock) ? 1 : 0;
}

/* Spinlock variants with IRQ disable (macOS doesn't distinguish) */
#define spin_lock_irq(lock)                 spin_lock(lock)
#define spin_unlock_irq(lock)               spin_unlock(lock)
#define spin_lock_irqsave(lock, flags)      do { (void)(flags); spin_lock(lock); } while(0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(flags); spin_unlock(lock); } while(0)


typedef struct {
    IORWLock *lock;
} rwlock_t;

static inline void rwlock_init(rwlock_t *lock)
{
    lock->lock = IORWLockAlloc();
}

static inline void rwlock_destroy(rwlock_t *lock)
{
    if (lock->lock) {
        IORWLockFree(lock->lock);
        lock->lock = NULL;
    }
}

static inline void read_lock(rwlock_t *lock)
{
    IORWLockRead(lock->lock);
}

static inline void read_unlock(rwlock_t *lock)
{
    IORWLockUnlock(lock->lock);
}

static inline void write_lock(rwlock_t *lock)
{
    IORWLockWrite(lock->lock);
}

static inline void write_unlock(rwlock_t *lock)
{
    IORWLockUnlock(lock->lock);
}

/* RW lock variants with IRQ disable */
#define read_lock_irq(lock)                 read_lock(lock)
#define read_unlock_irq(lock)               read_unlock(lock)
#define write_lock_irq(lock)                write_lock(lock)
#define write_unlock_irq(lock)              write_unlock(lock)
#define read_lock_irqsave(lock, flags)      do { (void)(flags); read_lock(lock); } while(0)
#define read_unlock_irqrestore(lock, flags) do { (void)(flags); read_unlock(lock); } while(0)
#define write_lock_irqsave(lock, flags)     do { (void)(flags); write_lock(lock); } while(0)
#define write_unlock_irqrestore(lock, flags) do { (void)(flags); write_unlock(lock); } while(0)


#define ATOMIC_INIT(i)  { (i) }

static inline int atomic_read(const atomic_t *v)
{
    return v->counter;
}

static inline void atomic_set(atomic_t *v, int i)
{
    v->counter = i;
}

static inline void atomic_add(int i, atomic_t *v)
{
    OSAddAtomic(i, &v->counter);
}

static inline void atomic_sub(int i, atomic_t *v)
{
    OSAddAtomic(-i, &v->counter);
}

static inline void atomic_inc(atomic_t *v)
{
    OSIncrementAtomic(&v->counter);
}

static inline void atomic_dec(atomic_t *v)
{
    OSDecrementAtomic(&v->counter);
}

static inline int atomic_inc_return(atomic_t *v)
{
    return OSIncrementAtomic(&v->counter) + 1;
}

static inline int atomic_dec_return(atomic_t *v)
{
    return OSDecrementAtomic(&v->counter) - 1;
}

static inline int atomic_dec_and_test(atomic_t *v)
{
    return OSDecrementAtomic(&v->counter) == 1;
}

static inline int atomic_inc_and_test(atomic_t *v)
{
    return (OSIncrementAtomic(&v->counter) + 1) == 0;
}

static inline int atomic_add_return(int i, atomic_t *v)
{
    return OSAddAtomic(i, &v->counter) + i;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
    return OSAddAtomic(-i, &v->counter) - i;
}

static inline int atomic_cmpxchg(atomic_t *v, int old, int new_val)
{
    return OSCompareAndSwap(old, new_val, &v->counter) ? old : v->counter;
}

static inline int atomic_xchg(atomic_t *v, int new_val)
{
    int old;
    do {
        old = atomic_read(v);
    } while (!OSCompareAndSwap(old, new_val, &v->counter));
    return old;
}

/* 64-bit atomics */
static inline long atomic64_read(const atomic64_t *v)
{
    return v->counter;
}

static inline void atomic64_set(atomic64_t *v, long i)
{
    v->counter = i;
}

static inline void atomic64_add(long i, atomic64_t *v)
{
    OSAddAtomic64(i, (SInt64 *)&v->counter);
}

static inline void atomic64_sub(long i, atomic64_t *v)
{
    OSAddAtomic64(-i, (SInt64 *)&v->counter);
}

static inline void atomic64_inc(atomic64_t *v)
{
    OSIncrementAtomic64((SInt64 *)&v->counter);
}

static inline void atomic64_dec(atomic64_t *v)
{
    OSDecrementAtomic64((SInt64 *)&v->counter);
}

/* Atomic bitops */
static inline void set_bit(int nr, volatile unsigned long *addr)
{
    OSTestAndSet(nr % 8, ((volatile UInt8 *)addr) + (nr / 8));
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
    OSTestAndClear(nr % 8, ((volatile UInt8 *)addr) + (nr / 8));
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
    return (addr[nr / BITS_PER_LONG] >> (nr % BITS_PER_LONG)) & 1;
}

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
    return OSTestAndSet(nr % 8, ((volatile UInt8 *)addr) + (nr / 8));
}

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
    return OSTestAndClear(nr % 8, ((volatile UInt8 *)addr) + (nr / 8));
}


struct semaphore {
    IOLock *lock;
    int count;
};

static inline void sema_init(struct semaphore *sem, int val)
{
    sem->lock = IOLockAlloc();
    sem->count = val;
}

static inline void sema_destroy(struct semaphore *sem)
{
    if (sem->lock) {
        IOLockFree(sem->lock);
        sem->lock = NULL;
    }
}

static inline void down(struct semaphore *sem)
{
    IOLockLock(sem->lock);
    while (sem->count <= 0) {
        IOLockSleep(sem->lock, sem, THREAD_UNINT);
    }
    sem->count--;
    IOLockUnlock(sem->lock);
}

static inline void up(struct semaphore *sem)
{
    IOLockLock(sem->lock);
    sem->count++;
    IOLockWakeup(sem->lock, sem, true);
    IOLockUnlock(sem->lock);
}

/* RCU is complex, providing basic stubs for now */

#define rcu_read_lock()         do { } while (0)
#define rcu_read_unlock()       do { } while (0)
#define rcu_dereference(p)      (p)
#define rcu_assign_pointer(p, v) do { (p) = (v); } while (0)
#define synchronize_rcu()       do { } while (0)


typedef struct {
    spinlock_t lock;
    unsigned sequence;
} seqlock_t;

static inline void seqlock_init(seqlock_t *sl)
{
    spin_lock_init(&sl->lock);
    sl->sequence = 0;
}

static inline void write_seqlock(seqlock_t *sl)
{
    spin_lock(&sl->lock);
    ++sl->sequence;
    smp_wmb();
}

static inline void write_sequnlock(seqlock_t *sl)
{
    smp_wmb();
    ++sl->sequence;
    spin_unlock(&sl->lock);
}

static inline unsigned read_seqbegin(const seqlock_t *sl)
{
    unsigned ret = READ_ONCE(sl->sequence);
    smp_rmb();
    return ret;
}

static inline int read_seqretry(const seqlock_t *sl, unsigned start)
{
    smp_rmb();
    return (sl->sequence != start);
}

#ifdef __cplusplus
}
#endif

#endif /* LINUX_SYNC_H */
