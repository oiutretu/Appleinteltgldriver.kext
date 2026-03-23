/*
 * linux_types.h - Linux kernel type definitions for macOS
 * 
 * This header provides macOS equivalents for common Linux kernel types
 * used throughout the i915 driver.
 */

#ifndef LINUX_TYPES_H
#define LINUX_TYPES_H

/* Don't include sys/types.h in kernel mode - conflicts with kernel headers */
#ifndef KERNEL
#include <sys/types.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic integer types */
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;

typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;
typedef int64_t   s64;

typedef uint8_t   __u8;
typedef uint16_t  __u16;
typedef uint32_t  __u32;
typedef uint64_t  __u64;

typedef int8_t    __s8;
typedef int16_t   __s16;
typedef int32_t   __s32;
typedef int64_t   __s64;

/* Boolean */
typedef bool      bool_t;

/* Size types - don't redefine if already defined in sys/types.h */
// size_t and ssize_t already defined by sys/types.h in kernel mode

/* Physical address types */
typedef uint64_t  phys_addr_t;
typedef uint64_t  dma_addr_t;
typedef uint64_t  resource_size_t;

/* Page frame number */
typedef unsigned long pfn_t;

/* Linux kernel specific types */
// Note: off_t and pid_t are already defined in sys/types.h in kernel mode
// Don't redefine them
#ifndef _PID_T
typedef int       pid_t;
#endif
typedef unsigned  gfp_t;    // Memory allocation flags
typedef unsigned  fmode_t;  // File mode
typedef u64       loff_t;   // File offset

/* Address space qualifiers (Linux kernel uses these for sparse type checking) */
#define __iomem        /* I/O memory space */
#define __user         /* User space pointer */
#define __kernel       /* Kernel space pointer */
#define __force        /* Force type conversion */
#define __bitwise      /* Bitwise type */

/* Atomic types - we'll use OSAtomic on macOS */
typedef struct {
    volatile int counter;
} atomic_t;

typedef struct {
    volatile long counter;
} atomic_long_t;

typedef struct {
    volatile u64 counter;
} atomic64_t;

/* List types - we'll provide wrappers */
struct list_head {
    struct list_head *next, *prev;
};

struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next, **pprev;
};

/* Work queue types */
struct work_struct;
struct delayed_work;
struct workqueue_struct;

/* Timer types */
struct timer_list;

/* Wait queue types */
struct wait_queue_head;
typedef struct wait_queue_head wait_queue_head_t;

/* Completion */
struct completion;

/* Device types */
struct device;
struct pci_dev;

/* DRM types placeholders */
struct drm_device;
struct drm_file;
struct drm_gem_object;

/* Time types */
typedef u64 ktime_t;
typedef unsigned long jiffies_t;

/* Error codes */
#ifndef EPERM
#define EPERM            1      /* Operation not permitted */
#define ENOENT           2      /* No such file or directory */
#define ESRCH            3      /* No such process */
#define EINTR            4      /* Interrupted system call */
#define EIO              5      /* I/O error */
#define ENXIO            6      /* No such device or address */
#define E2BIG            7      /* Argument list too long */
#define ENOEXEC          8      /* Exec format error */
#define EBADF            9      /* Bad file number */
#define ECHILD          10      /* No child processes */
#define EAGAIN          11      /* Try again */
#define ENOMEM          12      /* Out of memory */
#define EACCES          13      /* Permission denied */
#define EFAULT          14      /* Bad address */
#define ENOTBLK         15      /* Block device required */
#define EBUSY           16      /* Device or resource busy */
#define EEXIST          17      /* File exists */
#define EXDEV           18      /* Cross-device link */
#define ENODEV          19      /* No such device */
#define ENOTDIR         20      /* Not a directory */
#define EISDIR          21      /* Is a directory */
#define EINVAL          22      /* Invalid argument */
#define ENFILE          23      /* File table overflow */
#define EMFILE          24      /* Too many open files */
#define ENOTTY          25      /* Not a typewriter */
#define ETXTBSY         26      /* Text file busy */
#define EFBIG           27      /* File too large */
#define ENOSPC          28      /* No space left on device */
#define ESPIPE          29      /* Illegal seek */
#define EROFS           30      /* Read-only file system */
#define EMLINK          31      /* Too many links */
#define EPIPE           32      /* Broken pipe */
#define EDOM            33      /* Math argument out of domain of func */
#define ERANGE          34      /* Math result not representable */
#define ETIMEDOUT       110     /* Connection timed out */
#endif

/* Compiler attributes - define these first */
#define __aligned(x)            __attribute__((aligned(x)))
#define __packed                __attribute__((packed))
#define __maybe_unused          __attribute__((unused))
#define __always_inline         inline __attribute__((always_inline))
#define __must_check            __attribute__((warn_unused_result))

/* Pointer error handling */
#define MAX_ERRNO       4095
#define IS_ERR_VALUE(x) ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

static inline void * __must_check ERR_PTR(long error)
{
    return (void *) error;
}

static inline long __must_check PTR_ERR(const void *ptr)
{
    return (long) ptr;
}

static inline bool __must_check IS_ERR(const void *ptr)
{
    return IS_ERR_VALUE((unsigned long)ptr);
}

static inline bool __must_check IS_ERR_OR_NULL(const void *ptr)
{
    return !ptr || IS_ERR_VALUE((unsigned long)ptr);
}

/* Cache line size */
#ifndef L1_CACHE_BYTES
#define L1_CACHE_BYTES          64
#endif
#define SMP_CACHE_BYTES         L1_CACHE_BYTES
#define ____cacheline_aligned   __aligned(SMP_CACHE_BYTES)

/* NULL pointer */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* MIN/MAX macros - only define if not already defined */
#ifndef min
#define min(x, y) ({                \
    __typeof__(x) _min1 = (x);      \
    __typeof__(y) _min2 = (y);      \
    (void) (&_min1 == &_min2);      \
    _min1 < _min2 ? _min1 : _min2; })
#endif

#ifndef max
#define max(x, y) ({                \
    __typeof__(x) _max1 = (x);      \
    __typeof__(y) _max2 = (y);      \
    (void) (&_max1 == &_max2);      \
    _max1 > _max2 ? _max1 : _max2; })
#endif

/* Array size */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/* Bit operations */
#define BIT(nr)                 (1UL << (nr))
#define BIT_ULL(nr)             (1ULL << (nr))
#define GENMASK(h, l) \
    (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_ULL(h, l) \
    (((~0ULL) - (1ULL << (l)) + 1) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))

#define BITS_PER_LONG           (sizeof(long) * 8)
#define BITS_PER_LONG_LONG      (sizeof(long long) * 8)

/* Container of macro */
#ifndef container_of
#define container_of(ptr, type, member) ({                      \
    const __typeof__( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type,member) );})
#endif

/* Endianness */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define __LITTLE_ENDIAN_BITFIELD
#else
#define __BIG_ENDIAN_BITFIELD
#endif

/* Likely/Unlikely branch prediction */
#define likely(x)               __builtin_expect(!!(x), 1)
#define unlikely(x)             __builtin_expect(!!(x), 0)

/* Barriers */
#define barrier()               __asm__ __volatile__("": : :"memory")

/* Read/Write once */
#define READ_ONCE(x)            (*(volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, val)      (*(volatile __typeof__(x) *)&(x) = (val))

/* Swap bytes */
#define __swab16(x)             __builtin_bswap16(x)
#define __swab32(x)             __builtin_bswap32(x)
#define __swab64(x)             __builtin_bswap64(x)

#ifdef __cplusplus
}
#endif

#endif /* LINUX_TYPES_H */
