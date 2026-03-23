/*
 * linux_time.h - Linux time/delay functions for macOS
 */

#ifndef LINUX_TIME_H
#define LINUX_TIME_H

#include "linux_types.h"
#include <IOKit/IOLib.h>
#include <kern/clock.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Jiffies - system tick counter */
extern volatile unsigned long jiffies;

#define HZ              1000    /* Ticks per second (macOS typically 1000Hz) */

#define MAX_JIFFY_OFFSET ((LONG_MAX >> 1)-1)

/* Time conversions */
#define msecs_to_jiffies(ms)    ((ms) * HZ / 1000)
#define jiffies_to_msecs(j)     ((j) * 1000 / HZ)
#define jiffies_to_usecs(j)     ((j) * 1000000 / HZ)
#define usecs_to_jiffies(us)    ((us) * HZ / 1000000)

/* Time comparison */
#define time_after(a,b)         ((long)((b) - (a)) < 0)
#define time_before(a,b)        time_after(b,a)
#define time_after_eq(a,b)      ((long)((a) - (b)) >= 0)
#define time_before_eq(a,b)     time_after_eq(b,a)

/* Delays */
static inline void msleep(unsigned int msecs)
{
    IOSleep(msecs);
}

static inline void usleep_range(unsigned long min, unsigned long max)
{
    IOSleep((min + max) / 2 / 1000); // Convert to ms
}

static inline void mdelay(unsigned int msecs)
{
    IODelay(msecs * 1000); // Busy wait
}

/* AbsoluteTime comparison macro for macOS */
#ifndef CMP_ABSOLUTETIME
#define CMP_ABSOLUTETIME(t1, t2) \
    (((t1) > (t2)) ? 1 : (((t1) < (t2)) ? -1 : 0))
#endif

static inline void udelay(unsigned int usecs)
{
    IODelay(usecs); // Busy wait
}

static inline void ndelay(unsigned int nsecs)
{
    IODelay(nsecs / 1000); // Best we can do
}

/* Get time */
static inline u64 ktime_get_ns(void)
{
    clock_sec_t secs;
    clock_nsec_t nsecs;
    clock_get_system_nanotime(&secs, &nsecs);
    return (u64)secs * 1000000000ULL + nsecs;
}

static inline u64 ktime_get_raw_ns(void)
{
    return ktime_get_ns();
}

static inline ktime_t ktime_get(void)
{
    return (ktime_t)ktime_get_ns();
}

static inline u64 ktime_to_ns(ktime_t kt)
{
    return (u64)kt;
}

static inline ktime_t ns_to_ktime(u64 ns)
{
    return (ktime_t)ns;
}

/* Timespec conversion */
struct timespec64 {
    time_t tv_sec;
    long tv_nsec;
};

static inline struct timespec64 ktime_to_timespec64(ktime_t kt)
{
    struct timespec64 ts;
    u64 nsec = ktime_to_ns(kt);
    ts.tv_sec = nsec / 1000000000ULL;
    ts.tv_nsec = nsec % 1000000000ULL;
    return ts;
}

/* Timeout helpers */
static inline unsigned long round_jiffies_up(unsigned long j)
{
    return j + HZ;
}

#ifdef __cplusplus
}
#endif

#endif /* LINUX_TIME_H */
