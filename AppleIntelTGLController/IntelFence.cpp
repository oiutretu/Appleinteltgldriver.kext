/*
 * IntelFence.cpp - GPU Synchronization Fence Implementation
 */

#include "IntelFence.h"
#include "linux_compat.h"
#include "linux_time.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelFence, OSObject)

IntelFence* IntelFence::create(uint32_t id)
{
    IntelFence* fence = new IntelFence;
    if (!fence) {
        return nullptr;
    }
    
    if (!fence->init()) {
        fence->release();
        return nullptr;
    }
    
    fence->fenceId = id;
    fence->signaled = false;
    fence->seqno = 0;
    fence->engineId = 0;
    fence->signalTime = 0;
    
    return fence;
}

bool IntelFence::init()
{
    if (!super::init()) {
        return false;
    }
    
    fenceLock = IOLockAlloc();
    waitLock = IOLockAlloc();
    
    if (!fenceLock || !waitLock) {
        IOLog("IntelFence: Failed to allocate locks\n");
        return false;
    }
    
    return true;
}

void IntelFence::free()
{
    if (fenceLock) {
        IOLockFree(fenceLock);
        fenceLock = nullptr;
    }
    
    if (waitLock) {
        IOLockFree(waitLock);
        waitLock = nullptr;
    }
    
    super::free();
}

bool IntelFence::wait(uint32_t timeoutMs)
{
    // Fast path: already signaled
    IOLockLock(fenceLock);
    if (signaled) {
        IOLockUnlock(fenceLock);
        return true;
    }
    IOLockUnlock(fenceLock);
    
    // Calculate deadline in nanoseconds
    u64 startTime = ktime_get_ns();
    u64 deadlineNs = startTime + (u64)timeoutMs * 1000000ULL;
    
    while (true) {
        // Check if signaled
        IOLockLock(fenceLock);
        if (signaled) {
            IOLockUnlock(fenceLock);
            return true;
        }
        IOLockUnlock(fenceLock);
        
        // Check timeout
        u64 now = ktime_get_ns();
        if (now >= deadlineNs) {
            IOLog("IntelFence: Timeout waiting for fence %u (seqno=%u)\n", fenceId, seqno);
            return false;
        }
        
        // Sleep briefly (1ms)
        msleep(1);
    }
}

void IntelFence::signal()
{
    IOLockLock(fenceLock);
    
    if (!signaled) {
        signaled = true;
        signalTime = ktime_get_ns();
        
        IOLog("IntelFence: OK  Fence %u signaled (seqno=%u, engine=%u)\n",
              fenceId, seqno, engineId);
    }
    
    IOLockUnlock(fenceLock);
}

bool IntelFence::isSignaled() const
{
    return signaled;
}

void IntelFence::reset()
{
    IOLockLock(fenceLock);
    signaled = false;
    signalTime = 0;
    IOLockUnlock(fenceLock);
}
