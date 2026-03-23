//
//  IntelRequestOptimizer.cpp
// macOS Driver
//
//  Implementation of request optimization and load balancing.
//

#include "IntelRequestOptimizer.h"
#include "IntelRequest.h"
#include "IntelGEMObject.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelRequestOptimizer, OSObject)

//
// Initialization
//

bool IntelRequestOptimizer::init() {
    if (!super::init()) {
        return false;
    }
    
    controller = nullptr;
    requestManager = nullptr;
    
    strategy = OPTIMIZATION_BALANCED;
    coalescingPolicy = COALESCE_AUTO;
    preemptionLevel = PREEMPTION_BATCH;
    agingThresholdMs = AGING_THRESHOLD_MS;
    
    activeCoalesced = nullptr;
    maxCoalesceSize = MAX_COALESCE_SIZE;
    maxCoalesceDelayMs = MAX_COALESCE_DELAY_MS;
    coalesceTimer = nullptr;
    
    engineLoads = nullptr;
    engineCount = 0;
    
    preemptionContexts = nullptr;
    activePreemptions = 0;
    
    latencyHistory = nullptr;
    latencyHistorySize = LATENCY_HISTORY_SIZE;
    latencyHistoryIndex = 0;
    
    optimizerLock = nullptr;
    statsLock = nullptr;
    
    memset(&stats, 0, sizeof(stats));
    
    return true;
}

void IntelRequestOptimizer::free() {
    stop();
    
    if (latencyHistory) {
        IOFree(latencyHistory, latencyHistorySize * sizeof(uint64_t));
    }
    
    if (engineLoads) {
        IOFree(engineLoads, engineCount * sizeof(EngineLoad));
    }
    
    if (optimizerLock) IORecursiveLockFree(optimizerLock);
    if (statsLock) IORecursiveLockFree(statsLock);
    
    super::free();
}

bool IntelRequestOptimizer::initWithController(AppleIntelTGLController* ctrl) {
    controller = ctrl;
    return true;
}

bool IntelRequestOptimizer::start() {
    IOLog("IntelRequestOptimizer::start() - Initializing optimizer\n");
    
    // Create locks
    optimizerLock = IORecursiveLockAlloc();
    statsLock = IORecursiveLockAlloc();
    if (!optimizerLock || !statsLock) {
        IOLog("IntelRequestOptimizer::start() - Failed to allocate locks\n");
        return false;
    }
    
    // Allocate latency history
    latencyHistory = (uint64_t*)IOMalloc(latencyHistorySize * sizeof(uint64_t));
    if (!latencyHistory) {
        IOLog("IntelRequestOptimizer::start() - Failed to allocate history\n");
        return false;
    }
    memset(latencyHistory, 0, latencyHistorySize * sizeof(uint64_t));
    
    // Allocate engine load tracking (assume 5 engines: RCS/BCS/VCS0/VCS1/VECS)
    engineCount = 5;
    engineLoads = (EngineLoad*)IOMalloc(engineCount * sizeof(EngineLoad));
    if (!engineLoads) {
        IOLog("IntelRequestOptimizer::start() - Failed to allocate engine loads\n");
        return false;
    }
    memset(engineLoads, 0, engineCount * sizeof(EngineLoad));
    
    IOLog("IntelRequestOptimizer::start() - Optimizer initialized\n");
    IOLog("  Strategy: %s\n", 
          strategy == OPTIMIZATION_THROUGHPUT ? "Throughput" :
          strategy == OPTIMIZATION_LATENCY ? "Latency" :
          strategy == OPTIMIZATION_POWER ? "Power" : "Balanced");
    IOLog("  Coalescing: %s\n",
          coalescingPolicy == COALESCE_NONE ? "Disabled" :
          coalescingPolicy == COALESCE_SMALL ? "Small" :
          coalescingPolicy == COALESCE_AGGRESSIVE ? "Aggressive" : "Auto");
    IOLog("  Preemption: %s\n",
          preemptionLevel == PREEMPTION_DISABLED ? "Disabled" :
          preemptionLevel == PREEMPTION_BATCH ? "Batch" :
          preemptionLevel == PREEMPTION_COMMAND ? "Command" : "Context");
    
    return true;
}

void IntelRequestOptimizer::stop() {
    IOLog("IntelRequestOptimizer::stop() - Shutting down\n");
    
    // Flush any pending coalesced requests
    IORecursiveLockLock(optimizerLock);
    flushCoalescedRequests();
    IORecursiveLockUnlock(optimizerLock);
    
    printStatistics();
}

//
// Configuration
//

void IntelRequestOptimizer::setOptimizationStrategy(OptimizationStrategy newStrategy) {
    IORecursiveLockLock(optimizerLock);
    strategy = newStrategy;
    IORecursiveLockUnlock(optimizerLock);
    
    IOLog("IntelRequestOptimizer: Strategy set to %d\n", newStrategy);
}

void IntelRequestOptimizer::setCoalescingPolicy(CoalescingPolicy policy) {
    IORecursiveLockLock(optimizerLock);
    coalescingPolicy = policy;
    IORecursiveLockUnlock(optimizerLock);
    
    IOLog("IntelRequestOptimizer: Coalescing policy set to %d\n", policy);
}

void IntelRequestOptimizer::setPreemptionLevel(PreemptionLevel level) {
    IORecursiveLockLock(optimizerLock);
    preemptionLevel = level;
    IORecursiveLockUnlock(optimizerLock);
    
    IOLog("IntelRequestOptimizer: Preemption level set to %d\n", level);
}

void IntelRequestOptimizer::setAgingThreshold(uint64_t thresholdMs) {
    IORecursiveLockLock(optimizerLock);
    agingThresholdMs = thresholdMs;
    IORecursiveLockUnlock(optimizerLock);
}

//
// Request Coalescing
//

bool IntelRequestOptimizer::shouldCoalesce(IntelRequest* request) {
    if (!request) {
        return false;
    }
    
    if (coalescingPolicy == COALESCE_NONE) {
        return false;
    }
    
    if (coalescingPolicy == COALESCE_AGGRESSIVE) {
        return true;
    }
    
    if (coalescingPolicy == COALESCE_SMALL) {
        // Only coalesce small batches (< 4KB)
        if (request->getBatchBuffer()) {
            uint32_t size = request->getBatchBuffer()->getSize();
            return size < 4096;
        }
        return false;
    }
    
    // COALESCE_AUTO: decide based on load
    IntelRingBuffer* ring = request->getRing();
    if (!ring) {
        return false;
    }
    
    // Get engine load
    for (uint32_t i = 0; i < engineCount; i++) {
        if (engineLoads[i].engine == ring) {
            // Coalesce if queue depth is low and utilization < 50%
            return (engineLoads[i].queueDepth < 4 && 
                   engineLoads[i].utilizationPercent < 50);
        }
    }
    
    return false;
}

CoalescedRequest* IntelRequestOptimizer::createCoalescedRequest() {
    CoalescedRequest* coalesced = (CoalescedRequest*)IOMalloc(sizeof(CoalescedRequest));
    if (!coalesced) {
        return nullptr;
    }
    
    memset(coalesced, 0, sizeof(CoalescedRequest));
    
    coalesced->maxRequests = maxCoalesceSize;
    coalesced->requests = (IntelRequest**)IOMalloc(
        maxCoalesceSize * sizeof(IntelRequest*));
    if (!coalesced->requests) {
        IOFree(coalesced, sizeof(CoalescedRequest));
        return nullptr;
    }
    
    memset(coalesced->requests, 0, maxCoalesceSize * sizeof(IntelRequest*));
    coalesced->createTime = mach_absolute_time();
    coalesced->priority = REQUEST_PRIORITY_LOW;
    
    return coalesced;
}

bool IntelRequestOptimizer::addToCoalescedRequest(CoalescedRequest* coalesced,
                                                   IntelRequest* request) {
    if (!coalesced || !request) {
        return false;
    }
    
    if (coalesced->requestCount >= coalesced->maxRequests) {
        return false;
    }
    
    // Check compatibility
    if (coalesced->requestCount > 0) {
        if (!canCoalesceRequests(coalesced->requests[0], request)) {
            return false;
        }
    }
    
    coalesced->requests[coalesced->requestCount++] = request;
    
    if (request->getBatchBuffer()) {
        coalesced->totalSize += request->getBatchBuffer()->getSize();
    }
    
    // Track target engine and highest priority
    if (!coalesced->targetEngine) {
        coalesced->targetEngine = request->getRing();
    }
    
    IntelRequestPriority reqPriority = request->getPriority();
    if (reqPriority > coalesced->priority) {
        coalesced->priority = reqPriority;
    }
    
    return true;
}

bool IntelRequestOptimizer::submitCoalescedRequest(CoalescedRequest* coalesced) {
    if (!coalesced || coalesced->requestCount == 0) {
        return false;
    }
    
    IOLog("IntelRequestOptimizer: Submitting coalesced request with %u requests\n",
          coalesced->requestCount);
    
    // Submit all requests in the coalesced batch
    for (uint32_t i = 0; i < coalesced->requestCount; i++) {
        IntelRequest* req = coalesced->requests[i];
        if (req && requestManager) {
            requestManager->submitRequest(req);
        }
    }
    
    IORecursiveLockLock(statsLock);
    stats.coalescedRequests += coalesced->requestCount;
    stats.coalescesSaved += (coalesced->requestCount - 1);
    stats.averageCoalesceSize = 
        (stats.averageCoalesceSize + coalesced->requestCount) / 2;
    IORecursiveLockUnlock(statsLock);
    
    return true;
}

void IntelRequestOptimizer::destroyCoalescedRequest(CoalescedRequest* coalesced) {
    if (!coalesced) {
        return;
    }
    
    if (coalesced->requests) {
        IOFree(coalesced->requests, coalesced->maxRequests * sizeof(IntelRequest*));
    }
    
    IOFree(coalesced, sizeof(CoalescedRequest));
}

//
// Priority Optimization
//

void IntelRequestOptimizer::adjustPriority(IntelRequest* request) {
    if (!request) {
        return;
    }
    
    IntelRequestPriority newPriority = calculateDynamicPriority(request);
    IntelRequestPriority currentPriority = request->getPriority();
    
    if (newPriority != currentPriority) {
        request->setPriority(newPriority);
        
        IORecursiveLockLock(statsLock);
        if (newPriority > currentPriority) {
            stats.priorityBumps++;
        } else {
            stats.priorityDrops++;
        }
        IORecursiveLockUnlock(statsLock);
    }
}

void IntelRequestOptimizer::applyAging(IntelRequest* request) {
    if (!request) {
        return;
    }
    
    uint32_t waitTime = calculateWaitTime(request);
    
    if (waitTime > agingThresholdMs) {
        // Age the request - bump priority
        bumpPriority(request);
        
        IORecursiveLockLock(statsLock);
        stats.agingAdjustments++;
        IORecursiveLockUnlock(statsLock);
    }
}

IntelRequestPriority IntelRequestOptimizer::calculateDynamicPriority(
    IntelRequest* request) {
    
    if (!request) {
        return REQUEST_PRIORITY_NORMAL;
    }
    
    IntelRequestPriority basePriority = request->getPriority();
    uint32_t waitTime = calculateWaitTime(request);
    
    // Apply aging
    if (waitTime > STARVATION_THRESHOLD_MS) {
        return REQUEST_PRIORITY_REALTIME;  // Prevent starvation
    } else if (waitTime > agingThresholdMs * 2) {
        return REQUEST_PRIORITY_HIGH;
    } else if (waitTime > agingThresholdMs) {
        return (IntelRequestPriority)((int)basePriority + 1);
    }
    
    return basePriority;
}

void IntelRequestOptimizer::preventStarvation(IntelRequestQueue* queue) {
    if (!queue) {
        return;
    }
    
    // Scan queue for starving requests
    // In real implementation, would iterate through queue
    // and bump priority of old requests
}

//
// Preemption
//

bool IntelRequestOptimizer::canPreempt(IntelRequest* current, 
                                       IntelRequest* newRequest) {
    if (!current || !newRequest) {
        return false;
    }
    
    if (preemptionLevel == PREEMPTION_DISABLED) {
        return false;
    }
    
    // Check priority difference
    IntelRequestPriority currentPri = current->getPriority();
    IntelRequestPriority newPri = newRequest->getPriority();
    
    if (newPri <= currentPri) {
        return false;  // New request not higher priority
    }
    
    // Check if preemption is worthwhile
    return isPreemptionWorthwhile(current, newRequest);
}

bool IntelRequestOptimizer::preemptRequest(IntelRequest* current,
                                           IntelRequest* newRequest) {
    if (!canPreempt(current, newRequest)) {
        return false;
    }
    
    IOLog("IntelRequestOptimizer: Preempting request\n");
    
    // Create preemption context
    PreemptionContext* ctx = (PreemptionContext*)IOMalloc(sizeof(PreemptionContext));
    if (!ctx) {
        return false;
    }
    
    memset(ctx, 0, sizeof(PreemptionContext));
    ctx->preemptedRequest = current;
    ctx->preemptingRequest = newRequest;
    ctx->preemptTime = mach_absolute_time();
    
    // Save GPU state
    if (!saveGPUState(current, &ctx->savedState)) {
        IOFree(ctx, sizeof(PreemptionContext));
        IORecursiveLockLock(statsLock);
        stats.preemptionFailures++;
        IORecursiveLockUnlock(statsLock);
        return false;
    }
    
    // Add to preemption list
    IORecursiveLockLock(optimizerLock);
    ctx->next = preemptionContexts;
    preemptionContexts = ctx;
    activePreemptions++;
    IORecursiveLockUnlock(optimizerLock);
    
    IORecursiveLockLock(statsLock);
    stats.preemptions++;
    IORecursiveLockUnlock(statsLock);
    
    return true;
}

bool IntelRequestOptimizer::restorePreemptedRequest(PreemptionContext* ctx) {
    if (!ctx) {
        return false;
    }
    
    // Restore GPU state
    if (!restoreGPUState(ctx->preemptedRequest, ctx->savedState)) {
        return false;
    }
    
    // Remove from preemption list
    IORecursiveLockLock(optimizerLock);
    
    PreemptionContext** prev = &preemptionContexts;
    while (*prev) {
        if (*prev == ctx) {
            *prev = ctx->next;
            activePreemptions--;
            break;
        }
        prev = &(*prev)->next;
    }
    
    IORecursiveLockUnlock(optimizerLock);
    
    // Free saved state
    if (ctx->savedState) {
        IOFree(ctx->savedState, 4096);  // Assume 4KB state
    }
    
    IOFree(ctx, sizeof(PreemptionContext));
    
    IORecursiveLockLock(statsLock);
    stats.preemptionRestores++;
    IORecursiveLockUnlock(statsLock);
    
    return true;
}

void IntelRequestOptimizer::abortPreemption(PreemptionContext* ctx) {
    if (!ctx) {
        return;
    }
    
    IOLog("IntelRequestOptimizer: Aborting preemption\n");
    
    // Clean up without restoring
    IORecursiveLockLock(optimizerLock);
    
    PreemptionContext** prev = &preemptionContexts;
    while (*prev) {
        if (*prev == ctx) {
            *prev = ctx->next;
            activePreemptions--;
            break;
        }
        prev = &(*prev)->next;
    }
    
    IORecursiveLockUnlock(optimizerLock);
    
    if (ctx->savedState) {
        IOFree(ctx->savedState, 4096);
    }
    
    IOFree(ctx, sizeof(PreemptionContext));
}

//
// Load Balancing
//

IntelRingBuffer* IntelRequestOptimizer::selectOptimalEngine(IntelRequest* request) {
    if (!request) {
        return nullptr;
    }
    
    // Get compatible engines for this request
    // For simplicity, assume RCS is always compatible
    uint32_t engineMask = 0x1F;  // All 5 engines
    
    return findLeastLoadedEngine(engineMask);
}

void IntelRequestOptimizer::updateEngineLoad(IntelRingBuffer* engine) {
    if (!engine) {
        return;
    }
    
    IORecursiveLockLock(optimizerLock);
    
    for (uint32_t i = 0; i < engineCount; i++) {
        if (engineLoads[i].engine == engine) {
            engineLoads[i].queueDepth = calculateEngineLoad(engine);
            engineLoads[i].utilizationPercent = 
                (engineLoads[i].queueDepth * 100) / 256;  // Assume max 256
            engineLoads[i].lastSubmitTime = mach_absolute_time();
            engineLoads[i].isIdle = (engineLoads[i].queueDepth == 0);
            break;
        }
    }
    
    IORecursiveLockUnlock(optimizerLock);
}

bool IntelRequestOptimizer::shouldMigrateRequest(IntelRequest* request,
                                                 IntelRingBuffer* currentEngine) {
    if (!request || !currentEngine) {
        return false;
    }
    
    // Check if current engine is overloaded
    if (!isEngineOverloaded(currentEngine)) {
        return false;
    }
    
    // Find alternative engine
    IntelRingBuffer* alternative = selectOptimalEngine(request);
    if (!alternative || alternative == currentEngine) {
        return false;
    }
    
    // Check if alternative is significantly less loaded
    uint32_t currentLoad = calculateEngineLoad(currentEngine);
    uint32_t altLoad = calculateEngineLoad(alternative);
    
    return (altLoad < currentLoad / 2);  // 50% less loaded
}

IntelRingBuffer* IntelRequestOptimizer::findLeastLoadedEngine(uint32_t engineMask) {
    IntelRingBuffer* leastLoaded = nullptr;
    uint32_t minLoad = UINT32_MAX;
    
    IORecursiveLockLock(optimizerLock);
    
    for (uint32_t i = 0; i < engineCount; i++) {
        if (!(engineMask & (1 << i))) {
            continue;
        }
        
        if (engineLoads[i].queueDepth < minLoad) {
            minLoad = engineLoads[i].queueDepth;
            leastLoaded = engineLoads[i].engine;
        }
    }
    
    IORecursiveLockUnlock(optimizerLock);
    
    return leastLoaded;
}

//
// Performance Tuning
//

void IntelRequestOptimizer::optimizeForThroughput() {
    IOLog("IntelRequestOptimizer: Optimizing for throughput\n");
    
    setOptimizationStrategy(OPTIMIZATION_THROUGHPUT);
    setCoalescingPolicy(COALESCE_AGGRESSIVE);
    maxCoalesceSize = 32;
    maxCoalesceDelayMs = 10;
}

void IntelRequestOptimizer::optimizeForLatency() {
    IOLog("IntelRequestOptimizer: Optimizing for latency\n");
    
    setOptimizationStrategy(OPTIMIZATION_LATENCY);
    setCoalescingPolicy(COALESCE_NONE);
    setPreemptionLevel(PREEMPTION_COMMAND);
}

void IntelRequestOptimizer::optimizeForPower() {
    IOLog("IntelRequestOptimizer: Optimizing for power\n");
    
    setOptimizationStrategy(OPTIMIZATION_POWER);
    setCoalescingPolicy(COALESCE_AGGRESSIVE);
    maxCoalesceDelayMs = 20;  // Allow longer delays
}

void IntelRequestOptimizer::autoTune() {
    // Analyze current performance and adjust
    uint64_t throughput = calculateThroughput();
    uint64_t avgLatency = stats.averageLatencyUs;
    
    if (avgLatency > 10000) {  // > 10ms average latency
        optimizeForLatency();
    } else if (throughput < 1000) {  // < 1000 req/s
        optimizeForThroughput();
    } else {
        // Balanced
        setOptimizationStrategy(OPTIMIZATION_BALANCED);
        setCoalescingPolicy(COALESCE_AUTO);
    }
}

//
// Batch Optimization
//

uint32_t IntelRequestOptimizer::calculateOptimalBatchSize(IntelRingBuffer* engine) {
    if (!engine) {
        return 4096;  // Default 4KB
    }
    
    // Based on strategy
    switch (strategy) {
        case OPTIMIZATION_THROUGHPUT:
            return 16384;  // 16KB - larger batches
        case OPTIMIZATION_LATENCY:
            return 2048;   // 2KB - smaller batches
        case OPTIMIZATION_POWER:
            return 8192;   // 8KB - medium batches
        default:
            return 4096;   // 4KB - balanced
    }
}

bool IntelRequestOptimizer::shouldSplitBatch(IntelRequest* request) {
    if (!request || !request->getBatchBuffer()) {
        return false;
    }
    
    uint32_t size = request->getBatchBuffer()->getSize();
    uint32_t optimal = calculateOptimalBatchSize(request->getRing());
    
    // Split if batch is > 4x optimal size
    return (size > optimal * 4);
}

bool IntelRequestOptimizer::shouldMergeBatches(IntelRequest* req1,
                                               IntelRequest* req2) {
    if (!req1 || !req2) {
        return false;
    }
    
    return canCoalesceRequests(req1, req2);
}

//
// Statistics
//

void IntelRequestOptimizer::getStatistics(OptimizerStats* outStats) {
    if (!outStats) {
        return;
    }
    
    IORecursiveLockLock(statsLock);
    memcpy(outStats, &stats, sizeof(OptimizerStats));
    IORecursiveLockUnlock(statsLock);
}

void IntelRequestOptimizer::resetStatistics() {
    IORecursiveLockLock(statsLock);
    memset(&stats, 0, sizeof(stats));
    IORecursiveLockUnlock(statsLock);
}

void IntelRequestOptimizer::printStatistics() {
    IORecursiveLockLock(statsLock);
    
    IOLog("Coalescing:\n");
    IOLog("  Coalesced requests:      %llu\n", stats.coalescedRequests);
    IOLog("  Submissions saved:       %llu\n", stats.coalescesSaved);
    IOLog("  Average coalesce size:   %llu\n", stats.averageCoalesceSize);
    IOLog("\n");
    IOLog("Priority Optimization:\n");
    IOLog("  Priority bumps:          %llu\n", stats.priorityBumps);
    IOLog("  Priority drops:          %llu\n", stats.priorityDrops);
    IOLog("  Aging adjustments:       %llu\n", stats.agingAdjustments);
    IOLog("\n");
    IOLog("Preemption:\n");
    IOLog("  Preemptions:             %llu\n", stats.preemptions);
    IOLog("  Restores:                %llu\n", stats.preemptionRestores);
    IOLog("  Failures:                %llu\n", stats.preemptionFailures);
    IOLog("\n");
    IOLog("Load Balancing:\n");
    IOLog("  Balance events:          %llu\n", stats.loadBalanceEvents);
    IOLog("  Engine migrations:       %llu\n", stats.engineMigrations);
    IOLog("\n");
    IOLog("Performance:\n");
    IOLog("  Throughput:              %llu req/s\n", stats.throughputReqsPerSec);
    IOLog("  Average latency:         %llu uss\n", stats.averageLatencyUs);
    IOLog("  P99 latency:             %llu uss\n", stats.p99LatencyUs);
    
    IORecursiveLockUnlock(statsLock);
}

//
// Monitoring
//

void IntelRequestOptimizer::updatePerformanceMetrics() {
    stats.throughputReqsPerSec = calculateThroughput();
    stats.p99LatencyUs = calculateP99Latency();
}

uint64_t IntelRequestOptimizer::calculateThroughput() {
    // Calculate requests per second
    // In real implementation, would track submission rate
    return stats.coalescedRequests + stats.priorityBumps;  // Placeholder
}

uint64_t IntelRequestOptimizer::calculateP99Latency() {
    return calculatePercentile(99);
}

//
// Private Methods - Coalescing
//

bool IntelRequestOptimizer::canCoalesceRequests(IntelRequest* req1,
                                                IntelRequest* req2) {
    if (!req1 || !req2) {
        return false;
    }
    
    // Must use same engine
    if (req1->getRing() != req2->getRing()) {
        return false;
    }
    
    // Must use same context
    if (req1->getContext() != req2->getContext()) {
        return false;
    }
    
    // Check total size doesn't exceed limit
    uint32_t totalSize = 0;
    if (req1->getBatchBuffer()) totalSize += req1->getBatchBuffer()->getSize();
    if (req2->getBatchBuffer()) totalSize += req2->getBatchBuffer()->getSize();
    
    return (totalSize < 65536);  // Max 64KB combined
}

uint32_t IntelRequestOptimizer::estimateCoalesceBenefit(
    CoalescedRequest* coalesced) {
    
    if (!coalesced) {
        return 0;
    }
    
    // Benefit = (number of requests - 1) * submission overhead
    // Assume 100uss per submission
    return (coalesced->requestCount - 1) * 100;
}

void IntelRequestOptimizer::flushCoalescedRequests() {
    CoalescedRequest* coalesced = activeCoalesced;
    while (coalesced) {
        CoalescedRequest* next = coalesced->next;
        submitCoalescedRequest(coalesced);
        destroyCoalescedRequest(coalesced);
        coalesced = next;
    }
    activeCoalesced = nullptr;
}

void IntelRequestOptimizer::coalesceTimerFired(OSObject* owner,
                                                IOTimerEventSource* sender) {
    IntelRequestOptimizer* optimizer = (IntelRequestOptimizer*)owner;
    if (optimizer) {
        optimizer->flushCoalescedRequests();
    }
}

//
// Private Methods - Priority
//

uint32_t IntelRequestOptimizer::calculateWaitTime(IntelRequest* request) {
    if (!request) {
        return 0;
    }
    
    // Calculate time since allocation
    // In real implementation, would track allocation time
    return 50;  // Placeholder: 50ms
}

bool IntelRequestOptimizer::isStarving(IntelRequest* request) {
    return calculateWaitTime(request) > STARVATION_THRESHOLD_MS;
}

void IntelRequestOptimizer::bumpPriority(IntelRequest* request) {
    if (!request) {
        return;
    }
    
    IntelRequestPriority current = request->getPriority();
    if (current < REQUEST_PRIORITY_REALTIME) {
        request->setPriority((IntelRequestPriority)(current + 1));
    }
}

void IntelRequestOptimizer::dropPriority(IntelRequest* request) {
    if (!request) {
        return;
    }
    
    IntelRequestPriority current = request->getPriority();
    if (current > REQUEST_PRIORITY_LOW) {
        request->setPriority((IntelRequestPriority)(current - 1));
    }
}

//
// Private Methods - Preemption
//

bool IntelRequestOptimizer::saveGPUState(IntelRequest* request,
                                         void** savedState) {
    if (!request || !savedState) {
        return false;
    }
    
    // Allocate state buffer
    *savedState = IOMalloc(4096);  // Assume 4KB state
    if (!*savedState) {
        return false;
    }
    
    // In real implementation, would save actual GPU registers/context
    memset(*savedState, 0, 4096);
    
    return true;
}

bool IntelRequestOptimizer::restoreGPUState(IntelRequest* request,
                                            void* savedState) {
    if (!request || !savedState) {
        return false;
    }
    
    // In real implementation, would restore GPU registers/context
    
    return true;
}

uint32_t IntelRequestOptimizer::estimatePreemptionCost(IntelRequest* request) {
    // Estimate cost in microseconds
    // Includes: save state + context switch + restore state
    return 1000;  // ~1ms
}

bool IntelRequestOptimizer::isPreemptionWorthwhile(IntelRequest* current,
                                                   IntelRequest* newRequest) {
    uint32_t cost = estimatePreemptionCost(current);
    
    // Only preempt if priority difference is significant
    // and estimated savings > cost
    IntelRequestPriority priDiff = 
        (IntelRequestPriority)(newRequest->getPriority() - current->getPriority());
    
    return (priDiff >= 2 && cost < PREEMPTION_COST_THRESHOLD);
}

//
// Private Methods - Load Balancing
//

uint32_t IntelRequestOptimizer::calculateEngineLoad(IntelRingBuffer* engine) {
    if (!engine) {
        return 0;
    }
    
    // In real implementation, would query actual queue depth
    return 0;  // Placeholder
}

bool IntelRequestOptimizer::isEngineOverloaded(IntelRingBuffer* engine) {
    for (uint32_t i = 0; i < engineCount; i++) {
        if (engineLoads[i].engine == engine) {
            return engineLoads[i].utilizationPercent > ENGINE_OVERLOAD_THRESHOLD;
        }
    }
    return false;
}

bool IntelRequestOptimizer::isEngineUnderloaded(IntelRingBuffer* engine) {
    for (uint32_t i = 0; i < engineCount; i++) {
        if (engineLoads[i].engine == engine) {
            return engineLoads[i].utilizationPercent < ENGINE_UNDERLOAD_THRESHOLD;
        }
    }
    return false;
}

void IntelRequestOptimizer::redistributeLoad() {
    // Redistribute work from overloaded to underloaded engines
    IORecursiveLockLock(statsLock);
    stats.loadBalanceEvents++;
    IORecursiveLockUnlock(statsLock);
}

//
// Private Methods - Statistics
//

void IntelRequestOptimizer::recordLatency(uint64_t latencyUs) {
    IORecursiveLockLock(optimizerLock);
    
    latencyHistory[latencyHistoryIndex] = latencyUs;
    latencyHistoryIndex = (latencyHistoryIndex + 1) % latencyHistorySize;
    
    IORecursiveLockUnlock(optimizerLock);
    
    updateThroughput();
}

void IntelRequestOptimizer::updateThroughput() {
    IORecursiveLockLock(statsLock);
    
    // Calculate average latency
    uint64_t total = 0;
    uint32_t count = 0;
    
    for (uint32_t i = 0; i < latencyHistorySize; i++) {
        if (latencyHistory[i] > 0) {
            total += latencyHistory[i];
            count++;
        }
    }
    
    if (count > 0) {
        stats.averageLatencyUs = total / count;
    }
    
    IORecursiveLockUnlock(statsLock);
}

uint64_t IntelRequestOptimizer::calculatePercentile(uint32_t percentile) {
    if (percentile > 100) {
        return 0;
    }
    
    IORecursiveLockLock(optimizerLock);
    
    // Simple percentile calculation
    // In real implementation, would use proper sorting/selection
    uint64_t maxLatency = 0;
    for (uint32_t i = 0; i < latencyHistorySize; i++) {
        if (latencyHistory[i] > maxLatency) {
            maxLatency = latencyHistory[i];
        }
    }
    
    IORecursiveLockUnlock(optimizerLock);
    
    return maxLatency;
}
