/*
 * IntelGEM.h
 * 
 * Graphics Execution Manager - Main memory manager
 * Ported from Linux i915_gem.c/h
 */

#ifndef IntelGEM_h
#define IntelGEM_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include "IntelGEMObject.h"
#include "linux_compat.h"

// Forward declarations
class AppleIntelTGLController;
class IntelUncore;

/* GEM statistics */
struct gem_stats {
    u64 total_objects;          /* Total objects created */
    u64 active_objects;         /* Currently active objects */
    u64 total_memory;           /* Total memory allocated */
    u64 active_memory;          /* Currently allocated memory */
    u64 peak_memory;            /* Peak memory usage */
    u64 allocation_failures;    /* Failed allocations */
};

class IntelGEM {
public:
    /* Initialization */
    bool init(AppleIntelTGLController *controller);
    void cleanup();
    
    /* Object management */
    IntelGEMObject* createObject(u64 size, u32 flags = 0);
    void destroyObject(IntelGEMObject *obj);
    
    /* Object tracking */
    void trackObject(IntelGEMObject *obj);
    void untrackObject(IntelGEMObject *obj);
    
    /* Memory operations */
    bool allocateMemory(IntelGEMObject *obj);
    void freeMemory(IntelGEMObject *obj);
    
    /* Synchronization */
    bool waitForIdle(u64 timeout_ms);
    bool flushAll();
    
    /* Statistics */
    void getStatistics(struct gem_stats *stats);
    void printStatistics();
    
    /* Accessors */
    AppleIntelTGLController* getController() const { return controller; }
    IntelUncore* getUncore() const;
    bool isInitialized() const { return initialized; }
    
private:
    /* Object list management */
    void addToList(IntelGEMObject *obj);
    void removeFromList(IntelGEMObject *obj);
    
    /* Memory limit checking */
    bool checkMemoryLimit(u64 size);
    void updateMemoryStats(s64 delta);  // s64 is int64_t
    
    /* Parent controller */
    AppleIntelTGLController *controller;
    
    /* Object tracking */
    IntelGEMObject *object_list_head;
    IntelGEMObject *object_list_tail;
    IOLock *object_list_lock;
    
    /* Statistics */
    struct gem_stats stats;
    IOLock *stats_lock;
    
    /* Configuration */
    u64 max_memory;             /* Maximum memory to allocate */
    
    /* State */
    bool initialized;
};

#endif /* IntelGEM_h */
