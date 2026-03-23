//
//  IntelSynchronization.cpp
// macOS Driver
//
//  Implementation of synchronization primitives.
//

#include "IntelSynchronization.h"
#include "IntelGEM.h"
#include "IntelGEMObject.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelSynchronization, OSObject)

//
// Initialization
//

bool IntelSynchronization::init() {
    if (!super::init()) {
        return false;
    }
    
    controller = nullptr;
    semaphorePool = nullptr;
    freeList = nullptr;
    activeSemaphores = nullptr;
    dependencies = nullptr;
    poolLock = nullptr;
    statsLock = nullptr;
    
    poolSize = SEMAPHORE_POOL_SIZE;
    activeSemaphoreCount = 0;
    dependencyCount = 0;
    
    memset(&stats, 0, sizeof(stats));
    
    return true;
}

void IntelSynchronization::free() {
    stop();
    
    if (semaphorePool) {
        for (uint32_t i = 0; i < poolSize; i++) {
            IntelSemaphore* sem = &semaphorePool[i];
            if (sem->lock) {
                IORecursiveLockFree(sem->lock);
            }
            freeSemaphoreMemory(sem);
        }
        IOFree(semaphorePool, poolSize * sizeof(IntelSemaphore));
    }
    
    if (poolLock) IORecursiveLockFree(poolLock);
    if (statsLock) IORecursiveLockFree(statsLock);
    
    super::free();
}

bool IntelSynchronization::initWithController(AppleIntelTGLController* ctrl) {
    controller = ctrl;
    return true;
}

bool IntelSynchronization::start() {
    IOLog("IntelSynchronization::start() - Initializing synchronization system\n");
    
    // Create locks
    poolLock = IORecursiveLockAlloc();
    statsLock = IORecursiveLockAlloc();
    if (!poolLock || !statsLock) {
        IOLog("IntelSynchronization::start() - Failed to allocate locks\n");
        return false;
    }
    
    // Allocate semaphore pool
    semaphorePool = (IntelSemaphore*)IOMalloc(poolSize * sizeof(IntelSemaphore));
    if (!semaphorePool) {
        IOLog("IntelSynchronization::start() - Failed to allocate pool\n");
        return false;
    }
    
    memset(semaphorePool, 0, poolSize * sizeof(IntelSemaphore));
    
    // Initialize pool
    for (uint32_t i = 0; i < poolSize; i++) {
        IntelSemaphore* sem = &semaphorePool[i];
        sem->id = i;
        sem->lock = IORecursiveLockAlloc();
        
        // Add to free list
        sem->next = freeList;
        freeList = sem;
    }
    
    IOLog("IntelSynchronization::start() - Pool created with %u semaphores\n", 
          poolSize);
    
    return true;
}

void IntelSynchronization::stop() {
    IOLog("IntelSynchronization::stop() - Shutting down\n");
    
    // Clean up active semaphores
    IORecursiveLockLock(poolLock);
    
    IntelSemaphore* sem = activeSemaphores;
    while (sem) {
        IntelSemaphore* next = sem->next;
        destroySemaphore(sem);
        sem = next;
    }
    
    // Clean up dependencies
    IntelEngineDependency* dep = dependencies;
    while (dep) {
        IntelEngineDependency* next = dep->next;
        destroyDependency(dep);
        dep = next;
    }
    
    IORecursiveLockUnlock(poolLock);
    
    printStatistics();
}

//
// Binary Semaphore Operations
//

IntelSemaphore* IntelSynchronization::createBinarySemaphore() {
    IntelSemaphore* sem = allocateSemaphore();
    if (!sem) {
        return nullptr;
    }
    
    IORecursiveLockLock(sem->lock);
    
    sem->type = SEMAPHORE_TYPE_BINARY;
    sem->state = SEMAPHORE_STATE_IDLE;
    sem->value = 0;
    sem->maxValue = 1;
    
    if (!allocateSemaphoreMemory(sem)) {
        IORecursiveLockUnlock(sem->lock);
        freeSemaphore(sem);
        return nullptr;
    }
    
    IORecursiveLockUnlock(sem->lock);
    
    IORecursiveLockLock(statsLock);
    stats.semaphoresCreated++;
    stats.activeSemaphores++;
    IORecursiveLockUnlock(statsLock);
    
    return sem;
}

SyncError IntelSynchronization::signalBinarySemaphore(IntelSemaphore* semaphore) {
    if (!semaphore) {
        return SYNC_ERROR_NULL_OBJECT;
    }
    
    IORecursiveLockLock(semaphore->lock);
    
    if (semaphore->type != SEMAPHORE_TYPE_BINARY) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_ERROR_INVALID_TYPE;
    }
    
    if (semaphore->state == SEMAPHORE_STATE_SIGNALED) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_ERROR_ALREADY_SIGNALED;
    }
    
    // Write signal value to GPU memory
    if (semaphore->cpuAddress) {
        *semaphore->cpuAddress = 1;
    }
    
    semaphore->value = 1;
    semaphore->state = SEMAPHORE_STATE_SIGNALED;
    semaphore->signalTime = mach_absolute_time();
    semaphore->signalCount++;
    
    // Notify waiters
    notifyWaiters(semaphore);
    
    IORecursiveLockUnlock(semaphore->lock);
    
    return SYNC_OK;
}

SyncError IntelSynchronization::waitBinarySemaphore(IntelSemaphore* semaphore,
                                                    IntelRequest* request,
                                                    uint64_t timeoutMs) {
    if (!semaphore || !request) {
        return SYNC_ERROR_NULL_OBJECT;
    }
    
    IORecursiveLockLock(semaphore->lock);
    
    if (semaphore->type != SEMAPHORE_TYPE_BINARY) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_ERROR_INVALID_TYPE;
    }
    
    // Check if already signaled
    if (semaphore->state == SEMAPHORE_STATE_SIGNALED) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_OK;
    }
    
    // Check for deadlock
    if (checkForDeadlock(semaphore, request)) {
        IORecursiveLockUnlock(semaphore->lock);
        IOLog("IntelSynchronization::waitBinarySemaphore() - Deadlock detected\n");
        return SYNC_ERROR_DEADLOCK;
    }
    
    semaphore->waitTime = mach_absolute_time();
    semaphore->waitCount++;
    
    // Add to waiters
    SyncError error = addWaiter(semaphore, request);
    if (error != SYNC_OK) {
        IORecursiveLockUnlock(semaphore->lock);
        return error;
    }
    
    semaphore->state = SEMAPHORE_STATE_WAITING;
    
    IORecursiveLockUnlock(semaphore->lock);
    
    // Wait for signal (in real implementation, would use hardware wait)
    uint64_t startTime = mach_absolute_time();
    uint64_t timeoutNs = timeoutMs * 1000000ULL;
    
    while (semaphore->state != SEMAPHORE_STATE_SIGNALED) {
        IOSleep(1);  // Sleep 1ms
        
        uint64_t elapsed = mach_absolute_time() - startTime;
        if (elapsed > timeoutNs) {
            IORecursiveLockLock(semaphore->lock);
            removeWaiter(semaphore, request);
            semaphore->state = SEMAPHORE_STATE_ERROR;
            IORecursiveLockUnlock(semaphore->lock);
            
            IORecursiveLockLock(statsLock);
            stats.timeouts++;
            IORecursiveLockUnlock(statsLock);
            
            return SYNC_ERROR_TIMEOUT;
        }
    }
    
    // Record wait time
    uint64_t waitTimeNs = mach_absolute_time() - startTime;
    uint64_t waitTimeUs = waitTimeNs / 1000;
    recordWaitTime(waitTimeUs);
    
    IORecursiveLockLock(statsLock);
    stats.binaryWaits++;
    IORecursiveLockUnlock(statsLock);
    
    return SYNC_OK;
}

//
// Counting Semaphore Operations
//

IntelSemaphore* IntelSynchronization::createCountingSemaphore(uint32_t maxValue) {
    IntelSemaphore* sem = allocateSemaphore();
    if (!sem) {
        return nullptr;
    }
    
    IORecursiveLockLock(sem->lock);
    
    sem->type = SEMAPHORE_TYPE_COUNTING;
    sem->state = SEMAPHORE_STATE_IDLE;
    sem->value = maxValue;  // Start at max
    sem->maxValue = maxValue;
    
    if (!allocateSemaphoreMemory(sem)) {
        IORecursiveLockUnlock(sem->lock);
        freeSemaphore(sem);
        return nullptr;
    }
    
    IORecursiveLockUnlock(sem->lock);
    
    IORecursiveLockLock(statsLock);
    stats.semaphoresCreated++;
    stats.activeSemaphores++;
    IORecursiveLockUnlock(statsLock);
    
    return sem;
}

SyncError IntelSynchronization::incrementSemaphore(IntelSemaphore* semaphore) {
    if (!semaphore) {
        return SYNC_ERROR_NULL_OBJECT;
    }
    
    IORecursiveLockLock(semaphore->lock);
    
    if (semaphore->type != SEMAPHORE_TYPE_COUNTING) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_ERROR_INVALID_TYPE;
    }
    
    if (semaphore->value >= semaphore->maxValue) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_ERROR_INVALID_VALUE;
    }
    
    semaphore->value++;
    if (semaphore->cpuAddress) {
        *semaphore->cpuAddress = (uint32_t)semaphore->value;
    }
    
    semaphore->signalCount++;
    
    // Notify one waiter
    if (semaphore->waiterCount > 0) {
        IntelRequest* request = semaphore->waiters[0];
        removeWaiter(semaphore, request);
    }
    
    IORecursiveLockUnlock(semaphore->lock);
    
    return SYNC_OK;
}

SyncError IntelSynchronization::decrementSemaphore(IntelSemaphore* semaphore,
                                                   IntelRequest* request,
                                                   uint64_t timeoutMs) {
    if (!semaphore || !request) {
        return SYNC_ERROR_NULL_OBJECT;
    }
    
    IORecursiveLockLock(semaphore->lock);
    
    if (semaphore->type != SEMAPHORE_TYPE_COUNTING) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_ERROR_INVALID_TYPE;
    }
    
    // If value > 0, decrement immediately
    if (semaphore->value > 0) {
        semaphore->value--;
        if (semaphore->cpuAddress) {
            *semaphore->cpuAddress = (uint32_t)semaphore->value;
        }
        IORecursiveLockUnlock(semaphore->lock);
        
        IORecursiveLockLock(statsLock);
        stats.countingWaits++;
        IORecursiveLockUnlock(statsLock);
        
        return SYNC_OK;
    }
    
    // Must wait for increment
    semaphore->waitTime = mach_absolute_time();
    semaphore->waitCount++;
    
    SyncError error = addWaiter(semaphore, request);
    if (error != SYNC_OK) {
        IORecursiveLockUnlock(semaphore->lock);
        return error;
    }
    
    semaphore->state = SEMAPHORE_STATE_WAITING;
    
    IORecursiveLockUnlock(semaphore->lock);
    
    // Wait for increment
    uint64_t startTime = mach_absolute_time();
    uint64_t timeoutNs = timeoutMs * 1000000ULL;
    
    while (semaphore->value == 0) {
        IOSleep(1);
        
        uint64_t elapsed = mach_absolute_time() - startTime;
        if (elapsed > timeoutNs) {
            IORecursiveLockLock(semaphore->lock);
            removeWaiter(semaphore, request);
            IORecursiveLockUnlock(semaphore->lock);
            
            IORecursiveLockLock(statsLock);
            stats.timeouts++;
            IORecursiveLockUnlock(statsLock);
            
            return SYNC_ERROR_TIMEOUT;
        }
    }
    
    uint64_t waitTimeUs = (mach_absolute_time() - startTime) / 1000;
    recordWaitTime(waitTimeUs);
    
    IORecursiveLockLock(statsLock);
    stats.countingWaits++;
    IORecursiveLockUnlock(statsLock);
    
    return SYNC_OK;
}

//
// Timeline Semaphore Operations
//

IntelSemaphore* IntelSynchronization::createTimelineSemaphore(uint64_t initialValue) {
    IntelSemaphore* sem = allocateSemaphore();
    if (!sem) {
        return nullptr;
    }
    
    IORecursiveLockLock(sem->lock);
    
    sem->type = SEMAPHORE_TYPE_TIMELINE;
    sem->state = SEMAPHORE_STATE_IDLE;
    sem->value = initialValue;
    sem->targetValue = initialValue;
    
    if (!allocateSemaphoreMemory(sem)) {
        IORecursiveLockUnlock(sem->lock);
        freeSemaphore(sem);
        return nullptr;
    }
    
    IORecursiveLockUnlock(sem->lock);
    
    IORecursiveLockLock(statsLock);
    stats.semaphoresCreated++;
    stats.activeSemaphores++;
    IORecursiveLockUnlock(statsLock);
    
    return sem;
}

SyncError IntelSynchronization::signalTimeline(IntelSemaphore* semaphore, 
                                               uint64_t value) {
    if (!semaphore) {
        return SYNC_ERROR_NULL_OBJECT;
    }
    
    IORecursiveLockLock(semaphore->lock);
    
    if (semaphore->type != SEMAPHORE_TYPE_TIMELINE) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_ERROR_INVALID_TYPE;
    }
    
    // Timeline values must be monotonic
    if (value <= semaphore->value) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_ERROR_INVALID_VALUE;
    }
    
    semaphore->value = value;
    if (semaphore->cpuAddress) {
        // Write lower 32 bits
        *semaphore->cpuAddress = (uint32_t)value;
        // Write upper 32 bits
        *(semaphore->cpuAddress + 1) = (uint32_t)(value >> 32);
    }
    
    semaphore->signalTime = mach_absolute_time();
    semaphore->signalCount++;
    
    // Wake up waiters whose target value is reached
    for (uint32_t i = 0; i < semaphore->waiterCount; ) {
        if (isTimelineValueReached(semaphore, semaphore->targetValue)) {
            removeWaiter(semaphore, semaphore->waiters[i]);
        } else {
            i++;
        }
    }
    
    if (semaphore->waiterCount == 0) {
        semaphore->state = SEMAPHORE_STATE_IDLE;
    }
    
    IORecursiveLockUnlock(semaphore->lock);
    
    return SYNC_OK;
}

SyncError IntelSynchronization::waitTimeline(IntelSemaphore* semaphore,
                                             uint64_t value,
                                             IntelRequest* request,
                                             uint64_t timeoutMs) {
    if (!semaphore || !request) {
        return SYNC_ERROR_NULL_OBJECT;
    }
    
    IORecursiveLockLock(semaphore->lock);
    
    if (semaphore->type != SEMAPHORE_TYPE_TIMELINE) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_ERROR_INVALID_TYPE;
    }
    
    // Check if value already reached
    if (semaphore->value >= value) {
        IORecursiveLockUnlock(semaphore->lock);
        return SYNC_OK;
    }
    
    semaphore->targetValue = value;
    semaphore->waitTime = mach_absolute_time();
    semaphore->waitCount++;
    
    SyncError error = addWaiter(semaphore, request);
    if (error != SYNC_OK) {
        IORecursiveLockUnlock(semaphore->lock);
        return error;
    }
    
    semaphore->state = SEMAPHORE_STATE_WAITING;
    
    IORecursiveLockUnlock(semaphore->lock);
    
    // Wait for timeline value
    uint64_t startTime = mach_absolute_time();
    uint64_t timeoutNs = timeoutMs * 1000000ULL;
    
    while (semaphore->value < value) {
        IOSleep(1);
        
        uint64_t elapsed = mach_absolute_time() - startTime;
        if (elapsed > timeoutNs) {
            IORecursiveLockLock(semaphore->lock);
            removeWaiter(semaphore, request);
            IORecursiveLockUnlock(semaphore->lock);
            
            IORecursiveLockLock(statsLock);
            stats.timeouts++;
            IORecursiveLockUnlock(statsLock);
            
            return SYNC_ERROR_TIMEOUT;
        }
    }
    
    uint64_t waitTimeUs = (mach_absolute_time() - startTime) / 1000;
    recordWaitTime(waitTimeUs);
    
    IORecursiveLockLock(statsLock);
    stats.timelineWaits++;
    IORecursiveLockUnlock(statsLock);
    
    return SYNC_OK;
}

uint64_t IntelSynchronization::getTimelineValue(IntelSemaphore* semaphore) {
    if (!semaphore || semaphore->type != SEMAPHORE_TYPE_TIMELINE) {
        return 0;
    }
    
    IORecursiveLockLock(semaphore->lock);
    uint64_t value = semaphore->value;
    IORecursiveLockUnlock(semaphore->lock);
    
    return value;
}

//
// Cross-Engine Synchronization
//

IntelEngineDependency* IntelSynchronization::createDependency(
    IntelRequest* sourceRequest,
    IntelRequest* destRequest) {
    
    if (!sourceRequest || !destRequest) {
        return nullptr;
    }
    
    IntelEngineDependency* dep = (IntelEngineDependency*)IOMalloc(sizeof(IntelEngineDependency));
    if (!dep) {
        return nullptr;
    }
    
    memset(dep, 0, sizeof(IntelEngineDependency));
    
    dep->sourceEngine = sourceRequest->getRing();
    dep->destEngine = destRequest->getRing();
    dep->sourceRequest = sourceRequest;
    dep->destRequest = destRequest;
    dep->createTime = mach_absolute_time();
    
    // Create semaphore for sync
    dep->semaphore = createBinarySemaphore();
    if (!dep->semaphore) {
        IOFree(dep, sizeof(IntelEngineDependency));
        return nullptr;
    }
    
    IORecursiveLockLock(poolLock);
    dep->next = dependencies;
    dependencies = dep;
    dependencyCount++;
    IORecursiveLockUnlock(poolLock);
    
    return dep;
}

SyncError IntelSynchronization::waitForDependency(IntelEngineDependency* dep,
                                                  uint64_t timeoutMs) {
    if (!dep) {
        return SYNC_ERROR_NULL_OBJECT;
    }
    
    // Wait for source request to complete
    if (!dep->sourceRequest->wait(timeoutMs)) {
        return SYNC_ERROR_TIMEOUT;
    }
    
    // Signal semaphore
    SyncError error = signalBinarySemaphore(dep->semaphore);
    if (error != SYNC_OK) {
        return error;
    }
    
    IORecursiveLockLock(statsLock);
    stats.crossEngineWaits++;
    IORecursiveLockUnlock(statsLock);
    
    return SYNC_OK;
}

void IntelSynchronization::destroyDependency(IntelEngineDependency* dep) {
    if (!dep) {
        return;
    }
    
    IORecursiveLockLock(poolLock);
    
    // Remove from list
    IntelEngineDependency** prev = &dependencies;
    while (*prev) {
        if (*prev == dep) {
            *prev = dep->next;
            dependencyCount--;
            break;
        }
        prev = &(*prev)->next;
    }
    
    IORecursiveLockUnlock(poolLock);
    
    // Clean up semaphore
    if (dep->semaphore) {
        destroySemaphore(dep->semaphore);
    }
    
    IOFree(dep, sizeof(IntelEngineDependency));
}

//
// Wait-for-Idle Operations
//

SyncError IntelSynchronization::waitForEngineIdle(IntelRingBuffer* engine,
                                                  uint64_t timeoutMs) {
    if (!engine) {
        return SYNC_ERROR_NULL_OBJECT;
    }
    
    return waitForIdleWithPolling(engine, timeoutMs);
}

SyncError IntelSynchronization::waitForGPUIdle(uint64_t timeoutMs) {
    // Wait for all engines to be idle
    // In real implementation, would iterate over all engines
    
    uint64_t startTime = mach_absolute_time();
    uint64_t timeoutNs = timeoutMs * 1000000ULL;
    
    while (!isGPUIdle()) {
        IOSleep(1);
        
        uint64_t elapsed = mach_absolute_time() - startTime;
        if (elapsed > timeoutNs) {
            return SYNC_ERROR_TIMEOUT;
        }
    }
    
    return SYNC_OK;
}

bool IntelSynchronization::isEngineIdle(IntelRingBuffer* engine) {
    if (!engine) {
        return true;
    }
    
    return checkEngineIdleState(engine) == IDLE_STATE_IDLE;
}

bool IntelSynchronization::isGPUIdle() {
    // Check all engines
    // In real implementation, would query hardware status
    return true;
}

//
// Memory Barriers
//

void IntelSynchronization::flushWriteCombineBuffer() {
    // x86 SFENCE instruction
    __asm__ volatile("sfence" ::: "memory");
}

void IntelSynchronization::memoryBarrier() {
    // x86 MFENCE instruction
    __asm__ volatile("mfence" ::: "memory");
}

SyncError IntelSynchronization::flushGPUCache(IntelRingBuffer* engine) {
    if (!engine) {
        return SYNC_ERROR_NULL_OBJECT;
    }
    
    // Would emit PIPE_CONTROL with cache flush bits
    // For now, just ensure CPU/GPU coherency
    memoryBarrier();
    
    return SYNC_OK;
}

//
// Semaphore Management
//

void IntelSynchronization::destroySemaphore(IntelSemaphore* semaphore) {
    if (!semaphore) {
        return;
    }
    
    IORecursiveLockLock(semaphore->lock);
    
    // Clean up waiters
    if (semaphore->waiters) {
        IOFree(semaphore->waiters, semaphore->maxWaiters * sizeof(IntelRequest*));
        semaphore->waiters = nullptr;
    }
    
    freeSemaphoreMemory(semaphore);
    
    IORecursiveLockUnlock(semaphore->lock);
    
    freeSemaphore(semaphore);
    
    IORecursiveLockLock(statsLock);
    stats.semaphoresDestroyed++;
    stats.activeSemaphores--;
    IORecursiveLockUnlock(statsLock);
}

void IntelSynchronization::resetSemaphore(IntelSemaphore* semaphore) {
    if (!semaphore) {
        return;
    }
    
    IORecursiveLockLock(semaphore->lock);
    
    semaphore->state = SEMAPHORE_STATE_IDLE;
    semaphore->value = (semaphore->type == SEMAPHORE_TYPE_COUNTING) ? 
                       semaphore->maxValue : 0;
    semaphore->waiterCount = 0;
    
    if (semaphore->cpuAddress) {
        *semaphore->cpuAddress = (uint32_t)semaphore->value;
    }
    
    IORecursiveLockUnlock(semaphore->lock);
}

bool IntelSynchronization::isSemaphoreSignaled(IntelSemaphore* semaphore) {
    if (!semaphore) {
        return false;
    }
    
    IORecursiveLockLock(semaphore->lock);
    bool signaled = (semaphore->state == SEMAPHORE_STATE_SIGNALED);
    IORecursiveLockUnlock(semaphore->lock);
    
    return signaled;
}

//
// Deadlock Detection
//

bool IntelSynchronization::checkForDeadlock(IntelSemaphore* semaphore,
                                            IntelRequest* request) {
    // Simple check: detect circular waits
    // In real implementation, would build dependency graph
    
    for (uint32_t i = 0; i < semaphore->waiterCount; i++) {
        if (semaphore->waiters[i] == request) {
            return true;  // Already waiting
        }
    }
    
    return false;
}

void IntelSynchronization::detectDeadlocks() {
    // Periodic deadlock detection
    // Would analyze dependency graph
    
    IORecursiveLockLock(poolLock);
    
    IntelSemaphore* sem = activeSemaphores;
    while (sem) {
        if (sem->state == SEMAPHORE_STATE_WAITING) {
            uint64_t waitTime = mach_absolute_time() - sem->waitTime;
            if (waitTime > 5000000000ULL) {  // 5 seconds
                IOLog("IntelSynchronization::detectDeadlocks() - "
                      "Potential deadlock on semaphore %u\n", sem->id);
                
                IORecursiveLockLock(statsLock);
                stats.deadlocksDetected++;
                IORecursiveLockUnlock(statsLock);
            }
        }
        sem = sem->next;
    }
    
    IORecursiveLockUnlock(poolLock);
}

//
// Statistics
//

void IntelSynchronization::getStatistics(SyncStats* outStats) {
    if (!outStats) {
        return;
    }
    
    IORecursiveLockLock(statsLock);
    memcpy(outStats, &stats, sizeof(SyncStats));
    IORecursiveLockUnlock(statsLock);
}

void IntelSynchronization::resetStatistics() {
    IORecursiveLockLock(statsLock);
    
    uint32_t active = stats.activeSemaphores;
    memset(&stats, 0, sizeof(stats));
    stats.activeSemaphores = active;
    
    IORecursiveLockUnlock(statsLock);
}

void IntelSynchronization::printStatistics() {
    IORecursiveLockLock(statsLock);
    
    IOLog("Semaphores created:      %llu\n", stats.semaphoresCreated);
    IOLog("Semaphores destroyed:    %llu\n", stats.semaphoresDestroyed);
    IOLog("Active semaphores:       %u\n", stats.activeSemaphores);
    IOLog("Binary waits:            %llu\n", stats.binaryWaits);
    IOLog("Counting waits:          %llu\n", stats.countingWaits);
    IOLog("Timeline waits:          %llu\n", stats.timelineWaits);
    IOLog("Cross-engine waits:      %llu\n", stats.crossEngineWaits);
    IOLog("Timeouts:                %llu\n", stats.timeouts);
    IOLog("Deadlocks detected:      %llu\n", stats.deadlocksDetected);
    IOLog("Average wait time:       %llu uss\n", stats.averageWaitTimeUs);
    IOLog("Max wait time:           %llu uss\n", stats.maxWaitTimeUs);
    IOLog("Active waiters:          %u\n", stats.activeWaiters);
    
    IORecursiveLockUnlock(statsLock);
}

//
// Private Methods
//

IntelSemaphore* IntelSynchronization::allocateSemaphore() {
    IORecursiveLockLock(poolLock);
    
    IntelSemaphore* sem = freeList;
    if (!sem) {
        IORecursiveLockUnlock(poolLock);
        IOLog("IntelSynchronization::allocateSemaphore() - Pool exhausted\n");
        return nullptr;
    }
    
    freeList = sem->next;
    
    // Add to active list
    sem->next = activeSemaphores;
    activeSemaphores = sem;
    activeSemaphoreCount++;
    
    IORecursiveLockUnlock(poolLock);
    
    // Initialize
    memset(sem, 0, sizeof(IntelSemaphore));
    sem->lock = IORecursiveLockAlloc();
    
    return sem;
}

void IntelSynchronization::freeSemaphore(IntelSemaphore* semaphore) {
    if (!semaphore) {
        return;
    }
    
    IORecursiveLockLock(poolLock);
    
    // Remove from active list
    IntelSemaphore** prev = &activeSemaphores;
    while (*prev) {
        if (*prev == semaphore) {
            *prev = semaphore->next;
            activeSemaphoreCount--;
            break;
        }
        prev = &(*prev)->next;
    }
    
    // Add to free list
    semaphore->next = freeList;
    freeList = semaphore;
    
    IORecursiveLockUnlock(poolLock);
}

bool IntelSynchronization::allocateSemaphoreMemory(IntelSemaphore* semaphore) {
    // Allocate GPU-visible memory for semaphore
    // In real implementation, would use GEM object
    
    semaphore->cpuAddress = (uint32_t*)IOMalloc(4096);
    if (!semaphore->cpuAddress) {
        return false;
    }
    
    memset(semaphore->cpuAddress, 0, 4096);
    semaphore->gpuAddress = 0;  // Would get from GEM
    
    // Allocate waiter array
    semaphore->maxWaiters = MAX_WAITERS_PER_SEMAPHORE;
    semaphore->waiters = (IntelRequest**)IOMalloc(
        semaphore->maxWaiters * sizeof(IntelRequest*));
    if (!semaphore->waiters) {
        IOFree(semaphore->cpuAddress, 4096);
        return false;
    }
    
    memset(semaphore->waiters, 0, semaphore->maxWaiters * sizeof(IntelRequest*));
    
    return true;
}

void IntelSynchronization::freeSemaphoreMemory(IntelSemaphore* semaphore) {
    if (!semaphore) {
        return;
    }
    
    if (semaphore->cpuAddress) {
        IOFree(semaphore->cpuAddress, 4096);
        semaphore->cpuAddress = nullptr;
    }
    
    if (semaphore->waiters) {
        IOFree(semaphore->waiters, semaphore->maxWaiters * sizeof(IntelRequest*));
        semaphore->waiters = nullptr;
    }
}

SyncError IntelSynchronization::addWaiter(IntelSemaphore* semaphore,
                                          IntelRequest* request) {
    if (semaphore->waiterCount >= semaphore->maxWaiters) {
        return SYNC_ERROR_OUT_OF_MEMORY;
    }
    
    semaphore->waiters[semaphore->waiterCount++] = request;
    
    IORecursiveLockLock(statsLock);
    stats.activeWaiters++;
    IORecursiveLockUnlock(statsLock);
    
    return SYNC_OK;
}

void IntelSynchronization::removeWaiter(IntelSemaphore* semaphore,
                                        IntelRequest* request) {
    for (uint32_t i = 0; i < semaphore->waiterCount; i++) {
        if (semaphore->waiters[i] == request) {
            // Shift remaining waiters
            for (uint32_t j = i; j < semaphore->waiterCount - 1; j++) {
                semaphore->waiters[j] = semaphore->waiters[j + 1];
            }
            semaphore->waiterCount--;
            
            IORecursiveLockLock(statsLock);
            stats.activeWaiters--;
            IORecursiveLockUnlock(statsLock);
            
            break;
        }
    }
}

void IntelSynchronization::notifyWaiters(IntelSemaphore* semaphore) {
    // Wake up all waiters (for binary semaphore)
    semaphore->waiterCount = 0;
    
    IORecursiveLockLock(statsLock);
    stats.activeWaiters = 0;
    IORecursiveLockUnlock(statsLock);
}

bool IntelSynchronization::isTimelineValueReached(IntelSemaphore* semaphore,
                                                  uint64_t value) {
    return semaphore->value >= value;
}

IdleState IntelSynchronization::checkEngineIdleState(IntelRingBuffer* engine) {
    // Check if engine has pending work
    // In real implementation, would read hardware registers
    
    return IDLE_STATE_IDLE;
}

SyncError IntelSynchronization::waitForIdleWithPolling(IntelRingBuffer* engine,
                                                       uint64_t timeoutMs) {
    uint64_t startTime = mach_absolute_time();
    uint64_t timeoutNs = timeoutMs * 1000000ULL;
    
    while (!isEngineIdle(engine)) {
        IODelay(IDLE_POLL_INTERVAL_US);
        
        uint64_t elapsed = mach_absolute_time() - startTime;
        if (elapsed > timeoutNs) {
            return SYNC_ERROR_TIMEOUT;
        }
    }
    
    return SYNC_OK;
}

void IntelSynchronization::recordWaitTime(uint64_t durationUs) {
    IORecursiveLockLock(statsLock);
    
    // Update average
    uint64_t totalWaits = stats.binaryWaits + stats.countingWaits + 
                         stats.timelineWaits;
    if (totalWaits > 0) {
        stats.averageWaitTimeUs = 
            ((stats.averageWaitTimeUs * (totalWaits - 1)) + durationUs) / totalWaits;
    }
    
    // Update max
    if (durationUs > stats.maxWaitTimeUs) {
        stats.maxWaitTimeUs = durationUs;
    }
    
    IORecursiveLockUnlock(statsLock);
}

void IntelSynchronization::updateStatistics() {
    IORecursiveLockLock(statsLock);
    
    stats.activeSemaphores = activeSemaphoreCount;
    
    uint32_t totalWaiters = 0;
    IntelSemaphore* sem = activeSemaphores;
    while (sem) {
        totalWaiters += sem->waiterCount;
        sem = sem->next;
    }
    stats.activeWaiters = totalWaiters;
    
    IORecursiveLockUnlock(statsLock);
}
