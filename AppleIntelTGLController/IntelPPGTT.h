/*
 * IntelPPGTT.h
 * 
 * Per-Process Graphics Translation Table
 * Provides isolated virtual address space per GPU context
 * Ported from Linux i915_gem_gtt.c (PPGTT portions)
 */

#ifndef INTEL_PPGTT_H
#define INTEL_PPGTT_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "linux_compat.h"

class AppleIntelTGLController;
class IntelGEMObject;
class IntelGTT;

/* PPGTT Configuration (Gen12) */
#define PPGTT_ADDRESS_SPACE_SIZE  (256ULL * 1024 * 1024 * 1024)  // 256GB per context
#define PPGTT_PAGE_SIZE           4096

/* Page Directory Levels (Gen12 uses 4-level page tables) */
#define PPGTT_LEVEL_PML4  3  // Page Map Level 4
#define PPGTT_LEVEL_PDP   2  // Page Directory Pointer
#define PPGTT_LEVEL_PD    1  // Page Directory
#define PPGTT_LEVEL_PT    0  // Page Table

/* PPGTT Statistics */
struct ppgtt_stats {
    size_t total_entries;
    size_t used_entries;
    u64 total_bytes;
    u64 used_bytes;
    u32 insert_count;
    u32 clear_count;
};

class IntelPPGTT {
public:
    IntelPPGTT();
    virtual ~IntelPPGTT();
    
    // Initialization
    bool init(AppleIntelTGLController *ctrl);
    void cleanup();
    
    // PPGTT Operations
    bool insertEntries(u64 start, IOMemoryDescriptor *mem, u32 flags);
    bool clearEntries(u64 start, size_t size);
    bool bindObject(IntelGEMObject *obj, u32 cache_level);
    bool unbindObject(IntelGEMObject *obj);
    
    // Address Allocation
    u64 allocateSpace(size_t size, size_t alignment);
    bool freeSpace(u64 address, size_t size);
    
    // Query
    u64 getRootAddress() const { return pml4Address; }
    size_t getTotalSize() const { return totalSize; }
    size_t getUsedSize() const;
    bool isValid(u64 address, size_t size) const;
    
    // Statistics
    void getStats(struct ppgtt_stats *stats);
    void printStats();
    
private:
    AppleIntelTGLController *controller;
    IntelGTT *globalGtt;
    
    // Page Table Root (PML4)
    IntelGEMObject *pml4Obj;
    void *pml4Virtual;
    u64 pml4Address;
    
    // Address Space
    u64 baseAddress;
    size_t totalSize;
    size_t pageSize;
    
    // Free Space Tracking
    u8 *allocationBitmap;
    size_t bitmapSize;
    IOLock *allocationLock;
    
    // Statistics
    struct ppgtt_stats stats;
    IOLock *statsLock;
    
    // Private Methods
    bool allocatePageTables();
    bool initializeRoot();
    
    void writePTE(u64 address, u64 physAddr, u32 flags);
    u64 readPTE(u64 address);
    
    size_t findFreeSpace(size_t numPages, size_t alignmentPages);
    void markSpaceUsed(size_t startPage, size_t numPages);
    void markSpaceFree(size_t startPage, size_t numPages);
};

#endif // INTEL_PPGTT_H
