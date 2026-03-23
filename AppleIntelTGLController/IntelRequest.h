//
//  IntelRequest.h
//  Graphics Driver
//
//  Week 21: Advanced Command Submission - Request Management
//  Request lifecycle: allocation, submission, tracking, completion, retirement
//

#ifndef IntelRequest_h
#define IntelRequest_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOTimerEventSource.h>
#include "linux_compat.h"

class AppleIntelTGLController;
class IntelRingBuffer;
class IntelContext;
class IntelGEMObject;

// Request states
enum IntelRequestState {
    REQUEST_STATE_IDLE = 0,
    REQUEST_STATE_ALLOCATED,
    REQUEST_STATE_PENDING,
    REQUEST_STATE_SUBMITTED,
    REQUEST_STATE_RUNNING,
    REQUEST_STATE_COMPLETE,
    REQUEST_STATE_RETIRED,
    REQUEST_STATE_ERROR,
    REQUEST_STATE_TIMEOUT
};

// Request priorities
enum IntelRequestPriority {
    REQUEST_PRIORITY_LOW = 0,
    REQUEST_PRIORITY_NORMAL = 1,
    REQUEST_PRIORITY_HIGH = 2,
    REQUEST_PRIORITY_REALTIME = 3,
    REQUEST_PRIORITY_COUNT = 4
};

// Request flags
#define REQUEST_FLAG_PREEMPTIBLE    (1 << 0)
#define REQUEST_FLAG_PRIORITY       (1 << 1)
#define REQUEST_FLAG_PROTECTED      (1 << 2)
#define REQUEST_FLAG_KERNEL         (1 << 3)
#define REQUEST_FLAG_SYNC           (1 << 4)

// Forward declarations
class IntelRequest;
class IntelRequestQueue;

// Request completion callback
typedef void (*RequestCompleteCallback)(void* context, IntelRequest* request, bool success);

// Request timeout callback
typedef void (*RequestTimeoutCallback)(void* context, IntelRequest* request);

// Forward declaration of fence object (defined in IntelFence.h)
class IntelFence;

// Request statistics
struct IntelRequestStats {
    uint64_t allocated;                  // Total allocated
    uint64_t submitted;                  // Total submitted
    uint64_t completed;                  // Total completed
    uint64_t retired;                    // Total retired
    uint64_t errors;                     // Total errors
    uint64_t timeouts;                   // Total timeouts
    uint64_t preemptions;                // Total preemptions
    uint64_t totalLatencyUs;             // Total submission latency
    uint64_t totalExecutionUs;           // Total execution time
    uint32_t queueDepth;                 // Current queue depth
    uint32_t maxQueueDepth;              // Maximum queue depth
    uint32_t pendingRequests;            // Requests pending
    uint32_t runningRequests;            // Requests running
};

//
// IntelRequest - GPU command request
//
class IntelRequest : public OSObject {
    OSDeclareDefaultStructors(IntelRequest)
    
public:
    // Initialization
    bool init() override;
    void free() override;
    bool initWithContext(IntelContext* context);
    
    // Factory methods
    static IntelRequest* withController(AppleIntelTGLController* controller);
    static IntelRequest* withRing(IntelRingBuffer* ring);
    
    // Lifecycle
    bool allocate(IntelRingBuffer* ring, IntelContext* context);
    bool submit();
    bool wait(uint64_t timeoutMs = 0);
    bool waitForCompletion(uint64_t timeoutMs = 0) { return wait(timeoutMs); }
    bool cancel();
    void retire();
    
    // State management
    IntelRequestState getState() const { return state; }
    void setState(IntelRequestState newState);
    bool isComplete() const { return state >= REQUEST_STATE_COMPLETE; }
    bool isRetired() const { return state == REQUEST_STATE_RETIRED; }
    bool hasError() const { return state == REQUEST_STATE_ERROR; }
    
    // Properties
    void setSeqno(uint32_t seqno) { this->seqno = seqno; }
    uint32_t getSeqno() const { return seqno; }
    
    void setPriority(IntelRequestPriority prio);
    IntelRequestPriority getPriority() const { return priority; }
    
    void setFlags(uint32_t flags) { this->flags = flags; }
    uint32_t getFlags() const { return flags; }
    
    void setContext(IntelContext* ctx) { context = ctx; }
    IntelContext* getContext() const { return context; }
    
    void setRing(IntelRingBuffer* ring) { this->ring = ring; }
    IntelRingBuffer* getRing() const { return ring; }
    
    // Command buffer management
    bool addObject(IntelGEMObject* obj);
    bool removeObject(IntelGEMObject* obj);
    void clearObjects();
    uint32_t getObjectCount() const { return objectCount; }
    
    // Batch buffer
    void setBatchBuffer(IntelGEMObject* batch) { batchBuffer = batch; }
    IntelGEMObject* getBatchBuffer() const { return batchBuffer; }
    
    void setBatchOffset(uint32_t offset) { batchOffset = offset; }
    uint32_t getBatchOffset() const { return batchOffset; }
    
    void setBatchLength(uint32_t length) { batchLength = length; }
    uint32_t getBatchLength() const { return batchLength; }
    
    // Direct GPU address submission (for Apple Metal command buffers)
    bool setBatchAddress(uint64_t userspaceAddress);
    uint64_t getBatchAddress() const { return batchGPUAddress; }
    
    // Get fence ID for this request (for Apple IOAccelerator compatibility)
    uint32_t getFence() const { return (uint32_t)(uintptr_t)modernFence; }
    
    // Apple IOAccelerator support methods
    void setCompletionTag(uint64_t tag) { completionTag = tag; }
    uint64_t getCompletionTag() const { return completionTag; }
    
    void setHangTimeout(uint32_t timeoutMs) { hangTimeoutMs = timeoutMs; }
    uint32_t getHangTimeout() const { return hangTimeoutMs; }
    
    bool validateCommandBuffer() const;  // Validate command buffer before submission
    
    // Apple Metal/IOAccelerator metadata
    void setContextID(uint32_t id) { contextID = id; }
    uint32_t getContextID() const { return contextID; }
    
    void setQueueID(uint32_t id) { queueID = id; }
    uint32_t getQueueID() const { return queueID; }
    
    void setCommandCount(uint32_t count) { commandCount = count; }
    uint32_t getCommandCount() const { return commandCount; }
    
    void setCommandBuffer(IOMemoryDescriptor* desc) {
        if (commandBufferDesc) commandBufferDesc->release();
        commandBufferDesc = desc;
        if (commandBufferDesc) commandBufferDesc->retain();
    }
    IOMemoryDescriptor* getCommandBuffer() const { return commandBufferDesc; }
    
    // Fence management
    void setFence(class IntelFence* fence) { modernFence = fence; }
    
    // Sequence number (for tracking)
    uint32_t getSequenceNumber() const { return seqno; }
    
    // Callback management
    void setCompleteCallback(RequestCompleteCallback cb, void* ctx);
    void setTimeoutCallback(RequestTimeoutCallback cb, void* ctx);
    
    // Fence operations
    IntelFence* createFence();
    bool waitForFence(IntelFence* fence, uint64_t timeoutMs = 0);
    void signalFence(IntelFence* fence);
    void destroyFence(IntelFence* fence);
    
    // Modern fence operations (GuC)
    void setModernFence(class IntelFence* fence) { modernFence = fence; }
    class IntelFence* getModernFence() const { return modernFence; }
    
    // Timing
    uint64_t getSubmitTime() const { return submitTime; }
    uint64_t getStartTime() const { return startTime; }
    uint64_t getCompleteTime() const { return completeTime; }
    uint64_t getLatencyUs() const;
    uint64_t getExecutionUs() const;
    
    // Internal methods (exposed for request manager)
    void invokeCompleteCallback(bool success);
    void invokeTimeoutCallback();
    
    // Linked list
    IntelRequest* next;
    IntelRequest* prev;
    
private:
    AppleIntelTGLController* controller;
    IntelRingBuffer* ring;
    IntelContext* context;
    
    // Request state
    IntelRequestState state;
    uint32_t seqno;
    IntelRequestPriority priority;
    uint32_t flags;
    
    // Objects referenced by this request
    IntelGEMObject** objects;
    uint32_t objectCount;
    uint32_t objectCapacity;
    
    // Batch buffer
    IntelGEMObject* batchBuffer;
    uint32_t batchOffset;
    uint32_t batchLength;
    uint64_t batchGPUAddress;  // For direct GPU address submission (Apple Metal)
    
    // Callbacks
    RequestCompleteCallback completeCallback;
    void* completeContext;
    RequestTimeoutCallback timeoutCallback;
    void* timeoutContext;
    
    // Fences (legacy - for linked list)
    IntelFence* fenceList;
    IORecursiveLock* fenceLock;
    
    // Modern fence (GuC completion)
    class IntelFence* modernFence;  // Forward declare to avoid circular include
    
    // Apple IOAccelerator support
    uint64_t completionTag;          // Completion tracking tag
    uint32_t hangTimeoutMs;          // Hang detection timeout in milliseconds
    uint32_t contextID;              // Apple context ID
    uint32_t queueID;                // Apple queue ID
    uint32_t commandCount;           // Number of commands in buffer
    IOMemoryDescriptor* commandBufferDesc;  // Command buffer descriptor
    
    // Timing
    uint64_t allocTime;
    uint64_t submitTime;
    uint64_t startTime;
    uint64_t completeTime;
    uint64_t retireTime;
};

//
// IntelRequestQueue - Priority queue for requests
//
class IntelRequestQueue : public OSObject {
    OSDeclareDefaultStructors(IntelRequestQueue)
    
public:
    // Initialization
    bool init() override;
    void free() override;
    
    // Factory
    static IntelRequestQueue* withController(AppleIntelTGLController* controller);
    
    // Queue operations
    bool enqueueWork(IntelRequest* request);
    IntelRequest* dequeueWork();
    IntelRequest* peek() const;
    bool remove(IntelRequest* request);
    void clear();
    
    // Priority queue operations
    bool enqueuePriorityWork(IntelRequest* request, IntelRequestPriority priority);
    IntelRequest* dequeuePriorityWork(IntelRequestPriority priority);
    
    // Queue state
    uint32_t getCount() const { return totalCount; }
    uint32_t getCountForPriority(IntelRequestPriority priority) const;
    bool isEmpty() const { return totalCount == 0; }
    bool isFull() const { return totalCount >= maxDepth; }
    
    // Limits
    void setMaxDepth(uint32_t depth) { maxDepth = depth; }
    uint32_t getMaxDepth() const { return maxDepth; }
    
    // Controller
    void setController(AppleIntelTGLController* ctrl) { controller = ctrl; }
    AppleIntelTGLController* getController() const { return controller; }
    
    // Search
    IntelRequest* findBySeqno(uint32_t seqno);
    IntelRequest* findByContext(IntelContext* context);
    
    // Statistics
    void getStats(IntelRequestStats* stats);
    void resetStats();
    
private:
    AppleIntelTGLController* controller;
    
    // Priority queues (one per priority level)
    IntelRequest* heads[REQUEST_PRIORITY_COUNT];
    IntelRequest* tails[REQUEST_PRIORITY_COUNT];
    uint32_t counts[REQUEST_PRIORITY_COUNT];
    
    // Total count
    uint32_t totalCount;
    uint32_t maxDepth;
    
    // Statistics
    IntelRequestStats stats;
    
    // Lock
    IORecursiveLock* queueLock;
    
    // Internal methods
    bool enqueueInternalWork(IntelRequest* request, IntelRequestPriority priority);
    IntelRequest* dequeueInternalWork(IntelRequestPriority priority);
};

//
// IntelRequestManager - Request lifecycle management
//
class IntelRequestManager : public OSObject {
    OSDeclareDefaultStructors(IntelRequestManager)
    
public:
    // Initialization
    bool init() override;
    void free() override;
    
    bool initWithController(AppleIntelTGLController* controller);
    bool start();
    void stop();
    
    // Request allocation
    IntelRequest* allocateRequest(IntelRingBuffer* ring, IntelContext* context);
    void freeRequest(IntelRequest* request);
    
    // Request submission
    bool submitRequest(IntelRequest* request);
    bool submitBatch(IntelRequest** requests, uint32_t count);
    
    // Request tracking
    IntelRequest* getRequestBySeqno(uint32_t seqno);
    IntelRequest* getOldestPendingRequest();
    uint32_t getPendingCount() const;
    
    // Request completion
    void notifyComplete(IntelRequest* request, bool success);
    void retireCompletedRequests();
    void retireAllRequests();
    
    // Timeout management
    void startTimeoutTimer(uint64_t intervalMs);
    void stopTimeoutTimer();
    void checkTimeouts();
    
    // Statistics
    void getStats(IntelRequestStats* stats);
    void resetStats();
    void printStats();
    
    // Accessors
    IntelRequestQueue* getPendingQueue() const { return pendingQueue; }
    IntelRequestQueue* getRunningQueue() const { return runningQueue; }
    IntelRequestQueue* getCompleteQueue() const { return completeQueue; }
    
private:
    AppleIntelTGLController* controller;
    bool started;
    
    // Request pools
    IntelRequest** requestPool;
    uint32_t poolSize;
    uint32_t poolUsed;
    IORecursiveLock* poolLock;
    
    // Request queues
    IntelRequestQueue* pendingQueue;     // Waiting to submit
    IntelRequestQueue* runningQueue;     // Currently executing
    IntelRequestQueue* completeQueue;    // Completed, waiting to retire
    
    // Timeout timer
    IOTimerEventSource* timeoutTimer;
    uint64_t timeoutInterval;
    
    // Statistics
    IntelRequestStats globalStats;
    
    // Locks
    IORecursiveLock* managerLock;
    
    // Callbacks
    static void timeoutTimerFired(OSObject* owner, IOTimerEventSource* timer);
    
    // Internal methods
    bool allocateRequestPool(uint32_t size);
    void freeRequestPool();
    void updateStats();
};

#endif /* IntelRequest_h */
