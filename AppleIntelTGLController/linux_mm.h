/*
 * linux_mm.h - Linux memory management for macOS
 * 
 * This header provides macOS equivalents for Linux kernel memory
 * allocation and management functions.
 */

#ifndef LINUX_MM_H
#define LINUX_MM_H

#include "linux_types.h"

// Undefine min/max macros before including IOKit headers
// libkern.h defines these as functions and will conflict with our macros
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/OSMalloc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory allocation flags */
#define GFP_KERNEL              0x00000001
#define GFP_ATOMIC              0x00000002
#define GFP_NOWAIT              0x00000004
#define GFP_DMA                 0x00000008
#define GFP_DMA32               0x00000010
#define __GFP_ZERO              0x00000100
#define __GFP_HIGHMEM           0x00000200
#define __GFP_COLD              0x00000400
#define __GFP_NOWARN            0x00000800
#define __GFP_RETRY_MAYFAIL     0x00001000
#define __GFP_NORETRY           0x00002000

/* Page size */
#define PAGE_SHIFT              12
#define PAGE_SIZE               (1UL << PAGE_SHIFT)
#define PAGE_MASK               (~(PAGE_SIZE - 1))

/* Page alignment */
#define PAGE_ALIGN(addr)        ALIGN(addr, PAGE_SIZE)
#define PAGE_ALIGNED(addr)      (((addr) & (PAGE_SIZE - 1)) == 0)

/* Alignment macros */
#define ALIGN(x, a)             __ALIGN_KERNEL((x), (a))
#define __ALIGN_KERNEL(x, a)    __ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define IS_ALIGNED(x, a)        (((x) & ((typeof(x))(a) - 1)) == 0)

/* Memory allocation tracking */
extern OSMallocTag apple_intel_tgl_malloc_tag;

/* Initialize memory subsystem */
static inline void i915_memory_init(void)
{
    extern OSMallocTag apple_intel_tgl_malloc_tag;
    if (!apple_intel_tgl_malloc_tag) {
        apple_intel_tgl_malloc_tag = OSMalloc_Tagalloc("com.apple.driver.AppleIntelTGL", OSMT_DEFAULT);
    }
}

static inline void i915_memory_cleanup(void)
{
    extern OSMallocTag apple_intel_tgl_malloc_tag;
    if (apple_intel_tgl_malloc_tag) {
        OSMalloc_Tagfree(apple_intel_tgl_malloc_tag);
        apple_intel_tgl_malloc_tag = NULL;
    }
}

/* Basic allocation */
static inline void *kmalloc(size_t size, gfp_t flags)
{
    (void)flags; // Ignore flags for now
    return IOMalloc(size);
}

static inline void *kzalloc(size_t size, gfp_t flags)
{
    void *ptr = kmalloc(size, flags);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

static inline void kfree(void *ptr)
{
    if (ptr) {
        // Note: IOFree requires size, but Linux kfree doesn't have it
        // We'll need to track sizes in production code
        // For now, this is a placeholder
        IOFree(ptr, 0); // This won't work - need size tracking!
    }
}

/* Tracked allocation - preferred method */
struct kmem_allocation {
    void *ptr;
    size_t size;
    struct list_head list;
};

static inline void *kmalloc_tracked(size_t size, gfp_t flags)
{
    void *ptr = IOMalloc(size);
    // TODO: Add to tracking list
    return ptr;
}

static inline void kfree_tracked(void *ptr, size_t size)
{
    if (ptr) {
        IOFree(ptr, size);
        // TODO: Remove from tracking list
    }
}

/* Aligned allocation */
static inline void *kmalloc_aligned(size_t size, size_t align, gfp_t flags)
{
    (void)flags;
    return IOMallocAligned(size, align);
}

static inline void kfree_aligned(void *ptr, size_t size)
{
    if (ptr) {
        IOFreeAligned(ptr, size);
    }
}

/* Contiguous memory allocation */
static inline void *kmalloc_contiguous(size_t size, gfp_t flags, dma_addr_t *dma_handle)
{
    (void)flags;
    IOPhysicalAddress phys;
    void *ptr = IOMallocContiguous(size, PAGE_SIZE, &phys);
    if (ptr && dma_handle) {
        *dma_handle = (dma_addr_t)phys;
    }
    return ptr;
}

static inline void kfree_contiguous(void *ptr, size_t size)
{
    if (ptr) {
        IOFreeContiguous(ptr, size);
    }
}

/* Virtual memory allocation */
static inline void *vmalloc(unsigned long size)
{
    return IOMalloc(size);
}

static inline void *vzalloc(unsigned long size)
{
    void *ptr = vmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

static inline void vfree(const void *addr)
{
    if (addr) {
        IOFree((void*)addr, 0); // Need size tracking!
    }
}

/* Page allocation */
struct page {
    void *virt_addr;  // 'virtual' is a C++ keyword, renamed to virt_addr
    phys_addr_t physical;
    atomic_t refcount;
};

static inline struct page *alloc_page(gfp_t flags)
{
    (void)flags;
    struct page *page = (struct page *)IOMalloc(sizeof(struct page));
    if (page) {
        IOPhysicalAddress phys;
        page->virt_addr = IOMallocContiguous(PAGE_SIZE, PAGE_SIZE, &phys);
        page->physical = phys;
        page->refcount.counter = 1;
    }
    return page;
}

static inline void __free_page(struct page *page)
{
    if (page) {
        if (page->virt_addr) {
            IOFreeContiguous(page->virt_addr, PAGE_SIZE);
        }
        IOFree(page, sizeof(struct page));
    }
}

/* Page operations */
static inline void *page_address(struct page *page)
{
    return page ? page->virt_addr : NULL;
}

static inline phys_addr_t page_to_phys(struct page *page)
{
    return page ? page->physical : 0;
}

static inline void get_page(struct page *page)
{
    if (page) {
        OSIncrementAtomic(&page->refcount.counter);
    }
}

static inline void put_page(struct page *page)
{
    if (page && OSDecrementAtomic(&page->refcount.counter) == 1) {
        __free_page(page);
    }
}

/* Memory barriers - use compiler barriers for kernel extensions */
#define mb()            __asm__ __volatile__("mfence":::"memory")
#define rmb()           __asm__ __volatile__("lfence":::"memory")
#define wmb()           __asm__ __volatile__("sfence":::"memory")
#define smp_mb()        __asm__ __volatile__("mfence":::"memory")
#define smp_rmb()       __asm__ __volatile__("lfence":::"memory")
#define smp_wmb()       __asm__ __volatile__("sfence":::"memory")

/* Cache operations */
static inline void flush_cache_range(void *addr, size_t size)
{
    // macOS equivalent
    OSSynchronizeIO();
}

static inline void clflush(volatile void *addr)
{
    // Cache line flush
    __asm__ __volatile__("clflush %0" : "+m" (*(volatile char *)addr));
}

static inline void clflushopt(volatile void *addr)
{
    // Optimized cache line flush
    __asm__ __volatile__("clflushopt %0" : "+m" (*(volatile char *)addr));
}

/* Memory copy helpers */
#define memcpy_fromio(dst, src, len)  memcpy(dst, (const void *)(src), len)
#define memcpy_toio(dst, src, len)    memcpy((void *)(dst), src, len)
#define memset_io(dst, val, len)      memset((void *)(dst), val, len)

/* Check if address is I/O memory */
static inline bool virt_addr_valid(const void *addr)
{
    // Simple check - in production, need proper validation
    return addr != NULL;
}

/* Get physical address from virtual */
static inline phys_addr_t virt_to_phys(volatile void *address)
{
    // This is a placeholder - need proper implementation
    // using IOMemoryDescriptor
    return 0;
}

static inline void *phys_to_virt(phys_addr_t address)
{
    // This is a placeholder - need proper implementation
    return NULL;
}

/* PFN (Page Frame Number) operations */
#define virt_to_pfn(kaddr)      ((unsigned long)(kaddr) >> PAGE_SHIFT)
#define pfn_to_virt(pfn)        ((void *)((pfn) << PAGE_SHIFT))

/* Memory statistics (stubs for now) */
static inline unsigned long totalram_pages(void)
{
    return 0; // TODO: Get from system
}

static inline unsigned long nr_free_pages(void)
{
    return 0; // TODO: Get from system
}

#ifdef __cplusplus
}
#endif

// Redefine min/max macros after IOKit headers
// Now libkern.h functions are already defined, we can redefine as macros for Linux compat
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

#endif /* LINUX_MM_H */
