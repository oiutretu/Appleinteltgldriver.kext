/*
 * linux_compat.h - Master compatibility header
 * 
 * Include this header to get all Linux compatibility definitions.
 */

#ifndef LINUX_COMPAT_H
#define LINUX_COMPAT_H

/* Include all compatibility headers */
#include "linux_types.h"
#include "linux_mm.h"
#include "linux_sync.h"
#include "linux_pci.h"
#include "linux_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Logging functions */
#include <IOKit/IOLib.h>

#define printk(fmt, ...)        IOLog(fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)       IOLog("INFO: " fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)       IOLog("WARNING: " fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)        IOLog("ERROR: " fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)      IOLog("DEBUG: " fmt, ##__VA_ARGS__)

/* Device logging */
#define dev_info(dev, fmt, ...)  IOLog("INFO: " fmt, ##__VA_ARGS__)
#define dev_warn(dev, fmt, ...)  IOLog("WARN: " fmt, ##__VA_ARGS__)
#define dev_err(dev, fmt, ...)   IOLog("ERROR: " fmt, ##__VA_ARGS__)
#define dev_dbg(dev, fmt, ...)   IOLog("DEBUG: " fmt, ##__VA_ARGS__)

/* Assertions */
#define BUG()                   panic("BUG at %s:%d", __FILE__, __LINE__)
#define BUG_ON(condition)       do { if (unlikely(condition)) BUG(); } while(0)
#define WARN_ON(condition)      ({                                          \
    int __ret_warn_on = !!(condition);                                      \
    if (unlikely(__ret_warn_on))                                            \
        IOLog("WARNING at %s:%d\n", __FILE__, __LINE__);                    \
    unlikely(__ret_warn_on);                                                \
})
#define WARN_ON_ONCE(condition) ({                                          \
    static bool __warned;                                                    \
    int __ret_warn_once = !!(condition);                                    \
    if (unlikely(__ret_warn_once && !__warned)) {                           \
        __warned = true;                                                    \
        IOLog("WARNING at %s:%d\n", __FILE__, __LINE__);                    \
    }                                                                       \
    unlikely(__ret_warn_once);                                              \
})

/* Module parameters - convert to sysctl */
#define module_param(name, type, perm)
#define module_param_named(name, value, type, perm)
#define MODULE_PARM_DESC(var, desc)

/* Module information */
#define MODULE_AUTHOR(a)
#define MODULE_DESCRIPTION(d)
#define MODULE_LICENSE(l)
#define MODULE_VERSION(v)
#define MODULE_FIRMWARE(f)

/* Build-time checks */
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

/* String functions */
#include <string.h>
#define kstrdup(s, flags)       ({                                          \
    char *__new = (char *)kmalloc(strlen(s) + 1, flags);                    \
    if (__new) strcpy(__new, s);                                            \
    __new;                                                                  \
})

/* Misc helpers */
#define DIV_ROUND_UP(n, d)      (((n) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(x, divisor) ({                                    \
    typeof(x) __x = x;                                                      \
    typeof(divisor) __d = divisor;                                          \
    ((__x) + ((__d) / 2)) / (__d);                                          \
})

#define roundup(x, y)           ((((x) + ((y) - 1)) / (y)) * (y))
#define rounddown(x, y)         (((x) / (y)) * (y))

/* Swap */
#define swap(a, b)              do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

/* Clamp */
#define clamp(val, lo, hi)      min((typeof(val))max(val, lo), hi)
#define clamp_t(type, val, lo, hi) min((type)max((type)(val), (type)(lo)), (type)(hi))

/* Initialization status */
extern bool i915_compat_initialized;

/* Initialize compatibility layer */
void i915_compat_init(void);
void i915_compat_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* LINUX_COMPAT_H */
