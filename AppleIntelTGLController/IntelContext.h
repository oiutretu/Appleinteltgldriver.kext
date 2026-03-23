/*
 * IntelContext.h
 *
 * GPU context management
 * Manages GPU execution context state including ring state,
 * page tables, and context-specific resources
 * Ported from Linux gem_context.c
 */

#ifndef INTEL_CONTEXT_H
#define INTEL_CONTEXT_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "linux_compat.h"

class AppleIntelTGLController;
class IntelGEMObject;
class IntelRingBuffer;
class IntelPPGTT;

/* Context Flags */
#define CONTEXT_BANNED      (1 << 0)  // Context has caused GPU hang
#define CONTEXT_CLOSED      (1 << 1)  // Context is closed
#define CONTEXT_DEFAULT     (1 << 2)  // Default context

/* Context State Size (Gen12) */
#define GEN12_CONTEXT_SIZE  (12 * 4096)  // 48KB for Gen12 context state

/* Context Statistics */
struct context_stats {
    u64 switches;          // Number of context switches
    u64 submissions;       // Commands submitted
    u64 active_time_ns;    // Time GPU was active
    u32 last_seqno;        // Last sequence number
    u32 hangs;            // Number of GPU hangs
};

class IntelContext {
public:
    IntelContext();
    virtual ~IntelContext();
    
    // Initialization
    bool init(AppleIntelTGLController *ctrl, u32 contextId);
    bool initWithController(AppleIntelTGLController *ctrl) { return init(ctrl, 0); }  // Convenience method
    void cleanup();
    void release() { cleanup(); }  // Alias for cleanup
    
    // Context State
    bool allocateContextState();
    bool saveState();
    bool restoreState();
    
    // PPGTT (Per-Process GTT)
    bool createPPGTT();
    IntelPPGTT* getPPGTT() const { return ppgtt; }
    
    // Ring Binding
    bool bindRing(IntelRingBuffer *ring);
    bool unbindRing();
    IntelRingBuffer* getRing() const { return boundRing; }
    
    // Context Control
    bool activate();
    bool deactivate();
    bool reset();
    
    // Flags
    void setFlag(u32 flag) { flags |= flag; }
    void clearFlag(u32 flag) { flags &= ~flag; }
    bool hasFlag(u32 flag) const { return (flags & flag) != 0; }
    
    // Query
    u32 getId() const { return contextId; }
    bool isActive() const { return active; }
    bool isBanned() const { return hasFlag(CONTEXT_BANNED); }
    bool isDefault() const { return hasFlag(CONTEXT_DEFAULT); }
    
    // Statistics
    void getStats(struct context_stats *stats);
    void printStats();
    
    // Hardware Context Object
    IntelGEMObject* getContextObject() const { return contextObj; }
    u64 getContextAddress() const;
    
    // GPU Hang Recovery
    bool isValid() const;
    IOReturn restoreAfterReset();
    
private:
    AppleIntelTGLController *controller;
    
    // Context Identity
    u32 contextId;
    u32 flags;
    bool active;
    
    // Context State
    IntelGEMObject *contextObj;   // Hardware context state
    void *contextVirtual;         // CPU mapping
    u64 contextGpuAddress;        // GPU address
    
    // PPGTT (Per-Process GTT)
    IntelPPGTT *ppgtt;
    
    // Ring Buffer
    IntelRingBuffer *boundRing;
    
    // Statistics
    struct context_stats stats;
    IOLock *statsLock;
    
    // Private Methods
    bool writeContextState();
    bool readContextState();
    u32 generateContextId();
};

#endif // INTEL_CONTEXT_H
