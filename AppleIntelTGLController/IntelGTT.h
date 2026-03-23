/*
 * IntelGTT.h
 *
 * Graphics Translation Table (GTT) implementation
 * Manages GPU virtual address space and page table entries
 * Ported from Linux gem_gtt.c
 */

#ifndef INTEL_GTT_H
#define INTEL_GTT_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "linux_compat.h"

class AppleIntelTGLController;
class IntelGEMObject;
class IntelUncore;

/* GTT Entry Flags */
#define GTT_PAGE_PRESENT    (1ULL << 0)  // Page is valid
#define GTT_PAGE_WRITEABLE  (1ULL << 1)  // Page is writable
#define GTT_PAGE_CACHE_LLC  (3ULL << 8)  // Last Level Cache
#define GTT_PAGE_CACHE_L3   (1ULL << 10) // L3 cache

/* Address Space Types */
enum i915_address_space_type {
    GTT_TYPE_GGTT = 0,  // Global GTT (all contexts)
    GTT_TYPE_PPGTT,     // Per-Process GTT (per context)
};

/* GTT Page Table Entry (Gen12+) */
struct gen12_pte_t {
    u64 val;
} __attribute__((packed));

/* GTT Statistics */
struct gtt_stats {
    size_t total_entries;
    size_t used_entries;
    size_t free_entries;
    u64 total_bytes;
    u64 used_bytes;
    u64 free_bytes;
    u32 insert_count;
    u32 clear_count;
    u32 bind_count;
    u32 unbind_count;
};

class IntelGTT {
public:
    IntelGTT();
    virtual ~IntelGTT();
    
    // Initialization
    bool init(AppleIntelTGLController *ctrl);
    void cleanup();
    
    // GTT Operations
    bool insertEntries(u64 start, IOMemoryDescriptor *mem, u32 flags);
    bool clearEntries(u64 start, size_t size);
    bool bindObject(IntelGEMObject *obj, u32 cache_level);
    bool unbindObject(IntelGEMObject *obj);
    uint64_t getGTTBasePhysicalAddress();

    //  CRITICAL: Surface scanout binding (system physical addr -> GTT offset)
    uint32_t bindSurfacePages(IOPhysicalAddress sysPhys, size_t size);
    bool unbindSurfacePages(uint32_t gttOffset, size_t size);
    
    // Address Allocation
    u64 allocateSpace(size_t size, size_t alignment);
    bool freeSpace(u64 address, size_t size);
    
    // Cache Management
    void flush();
    void invalidate();
    
    // Query
    size_t getTotalSize() const { return totalSize; }
    size_t getUsableSize() const { return usableSize; }
    size_t getUsedSize() const;
    bool isValid(u64 address, size_t size) const;
    
    // Statistics
    void getStats(struct gtt_stats *stats);
    void printStats();
    
private:
    AppleIntelTGLController *controller;
    IntelUncore *uncore;
    
    // GTT Memory Mapping
    IOMemoryMap *gttMap;
    void *gttBase;           // Virtual address of GTT
    size_t gttSize;          // Size in bytes
    
    // Address Space
    u64 baseAddress;         // Start of GTT address space
    size_t totalSize;        // Total address space size
    size_t usableSize;       // Usable size (excluding reserved)
    size_t pageSize;         // Page size (4KB)
    size_t numEntries;       // Number of PTE entries
    
    // Free Space Tracking (simple bitmap for now)
    u8 *allocationBitmap;
    size_t bitmapSize;
    // IOLock *allocationLock;  // REMOVED - lock-free bitmap operations
    
    // Statistics
    struct gtt_stats stats;
    IOLock *statsLock;
    
    // Private Methods
    bool detectGTTSize();
    bool mapGTTMemory();
    bool initializePageTables();
    
    uint32_t findFreeGTTRegion(uint32_t numPages);  // Helper for bindSurfacePages
public:
    void writePTE(size_t index, u64 physAddr, u32 flags);
    u64 readPTE(size_t index);
    void flushPTE(size_t index);
    
    u64 makeGen12PTE(u64 physAddr, u32 flags);
    
    size_t findFreeSpace(size_t numPages, size_t alignmentPages);
    void markSpaceUsed(size_t startPage, size_t numPages);
    void markSpaceFree(size_t startPage, size_t numPages);
};

#endif // INTEL_GTT_H
