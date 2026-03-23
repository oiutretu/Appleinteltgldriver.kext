/*
 * IntelFence.h - GPU Synchronization Fence
 *
 * Fences are synchronization primitives that allow:
 * - Async command submission (return immediately)
 * - Efficient waiting (block until GPU completes)
 * - Multi-waiter support (multiple threads can wait)
 * - Signaling from interrupt context
 */

#ifndef INTELFENCE_H
#define INTELFENCE_H

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>

class IntelFence : public OSObject {
    OSDeclareDefaultStructors(IntelFence)
    
public:
    // Creation
    static IntelFence* create(uint32_t id);
    
    // Lifecycle
    virtual bool init() APPLE_KEXT_OVERRIDE;
    virtual void free() APPLE_KEXT_OVERRIDE;
    
    // Fence operations
    bool wait(uint32_t timeoutMs);      // Block until signaled or timeout
    void signal();                       // Signal completion (from interrupt)
    bool isSignaled() const;             // Check if already signaled
    void reset();                        // Reset to unsignaled state
    
    // Properties
    uint32_t getId() const { return fenceId; }
    uint64_t getSignalTime() const { return signalTime; }
    
    // Context tracking
    void setSeqno(uint32_t seqno) { this->seqno = seqno; }
    uint32_t getSeqno() const { return seqno; }
    
    void setEngineId(uint32_t engine) { this->engineId = engine; }
    uint32_t getEngineId() const { return engineId; }
    
private:
    uint32_t    fenceId;        // Unique fence ID
    bool        signaled;       // True when GPU completed
    uint32_t    seqno;          // Associated sequence number
    uint32_t    engineId;       // Which engine this fence is for
    uint64_t    signalTime;     // When it was signaled (for debugging)
    
    IOLock*     fenceLock;      // Protects signaled flag
    IOLock*     waitLock;       // For wait/signal mechanism
};

#endif /* INTELFENCE_H */
