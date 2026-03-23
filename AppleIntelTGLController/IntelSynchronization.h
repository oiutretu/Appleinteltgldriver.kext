//
//  IntelSynchronization.h
//  Graphics Driver
//
//  Synchronization primitives for GPU command submission.
//  Week 23: Semaphores, timeline semaphores, cross-engine sync.
//

#ifndef IntelSynchronization_h
#define IntelSynchronization_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOTimerEventSource.h>
#include "AppleIntelTGLController.h"
#include "IntelRingBuffer.h"
#include "IntelRequest.h"

// Forward declarations
class AppleIntelTGLController;
class IntelRingBuffer;
class IntelRequest;

//
// Semaphore Types
//

enum IntelSemaphoreType {
    SEMAPHORE_TYPE_BINARY,      // 0 or 1 (signaled/unsignaled)
    SEMAPHORE_TYPE_COUNTING,    // 0-N (counter)
    SEMAPHORE_TYPE_TIMELINE     // Monotonic timeline value
};

enum IntelSemaphoreState {
    SEMAPHORE_STATE_IDLE,
    SEMAPHORE_STATE_WAITING,
    SEMAPHORE_STATE_SIGNALED,
    SEMAPHORE_STATE_ERROR
};

//
// Synchronization Errors
//

enum SyncError {
    SYNC_OK = 0,
    SYNC_ERROR_NULL_OBJECT,
    SYNC_ERROR_INVALID_TYPE,
    SYNC_ERROR_TIMEOUT,
    SYNC_ERROR_ALREADY_SIGNALED,
    SYNC_ERROR_NOT_SIGNALED,
    SYNC_ERROR_DEADLOCK,
    SYNC_ERROR_ENGINE_MISMATCH,
    SYNC_ERROR_OUT_OF_MEMORY,
    SYNC_ERROR_GPU_HANG,
    SYNC_ERROR_INVALID_VALUE
};

//
// Semaphore Structure
//

struct IntelSemaphore {
    uint32_t id;                        // Unique identifier
    IntelSemaphoreType type;            // Binary/counting/timeline
    IntelSemaphoreState state;          // Current state
    uint64_t value;                     // Current value (timeline/counting)
    uint64_t targetValue;               // Wait target (timeline)
    uint32_t maxValue;                  // Max value (counting)
    
    // GPU hardware
    uint64_t gpuAddress;                // GPU memory address
    uint32_t* cpuAddress;               // CPU-visible address
    class IntelGEMObject* semaphoreObj; // Backing memory object
    
    // Waiters
    IntelRequest** waiters;             // Array of waiting requests
    uint32_t waiterCount;               // Number of waiters
    uint32_t maxWaiters;                // Max waiters
    
    // Timing
    uint64_t signalTime;                // Time when signaled (ns)
    uint64_t waitTime;                  // Time when wait started (ns)
    
    // Statistics
    uint64_t signalCount;               // Total signals
    uint64_t waitCount;                 // Total waits
    uint64_t timeoutCount;              // Timeouts
    
    IORecursiveLock* lock;              // Thread safety
    IntelSemaphore* next;               // For free list
};

//
// Timeline Point (for timeline semaphores)
//

struct IntelTimelinePoint {
    uint64_t value;                     // Timeline value
    IntelRequest* request;              // Associated request
    uint64_t signalTime;                // When signaled (ns)
    IntelTimelinePoint* next;           // Next point
};

//
// Cross-Engine Dependency
//

struct IntelEngineDependency {
    IntelRingBuffer* sourceEngine;      // Source engine
    IntelRingBuffer* destEngine;        // Destination engine
    IntelRequest* sourceRequest;        // Source request
    IntelRequest* destRequest;          // Destination request
    IntelSemaphore* semaphore;          // Sync semaphore
    uint64_t createTime;                // Creation time (ns)
    IntelEngineDependency* next;
};

//
// Wait-for-Idle State
//

enum IdleState {
    IDLE_STATE_ACTIVE,                  // GPU is active
    IDLE_STATE_GOING_IDLE,              // Transitioning to idle
    IDLE_STATE_IDLE,                    // GPU is idle
    IDLE_STATE_TIMEOUT                  // Idle wait timeout
};

//
// Synchronization Statistics
//

struct SyncStats {
    uint64_t semaphoresCreated;
    uint64_t semaphoresDestroyed;
    uint64_t binaryWaits;
    uint64_t countingWaits;
    uint64_t timelineWaits;
    uint64_t crossEngineWaits;
    uint64_t timeouts;
    uint64_t deadlocksDetected;
    uint64_t averageWaitTimeUs;
    uint64_t maxWaitTimeUs;
    uint32_t activeSemaphores;
    uint32_t activeWaiters;
};

//
// IntelSynchronization Class
//

class IntelSynchronization : public OSObject {
    OSDeclareDefaultStructors(IntelSynchronization)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    
    bool initWithController(AppleIntelTGLController* controller);
    bool start();
    void stop();
    
    // Binary Semaphore Operations
    IntelSemaphore* createBinarySemaphore();
    SyncError signalBinarySemaphore(IntelSemaphore* semaphore);
    SyncError waitBinarySemaphore(IntelSemaphore* semaphore, 
                                  IntelRequest* request,
                                  uint64_t timeoutMs);
    
    // Counting Semaphore Operations
    IntelSemaphore* createCountingSemaphore(uint32_t maxValue);
    SyncError incrementSemaphore(IntelSemaphore* semaphore);
    SyncError decrementSemaphore(IntelSemaphore* semaphore, 
                                 IntelRequest* request,
                                 uint64_t timeoutMs);
    
    // Timeline Semaphore Operations
    IntelSemaphore* createTimelineSemaphore(uint64_t initialValue);
    SyncError signalTimeline(IntelSemaphore* semaphore, uint64_t value);
    SyncError waitTimeline(IntelSemaphore* semaphore, 
                          uint64_t value,
                          IntelRequest* request,
                          uint64_t timeoutMs);
    uint64_t getTimelineValue(IntelSemaphore* semaphore);
    
    // Cross-Engine Synchronization
    IntelEngineDependency* createDependency(IntelRequest* sourceRequest,
                                           IntelRequest* destRequest);
    SyncError waitForDependency(IntelEngineDependency* dep, 
                               uint64_t timeoutMs);
    void destroyDependency(IntelEngineDependency* dep);
    
    // Wait-for-Idle Operations
    SyncError waitForEngineIdle(IntelRingBuffer* engine, uint64_t timeoutMs);
    SyncError waitForGPUIdle(uint64_t timeoutMs);
    bool isEngineIdle(IntelRingBuffer* engine);
    bool isGPUIdle();
    
    // Memory Barriers
    void flushWriteCombineBuffer();
    void memoryBarrier();
    SyncError flushGPUCache(IntelRingBuffer* engine);
    
    // Semaphore Management
    void destroySemaphore(IntelSemaphore* semaphore);
    void resetSemaphore(IntelSemaphore* semaphore);
    bool isSemaphoreSignaled(IntelSemaphore* semaphore);
    
    // Deadlock Detection
    bool checkForDeadlock(IntelSemaphore* semaphore, IntelRequest* request);
    void detectDeadlocks();
    
    // Statistics
    void getStatistics(SyncStats* stats);
    void resetStatistics();
    void printStatistics();
    
private:
    AppleIntelTGLController* controller;
    
    // Semaphore pool
    IntelSemaphore* semaphorePool;      // Pre-allocated pool
    uint32_t poolSize;                  // Pool size (1024)
    IntelSemaphore* freeList;           // Free semaphores
    
    // Active semaphores
    IntelSemaphore* activeSemaphores;   // Linked list
    uint32_t activeSemaphoreCount;
    
    // Dependencies
    IntelEngineDependency* dependencies; // Active dependencies
    uint32_t dependencyCount;
    
    // Statistics
    SyncStats stats;
    
    // Locks
    IORecursiveLock* poolLock;
    IORecursiveLock* statsLock;
    
    // Private methods
    IntelSemaphore* allocateSemaphore();
    void freeSemaphore(IntelSemaphore* semaphore);
    bool allocateSemaphoreMemory(IntelSemaphore* semaphore);
    void freeSemaphoreMemory(IntelSemaphore* semaphore);
    
    SyncError addWaiter(IntelSemaphore* semaphore, IntelRequest* request);
    void removeWaiter(IntelSemaphore* semaphore, IntelRequest* request);
    void notifyWaiters(IntelSemaphore* semaphore);
    
    bool isTimelineValueReached(IntelSemaphore* semaphore, uint64_t value);
    IntelTimelinePoint* createTimelinePoint(uint64_t value, IntelRequest* request);
    void addTimelinePoint(IntelSemaphore* semaphore, IntelTimelinePoint* point);
    void cleanupTimelinePoints(IntelSemaphore* semaphore);
    
    IdleState checkEngineIdleState(IntelRingBuffer* engine);
    SyncError waitForIdleWithPolling(IntelRingBuffer* engine, uint64_t timeoutMs);
    
    void recordWaitTime(uint64_t durationUs);
    void updateStatistics();
};

//
// Constants
//

#define SEMAPHORE_POOL_SIZE         1024
#define MAX_WAITERS_PER_SEMAPHORE   64
#define IDLE_TIMEOUT_MS             5000    // 5 seconds
#define IDLE_POLL_INTERVAL_US       100     // 100 microseconds
#define DEADLOCK_DETECTION_INTERVAL_MS 1000 // 1 second
#define MAX_TIMELINE_POINTS         256

#endif /* IntelSynchronization_h */
