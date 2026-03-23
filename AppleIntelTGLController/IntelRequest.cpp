//
//  IntelRequest.cpp
// macOS Driver
//
//  Week 21: Advanced Command Submission - Request Management
//

#include "IntelRequest.h"
#include "AppleIntelTGLController.h"
#include "IntelRingBuffer.h"
#include "IntelContext.h"
#include "IntelGEMObject.h"
#include "IntelMetalCommandBuffer.h"
#include <IOKit/IOLib.h>

#define super OSObject

// Request timeout (5 seconds)
#define REQUEST_TIMEOUT_MS 5000

// Maximum objects per request
#define MAX_REQUEST_OBJECTS 256

// Request pool size
#define REQUEST_POOL_SIZE 256

//
// IntelRequest implementation
//

OSDefineMetaClassAndStructors(IntelRequest, OSObject)

bool IntelRequest::init() {
    if (!super::init()) {
        return false;
    }
    
    controller = nullptr;
    ring = nullptr;
    context = nullptr;
    state = REQUEST_STATE_IDLE;
    seqno = 0;
    priority = REQUEST_PRIORITY_NORMAL;
    flags = 0;
    
    objects = nullptr;
    objectCount = 0;
    objectCapacity = 0;
    
    batchBuffer = nullptr;
    batchOffset = 0;
    batchLength = 0;
    
    completeCallback = nullptr;
    completeContext = nullptr;
    timeoutCallback = nullptr;
    timeoutContext = nullptr;
    
    fenceList = nullptr;
    fenceLock = IORecursiveLockAlloc();
    if (!fenceLock) {
        return false;
    }
    
    modernFence = nullptr;  // Modern GuC fence
    
    // Apple IOAccelerator support
    completionTag = 0;
    hangTimeoutMs = REQUEST_TIMEOUT_MS;  // Default 5 seconds
    contextID = 0;
    queueID = 0;
    commandCount = 0;
    commandBufferDesc = nullptr;
    
    allocTime = 0;
    submitTime = 0;
    startTime = 0;
    completeTime = 0;
    retireTime = 0;
    
    next = nullptr;
    prev = nullptr;
    
    return true;
}

bool IntelRequest::setBatchAddress(uint64_t userspaceAddress)
{
    IOLog("TGL: Setting batch address: userspace=0x%llx\n", userspaceAddress);

    if (state != REQUEST_STATE_ALLOCATED) {
        IOLog("TGL: Cannot set batch address in state %d\n", state);
        return false;
    }

    if (userspaceAddress == 0 || userspaceAddress == 0xFFFFFFFFFFFFFFFFULL) {
        IOLog("TGL: Invalid userspace address: 0x%llx\n", userspaceAddress);
        return false;
    }

    if (userspaceAddress & 0x3F) {
        IOLog("TGL: Userspace address not 64-byte aligned: 0x%llx\n", userspaceAddress);
        return false;
    }

    batchGPUAddress = userspaceAddress;
    return true;
}

namespace {
uint32_t metalCommandMinSize(uint32_t commandType) {
    switch (commandType) {
        case kMetalCommandTypeDraw:
            return sizeof(MetalDrawCommand);
        case kMetalCommandTypeDrawIndexed:
            return sizeof(MetalDrawIndexedCommand);
        case kMetalCommandTypeSetVertexBuffer:
        case kMetalCommandTypeSetFragmentBuffer:
        case kMetalCommandTypeSetComputeBuffer:
            return sizeof(MetalSetBufferCommand);
        case kMetalCommandTypeSetVertexTexture:
        case kMetalCommandTypeSetFragmentTexture:
        case kMetalCommandTypeSetComputeTexture:
            return sizeof(MetalSetTextureCommand);
        case kMetalCommandTypeSetVertexSamplerState:
        case kMetalCommandTypeSetFragmentSamplerState:
        case kMetalCommandTypeSetComputeSamplerState:
            return sizeof(MetalSetSamplerCommand);
        case kMetalCommandTypeSetRenderPipelineState:
        case kMetalCommandTypeSetComputePipelineState:
            return sizeof(MetalSetPipelineStateCommand);
        case kMetalCommandTypeSetViewport:
            return sizeof(MetalSetViewportCommand);
        case kMetalCommandTypeSetScissorRect:
            return sizeof(MetalSetScissorRectCommand);
        case kMetalCommandTypeDispatch:
            return sizeof(MetalDispatchCommand);
        case kMetalCommandTypeCopyBufferToBuffer:
            return sizeof(MetalCopyBufferCommand);
        case kMetalCommandTypeCopyTextureToTexture:
            return sizeof(MetalCopyTextureCommand);
        case kMetalCommandTypeFillBuffer:
            return sizeof(MetalFillBufferCommand);
        default:
            return 0;
    }
}

bool isValidMetalCommandType(uint32_t commandType) {
    switch (commandType) {
        case kMetalCommandTypeDraw:
        case kMetalCommandTypeDrawIndexed:
        case kMetalCommandTypeDrawIndirect:
        case kMetalCommandTypeDrawIndexedIndirect:
        case kMetalCommandTypeSetVertexBuffer:
        case kMetalCommandTypeSetVertexTexture:
        case kMetalCommandTypeSetVertexSamplerState:
        case kMetalCommandTypeSetFragmentBuffer:
        case kMetalCommandTypeSetFragmentTexture:
        case kMetalCommandTypeSetFragmentSamplerState:
        case kMetalCommandTypeSetRenderPipelineState:
        case kMetalCommandTypeSetViewport:
        case kMetalCommandTypeSetScissorRect:
        case kMetalCommandTypeSetDepthStencilState:
        case kMetalCommandTypeSetBlendColor:
        case kMetalCommandTypeDispatch:
        case kMetalCommandTypeDispatchIndirect:
        case kMetalCommandTypeSetComputePipelineState:
        case kMetalCommandTypeSetComputeBuffer:
        case kMetalCommandTypeSetComputeTexture:
        case kMetalCommandTypeSetComputeSamplerState:
        case kMetalCommandTypeSetThreadgroupMemory:
        case kMetalCommandTypeCopyBufferToBuffer:
        case kMetalCommandTypeCopyBufferToTexture:
        case kMetalCommandTypeCopyTextureToBuffer:
        case kMetalCommandTypeCopyTextureToTexture:
        case kMetalCommandTypeFillBuffer:
        case kMetalCommandTypeClearTexture:
        case kMetalCommandTypeGenerateMipmaps:
        case kMetalCommandTypeSynchronize:
        case kMetalCommandTypeWaitForFence:
        case kMetalCommandTypeUpdateFence:
        case kMetalCommandTypeMemoryBarrier:
            return true;
        default:
            return false;
    }
}
}

bool IntelRequest::validateCommandBuffer() const
{
    if (!commandBufferDesc) {
        if (batchGPUAddress != 0 && batchLength > 0) {
            IOLog("TGL: Skipping validation for GPU address submission\n");
            return true;
        }
        IOLog("TGL: No command buffer descriptor for validation\n");
        return false;
    }

    uint64_t bufferLength = commandBufferDesc->getLength();
    if (batchLength > 0 && batchLength <= bufferLength) {
        bufferLength = batchLength;
    }

    if (bufferLength < sizeof(MetalCommandHeader)) {
        IOLog("TGL: Command buffer too small for validation\n");
        return false;
    }

    IOMemoryMap* map = commandBufferDesc->map();
    if (!map) {
        IOLog("TGL: Failed to map command buffer for validation\n");
        return false;
    }

    const uint8_t* base = reinterpret_cast<const uint8_t*>(map->getVirtualAddress());
    if (!base) {
        IOLog("TGL: Invalid mapped command buffer address\n");
        map->release();
        return false;
    }

    uint64_t offset = 0;
    uint32_t commandCountLocal = 0;

    while (offset + sizeof(MetalCommandHeader) <= bufferLength) {
        const MetalCommandHeader* header =
            reinterpret_cast<const MetalCommandHeader*>(base + offset);
        uint64_t totalSize = sizeof(MetalCommandHeader) + header->commandSize;

        if (totalSize > (bufferLength - offset)) {
            IOLog("TGL: Command %u exceeds buffer length\n", commandCountLocal);
            map->release();
            return false;
        }

        if (!isValidMetalCommandType(header->commandType)) {
            IOLog("TGL: Unknown Metal command type: 0x%x\n", header->commandType);
            map->release();
            return false;
        }

        uint32_t minSize = metalCommandMinSize(header->commandType);
        if (header->commandSize < minSize) {
            IOLog("TGL: Command %u too small (type=0x%x)\n",
                  commandCountLocal, header->commandType);
            map->release();
            return false;
        }

        offset += totalSize;
        commandCountLocal++;
    }

    map->release();

    if (offset != bufferLength) {
        IOLog("TGL: Trailing bytes after command parsing\n");
        return false;
    }

    if (commandCountLocal == 0) {
        IOLog("TGL: No valid commands found in command buffer\n");
        return false;
    }

    IOLog("TGL: Batch buffer validation passed: %u commands\n", commandCountLocal);
    return true;
}

// IntelRequestQueue implementation
//

OSDefineMetaClassAndStructors(IntelRequestQueue, OSObject)

bool IntelRequestQueue::init() {
    if (!super::init()) {
        return false;
    }
    
    controller = nullptr;
    
    for (int i = 0; i < REQUEST_PRIORITY_COUNT; i++) {
        heads[i] = nullptr;
        tails[i] = nullptr;
        counts[i] = 0;
    }
    
    totalCount = 0;
    maxDepth = 1024;
    
    bzero(&stats, sizeof(stats));
    
    queueLock = IORecursiveLockAlloc();
    if (!queueLock) {
        return false;
    }
    
    return true;
}

void IntelRequestQueue::free() {
    clear();
    
    if (queueLock) {
        IORecursiveLockFree(queueLock);
        queueLock = nullptr;
    }
    
    super::free();
}

IntelRequestQueue* IntelRequestQueue::withController(AppleIntelTGLController* ctrl) {
    IntelRequestQueue* queue = new IntelRequestQueue;
    if (queue) {
        if (!queue->init()) {
            queue->release();
            return nullptr;
        }
        queue->controller = ctrl;
    }
    return queue;
}

bool IntelRequestQueue::enqueueWork(IntelRequest* request) {
    if (!request) {
        return false;
    }
    
    return enqueuePriorityWork(request, request->getPriority());
}

IntelRequest* IntelRequestQueue::dequeueWork() {
    IORecursiveLockLock(queueLock);
    
    // Try each priority from highest to lowest
    for (int i = REQUEST_PRIORITY_COUNT - 1; i >= 0; i--) {
        IntelRequest* request = dequeueInternalWork((IntelRequestPriority)i);
        if (request) {
            IORecursiveLockUnlock(queueLock);
            return request;
        }
    }
    
    IORecursiveLockUnlock(queueLock);
    return nullptr;
}

IntelRequest* IntelRequestQueue::peek() const {
    // Try each priority from highest to lowest
    for (int i = REQUEST_PRIORITY_COUNT - 1; i >= 0; i--) {
        if (heads[i]) {
            return heads[i];
        }
    }
    return nullptr;
}

bool IntelRequestQueue::remove(IntelRequest* request) {
    if (!request) {
        return false;
    }
    
    IORecursiveLockLock(queueLock);
    
    IntelRequestPriority priority = request->getPriority();
    IntelRequest* current = heads[priority];
    
    while (current) {
        if (current == request) {
            // Remove from list
            if (current->prev) {
                current->prev->next = current->next;
            } else {
                heads[priority] = current->next;
            }
            
            if (current->next) {
                current->next->prev = current->prev;
            } else {
                tails[priority] = current->prev;
            }
            
            current->next = nullptr;
            current->prev = nullptr;
            
            counts[priority]--;
            totalCount--;
            
            IORecursiveLockUnlock(queueLock);
            return true;
        }
        current = current->next;
    }
    
    IORecursiveLockUnlock(queueLock);
    return false;
}

void IntelRequestQueue::clear() {
    IORecursiveLockLock(queueLock);
    
    for (int i = 0; i < REQUEST_PRIORITY_COUNT; i++) {
        heads[i] = nullptr;
        tails[i] = nullptr;
        counts[i] = 0;
    }
    
    totalCount = 0;
    
    IORecursiveLockUnlock(queueLock);
}

bool IntelRequestQueue::enqueuePriorityWork(IntelRequest* request, IntelRequestPriority priority) {
    if (!request || priority >= REQUEST_PRIORITY_COUNT) {
        return false;
    }
    
    IORecursiveLockLock(queueLock);
    
    if (isFull()) {
        IORecursiveLockUnlock(queueLock);
        return false;
    }
    
    bool result = enqueueInternalWork(request, priority);
    
    IORecursiveLockUnlock(queueLock);
    return result;
}

IntelRequest* IntelRequestQueue::dequeuePriorityWork(IntelRequestPriority priority) {
    if (priority >= REQUEST_PRIORITY_COUNT) {
        return nullptr;
    }
    
    IORecursiveLockLock(queueLock);
    IntelRequest* request = dequeueInternalWork(priority);
    IORecursiveLockUnlock(queueLock);
    
    return request;
}

uint32_t IntelRequestQueue::getCountForPriority(IntelRequestPriority priority) const {
    if (priority >= REQUEST_PRIORITY_COUNT) {
        return 0;
    }
    return counts[priority];
}

IntelRequest* IntelRequestQueue::findBySeqno(uint32_t seqno) {
    IORecursiveLockLock(queueLock);
    
    for (int i = 0; i < REQUEST_PRIORITY_COUNT; i++) {
        IntelRequest* current = heads[i];
        while (current) {
            if (current->getSeqno() == seqno) {
                IORecursiveLockUnlock(queueLock);
                return current;
            }
            current = current->next;
        }
    }
    
    IORecursiveLockUnlock(queueLock);
    return nullptr;
}

IntelRequest* IntelRequestQueue::findByContext(IntelContext* context) {
    IORecursiveLockLock(queueLock);
    
    for (int i = 0; i < REQUEST_PRIORITY_COUNT; i++) {
        IntelRequest* current = heads[i];
        while (current) {
            if (current->getContext() == context) {
                IORecursiveLockUnlock(queueLock);
                return current;
            }
            current = current->next;
        }
    }
    
    IORecursiveLockUnlock(queueLock);
    return nullptr;
}

void IntelRequestQueue::getStats(IntelRequestStats* outStats) {
    if (!outStats) {
        return;
    }
    
    IORecursiveLockLock(queueLock);
    memcpy(outStats, &stats, sizeof(IntelRequestStats));
    outStats->queueDepth = totalCount;
    IORecursiveLockUnlock(queueLock);
}

void IntelRequestQueue::resetStats() {
    IORecursiveLockLock(queueLock);
    bzero(&stats, sizeof(stats));
    IORecursiveLockUnlock(queueLock);
}

bool IntelRequestQueue::enqueueInternalWork(IntelRequest* request, IntelRequestPriority priority) {
    request->next = nullptr;
    request->prev = tails[priority];
    
    if (tails[priority]) {
        tails[priority]->next = request;
    } else {
        heads[priority] = request;
    }
    
    tails[priority] = request;
    counts[priority]++;
    totalCount++;
    
    if (totalCount > stats.maxQueueDepth) {
        stats.maxQueueDepth = totalCount;
    }
    
    return true;
}

IntelRequest* IntelRequestQueue::dequeueInternalWork(IntelRequestPriority priority) {
    IntelRequest* request = heads[priority];
    if (!request) {
        return nullptr;
    }
    
    heads[priority] = request->next;
    if (heads[priority]) {
        heads[priority]->prev = nullptr;
    } else {
        tails[priority] = nullptr;
    }
    
    request->next = nullptr;
    request->prev = nullptr;
    
    counts[priority]--;
    totalCount--;
    
    return request;
}

//
// IntelRequestManager implementation
//

OSDefineMetaClassAndStructors(IntelRequestManager, OSObject)

bool IntelRequestManager::init() {
    if (!super::init()) {
        return false;
    }
    
    controller = nullptr;
    started = false;
    
    requestPool = nullptr;
    poolSize = 0;
    poolUsed = 0;
    
    poolLock = IORecursiveLockAlloc();
    managerLock = IORecursiveLockAlloc();
    
    if (!poolLock || !managerLock) {
        return false;
    }
    
    pendingQueue = IntelRequestQueue::withController(nullptr);
    runningQueue = IntelRequestQueue::withController(nullptr);
    completeQueue = IntelRequestQueue::withController(nullptr);
    
    if (!pendingQueue || !runningQueue || !completeQueue) {
        return false;
    }
    
    timeoutTimer = nullptr;
    timeoutInterval = REQUEST_TIMEOUT_MS;
    
    bzero(&globalStats, sizeof(globalStats));
    
    return true;
}

void IntelRequestManager::free() {
    stop();
    freeRequestPool();
    
    if (pendingQueue) {
        pendingQueue->release();
        pendingQueue = nullptr;
    }
    
    if (runningQueue) {
        runningQueue->release();
        runningQueue = nullptr;
    }
    
    if (completeQueue) {
        completeQueue->release();
        completeQueue = nullptr;
    }
    
    if (poolLock) {
        IORecursiveLockFree(poolLock);
        poolLock = nullptr;
    }
    
    if (managerLock) {
        IORecursiveLockFree(managerLock);
        managerLock = nullptr;
    }
    
    super::free();
}

bool IntelRequestManager::initWithController(AppleIntelTGLController* ctrl) {
    controller = ctrl;
    
    pendingQueue->setController(ctrl);
    runningQueue->setController(ctrl);
    completeQueue->setController(ctrl);
    
    return allocateRequestPool(REQUEST_POOL_SIZE);
}

bool IntelRequestManager::start() {
    if (started) {
        return true;
    }
    
    startTimeoutTimer(timeoutInterval);
    started = true;
    
    IOLog("IntelRequestManager: Started\n");
    return true;
}

void IntelRequestManager::stop() {
    if (!started) {
        return;
    }
    
    stopTimeoutTimer();
    retireAllRequests();
    started = false;
    
    IOLog("IntelRequestManager: Stopped\n");
}

IntelRequest* IntelRequestManager::allocateRequest(IntelRingBuffer* ring, IntelContext* context) {
    IORecursiveLockLock(poolLock);
    
    // Try to get from pool
    IntelRequest* request = nullptr;
    for (uint32_t i = 0; i < poolSize; i++) {
        if (requestPool[i]->getState() == REQUEST_STATE_IDLE) {
            request = requestPool[i];
            request->retain();
            poolUsed++;
            break;
        }
    }
    
    IORecursiveLockUnlock(poolLock);
    
    if (!request) {
        IOLog("IntelRequestManager: Pool exhausted\n");
        return nullptr;
    }
    
    if (!request->allocate(ring, context)) {
        request->release();
        return nullptr;
    }
    
    globalStats.allocated++;
    return request;
}

void IntelRequestManager::freeRequest(IntelRequest* request) {
    if (!request) {
        return;
    }
    
    request->setState(REQUEST_STATE_IDLE);
    request->release();
    
    IORecursiveLockLock(poolLock);
    poolUsed--;
    IORecursiveLockUnlock(poolLock);
}

bool IntelRequestManager::submitRequest(IntelRequest* request) {
    if (!request || !started) {
        return false;
    }
    
    if (!request->submit()) {
        return false;
    }
    
    IORecursiveLockLock(managerLock);
    bool result = pendingQueue->enqueueWork(request);
    if (result) {
        globalStats.submitted++;
    }
    IORecursiveLockUnlock(managerLock);
    
    return result;
}

bool IntelRequestManager::submitBatch(IntelRequest** requests, uint32_t count) {
    if (!requests || !count || !started) {
        return false;
    }
    
    IORecursiveLockLock(managerLock);
    
    for (uint32_t i = 0; i < count; i++) {
        if (!requests[i]->submit() || !pendingQueue->enqueueWork(requests[i])) {
            IORecursiveLockUnlock(managerLock);
            return false;
        }
        globalStats.submitted++;
    }
    
    IORecursiveLockUnlock(managerLock);
    return true;
}

IntelRequest* IntelRequestManager::getRequestBySeqno(uint32_t seqno) {
    IORecursiveLockLock(managerLock);
    
    IntelRequest* request = runningQueue->findBySeqno(seqno);
    if (!request) {
        request = completeQueue->findBySeqno(seqno);
    }
    
    IORecursiveLockUnlock(managerLock);
    return request;
}

IntelRequest* IntelRequestManager::getOldestPendingRequest() {
    return pendingQueue->peek();
}

uint32_t IntelRequestManager::getPendingCount() const {
    return pendingQueue->getCount();
}

void IntelRequestManager::notifyComplete(IntelRequest* request, bool success) {
    if (!request) {
        return;
    }
    
    IORecursiveLockLock(managerLock);
    
    request->setState(success ? REQUEST_STATE_COMPLETE : REQUEST_STATE_ERROR);
    
    runningQueue->remove(request);
    completeQueue->enqueueWork(request);
    
    if (success) {
        globalStats.completed++;
    } else {
        globalStats.errors++;
    }
    
    IORecursiveLockUnlock(managerLock);
}

void IntelRequestManager::retireCompletedRequests() {
    IORecursiveLockLock(managerLock);
    
    IntelRequest* request;
    while ((request = completeQueue->dequeueWork()) != nullptr) {
        request->retire();
        globalStats.retired++;
        freeRequest(request);
    }
    
    IORecursiveLockUnlock(managerLock);
}

void IntelRequestManager::retireAllRequests() {
    IORecursiveLockLock(managerLock);
    
    pendingQueue->clear();
    runningQueue->clear();
    retireCompletedRequests();
    
    IORecursiveLockUnlock(managerLock);
}

void IntelRequestManager::startTimeoutTimer(uint64_t intervalMs) {
    if (!timeoutTimer && controller) {
        IOWorkLoop* workLoop = controller->getWorkLoop();
        if (workLoop) {
            timeoutTimer = IOTimerEventSource::timerEventSource(this, timeoutTimerFired);
            if (timeoutTimer) {
                workLoop->addEventSource(timeoutTimer);
                timeoutTimer->setTimeoutMS(intervalMs);
            }
        }
    }
}

void IntelRequestManager::stopTimeoutTimer() {
    if (timeoutTimer) {
        timeoutTimer->cancelTimeout();
        timeoutTimer->disable();
        timeoutTimer = nullptr;
    }
}

void IntelRequestManager::checkTimeouts() {
    IORecursiveLockLock(managerLock);
    
    // Check running requests for timeouts
    uint64_t now = mach_absolute_time();
    uint64_t timeout = timeoutInterval * 1000000; // Convert to ns
    
    IntelRequest* request = runningQueue->peek();
    while (request) {
        uint64_t elapsed = now - request->getSubmitTime();
        if (elapsed > timeout) {
            IOLog("IntelRequestManager: Request %u timed out\n", request->getSeqno());
            request->setState(REQUEST_STATE_TIMEOUT);
            request->invokeTimeoutCallback();
            globalStats.timeouts++;
        }
        request = request->next;
    }
    
    IORecursiveLockUnlock(managerLock);
}

void IntelRequestManager::getStats(IntelRequestStats* outStats) {
    if (!outStats) {
        return;
    }
    
    IORecursiveLockLock(managerLock);
    memcpy(outStats, &globalStats, sizeof(IntelRequestStats));
    outStats->pendingRequests = pendingQueue->getCount();
    outStats->runningRequests = runningQueue->getCount();
    IORecursiveLockUnlock(managerLock);
}

void IntelRequestManager::resetStats() {
    IORecursiveLockLock(managerLock);
    bzero(&globalStats, sizeof(globalStats));
    pendingQueue->resetStats();
    runningQueue->resetStats();
    completeQueue->resetStats();
    IORecursiveLockUnlock(managerLock);
}

void IntelRequestManager::printStats() {
    IntelRequestStats stats;
    getStats(&stats);
    
    IOLog("Allocated:        %llu\n", stats.allocated);
    IOLog("Submitted:        %llu\n", stats.submitted);
    IOLog("Completed:        %llu\n", stats.completed);
    IOLog("Retired:          %llu\n", stats.retired);
    IOLog("Errors:           %llu\n", stats.errors);
    IOLog("Timeouts:         %llu\n", stats.timeouts);
    IOLog("Pending:          %u\n", stats.pendingRequests);
    IOLog("Running:          %u\n", stats.runningRequests);
    IOLog("Queue Depth:      %u / %u\n", stats.queueDepth, stats.maxQueueDepth);
    
    if (stats.completed > 0) {
        IOLog("Avg Latency:      %llu us\n", stats.totalLatencyUs / stats.completed);
        IOLog("Avg Execution:    %llu us\n", stats.totalExecutionUs / stats.completed);
    }
    
}

void IntelRequestManager::timeoutTimerFired(OSObject* owner, IOTimerEventSource* timer) {
    IntelRequestManager* manager = OSDynamicCast(IntelRequestManager, owner);
    if (manager) {
        manager->checkTimeouts();
        timer->setTimeoutMS(manager->timeoutInterval);
    }
}

bool IntelRequestManager::allocateRequestPool(uint32_t size) {
    if (requestPool) {
        return true;
    }
    
    requestPool = (IntelRequest**)IOMalloc(size * sizeof(IntelRequest*));
    if (!requestPool) {
        return false;
    }
    
    for (uint32_t i = 0; i < size; i++) {
        requestPool[i] = IntelRequest::withController(controller);
        if (!requestPool[i]) {
            freeRequestPool();
            return false;
        }
    }
    
    poolSize = size;
    poolUsed = 0;
    
    IOLog("IntelRequestManager: Allocated pool of %u requests\n", size);
    return true;
}

void IntelRequestManager::freeRequestPool() {
    if (requestPool) {
        for (uint32_t i = 0; i < poolSize; i++) {
            if (requestPool[i]) {
                requestPool[i]->release();
            }
        }
        IOFree(requestPool, poolSize * sizeof(IntelRequest*));
        requestPool = nullptr;
    }
    poolSize = 0;
    poolUsed = 0;
}

void IntelRequestManager::updateStats() {
    globalStats.queueDepth = pendingQueue->getCount() + runningQueue->getCount();
    if (globalStats.queueDepth > globalStats.maxQueueDepth) {
        globalStats.maxQueueDepth = globalStats.queueDepth;
    }
}

//
//

bool IntelRequest::initWithContext(IntelContext* ctx) {
    if (!init()) {
        return false;
    }
    context = ctx;
    // Note: IntelContext doesn't inherit from OSObject, so no retain/release
    return true;
}

IntelRequest* IntelRequest::withController(AppleIntelTGLController* ctrl) {
    IntelRequest* request = new IntelRequest;
    if (request) {
        if (!request->init()) {
            request->release();
            return nullptr;
        }
        request->controller = ctrl;
        if (ctrl) {
            ctrl->retain();
        }
    }
    return request;
}

bool IntelRequest::wait(uint64_t timeoutMs) {
    if (state == REQUEST_STATE_COMPLETE || state == REQUEST_STATE_RETIRED) {
        return true;
    }
    
    if (state == REQUEST_STATE_ERROR || state == REQUEST_STATE_TIMEOUT) {
        return false;
    }
    
    uint64_t start = mach_absolute_time();
    uint64_t timeout = timeoutMs * 1000000;
    
    while (state < REQUEST_STATE_COMPLETE) {
        IOSleep(1);
        
        if (timeoutMs > 0) {
            uint64_t now = mach_absolute_time();
            if ((now - start) > timeout) {
                IOLog("TGL: Request wait timeout after %llu ms\n", timeoutMs);
                return false;
            }
        }
    }
    
    return state == REQUEST_STATE_COMPLETE || state == REQUEST_STATE_RETIRED;
}

void IntelRequest::setState(IntelRequestState newState) {
    state = newState;
}

void IntelRequest::setPriority(IntelRequestPriority prio) {
    priority = prio;
}

bool IntelRequest::allocate(IntelRingBuffer* ringBuffer, IntelContext* ctx) {
    if (state != REQUEST_STATE_IDLE) {
        return false;
    }
    
    ring = ringBuffer;
    context = ctx;
    
    // Note: IntelRingBuffer and IntelContext don't inherit from OSObject
    // They are managed separately, no retain/release needed here
    
    state = REQUEST_STATE_ALLOCATED;
    allocTime = mach_absolute_time();
    
    return true;
}

bool IntelRequest::submit() {
    if (state != REQUEST_STATE_ALLOCATED) {
        IOLog("TGL: Cannot submit request in state %d\n", state);
        return false;
    }
    
    state = REQUEST_STATE_SUBMITTED;
    submitTime = mach_absolute_time();
    
    return true;
}

void IntelRequest::retire() {
    if (state == REQUEST_STATE_RETIRED) {
        return;
    }
    
    state = REQUEST_STATE_RETIRED;
    retireTime = mach_absolute_time();
    
    // Clear pointers (no release needed - not OSObject)
    ring = nullptr;
    context = nullptr;
    
    if (commandBufferDesc) {
        commandBufferDesc->release();
        commandBufferDesc = nullptr;
    }
}

void IntelRequest::invokeTimeoutCallback() {
    if (timeoutCallback) {
        timeoutCallback(this, (IntelRequest*)timeoutContext);
    }
}
