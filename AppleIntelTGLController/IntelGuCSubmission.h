/*
 * IntelGuCSubmission.h - GuC Work Queue and Command Submission
 * Week 39: GuC Submission
 *
 * This implements GuC-based command submission for Intel Tiger Lake GPUs.
 * GuC submission replaces legacy ring buffer submission with a firmware-managed
 * work queue system that provides:
 * - Hardware-accelerated context scheduling
 * - Priority-based execution
 * - Preemption support
 * - Better power management
 *
 * This is the primary method for submitting GPU work on Gen11+ hardware.
 */

#ifndef INTEL_GUC_SUBMISSION_H
#define INTEL_GUC_SUBMISSION_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

// Forward declarations
class IntelGuC;
class AppleIntelTGLController;
class IntelContext;
class IntelRequest;


// MARK: - GuC Work Queue Constants


// Work queue sizes
#define GUC_WQ_SIZE                 (PAGE_SIZE * 2)  // 8KB work queue
#define GUC_WQ_ITEM_SIZE            64               // 64 bytes per work item
#define GUC_MAX_WQ_ITEMS            (GUC_WQ_SIZE / GUC_WQ_ITEM_SIZE)

// Process descriptor sizes
#define GUC_PROCESS_DESC_SIZE       (PAGE_SIZE)

// Stage descriptor sizes
#define GUC_STAGE_DESC_SIZE         (PAGE_SIZE)
#define GUC_MAX_STAGE_DESCRIPTORS   1024

// Context descriptor constants
#define GUC_CTX_DESC_ATTR_ACTIVE    (1 << 0)
#define GUC_CTX_DESC_ATTR_PENDING   (1 << 1)
#define GUC_CTX_DESC_ATTR_PREEMPT   (1 << 2)

// Doorbell register layout
#define GUC_DOORBELL_ENABLED        (1 << 0)
#define GUC_DOORBELL_HW_ENABLED     (1 << 1)

// Priority levels (0 = lowest, 3 = highest)
#define GUC_CTX_PRIORITY_LOW        0
#define GUC_CTX_PRIORITY_NORMAL     1
#define GUC_CTX_PRIORITY_HIGH       2
#define GUC_CTX_PRIORITY_REALTIME   3


// MARK: - GuC Work Item Structure


// Work queue item types (from Linux kernel)
#define WQ_TYPE_BATCH_BUF        0x1
#define WQ_TYPE_PSEUDO           0x2
#define WQ_TYPE_INORDER          0x3
#define WQ_TYPE_NOOP             0x4
#define WQ_TYPE_MULTI_LRC        0x5

// Work queue item status
#define WQ_STATUS_ACTIVE         1
#define WQ_STATUS_SUSPENDED      2
#define WQ_STATUS_CMD_ERROR      3

// Work queue item submitted to GuC
struct GuCWorkItem {
    uint32_t header;
    uint32_t contextDescriptor;
    uint32_t ringTail;
    uint32_t fence;
    uint32_t reserved[12];  // Pad to 64 bytes
} __attribute__((packed));


// MARK: - GuC Context Descriptor


// Context descriptor registered with GuC
struct GuCContextDescriptor {
    uint32_t lrcDescriptor;     // Logical Ring Context descriptor
    uint32_t contextId;         // Context ID
    uint32_t priority;          // Scheduling priority
    uint32_t attributes;        // Context attributes
    uint64_t lrcBaseAddress;    // LRC base address
    uint32_t ringBufferAddress; // Ring buffer start
    uint32_t ringBufferSize;    // Ring buffer size
    uint32_t workQueueHead;     // Work queue head pointer
    uint32_t workQueueTail;     // Work queue tail pointer
    uint64_t workQueueAddress;  // Work queue base address
    uint32_t reserved[6];       // Reserved fields
} __attribute__((packed));


// MARK: - GuC Stage Descriptor


// Stage descriptor for context switching
struct GuCStageDescriptor {
    uint32_t contextId;
    uint64_t lrcAddress;
    uint32_t priority;
    uint32_t attributes;
    uint32_t engineMask;        // Which engines this context uses
    uint32_t doorbellId;        // Doorbell for this context
    uint64_t processDescPtr;    // Process descriptor pointer
    uint32_t workQueueHead;
    uint32_t workQueueTail;
    uint32_t contextIndex;
    uint32_t reserved[5];
} __attribute__((packed));


// MARK: - GuC Process Descriptor


// Process descriptor for multi-engine contexts
struct GuCProcessDescriptor {
    uint32_t processId;
    uint32_t attributes;
    uint64_t contextBaseAddress;
    uint32_t numContexts;
    uint32_t reserved[11];
} __attribute__((packed));


// MARK: - GuC Doorbell Info


// Doorbell information for fast notification
struct GuCDoorbellInfo {
    uint32_t doorbellId;        // Doorbell index
    uint32_t contextId;         // Associated context
    volatile uint32_t* cookie;  // Doorbell cookie location
    uint32_t status;            // Doorbell status
    uint64_t physicalAddress;   // Physical address for doorbell
} __attribute__((packed));


// MARK: - GuC Submission Queue


// Per-context submission queue
class GuCSubmissionQueue {
public:
    GuCSubmissionQueue();
    ~GuCSubmissionQueue();
    
    bool init(uint32_t queueSize);
    void cleanup();
    
    // Queue operations (renamed to avoid kernel queue.h macro conflicts)
    bool enqueueWork(GuCWorkItem* item);  // renamed from enqueue
    bool dequeueWork(GuCWorkItem* item);  // renamed from dequeue
    bool isFull();
    bool isEmpty();
    
    // Queue pointers
    uint32_t getHead() { return head; }
    uint32_t getTail() { return tail; }
    void updateHead(uint32_t newHead);
    
    // Memory access
    void* getQueueBuffer() { return queueBuffer; }
    uint64_t getPhysicalAddress();
    
private:
    IOBufferMemoryDescriptor* queueMemory;
    void* queueBuffer;
    uint32_t queueSize;
    uint32_t head;
    uint32_t tail;
    IOLock* queueLock;
};


// MARK: - GuC Context State


// Per-context state for GuC submission
class GuCContextState {
public:
    GuCContextState();
    ~GuCContextState();
    
    bool init(IntelContext* ctx);
    void cleanup();
    
    // Context info
    IntelContext* context;
    uint32_t contextId;
    uint32_t priority;
    bool registered;
    
    // Work queue
    GuCSubmissionQueue* workQueue;
    GuCContextDescriptor descriptor;
    
    // Doorbell
    GuCDoorbellInfo doorbell;
    bool doorbellEnabled;
    
    // Stage descriptor
    GuCStageDescriptor* stageDesc;
    IOBufferMemoryDescriptor* stageDescMemory;
    
    // Statistics
    uint64_t submissionsCount;
    uint64_t completionsCount;
    uint64_t preemptionsCount;
};


// MARK: - IntelGuCSubmission Class


class IntelGuCSubmission : public OSObject {
    OSDeclareDefaultStructors(IntelGuCSubmission)
    
public:
    // Initialization
    bool init(IntelGuC* guc, AppleIntelTGLController* controller);
    void free() override;
    
    bool initializeSubmission();
    void shutdownSubmission();
    

    // Context Management

    
    // Register/unregister contexts with GuC
    bool registerContext(IntelContext* context, uint32_t priority);
    bool unregisterContext(IntelContext* context);
    bool updateContextPriority(IntelContext* context, uint32_t priority);
    
    // Context descriptor setup
    bool setupContextDescriptor(GuCContextState* state);
    bool updateContextDescriptor(GuCContextState* state);
    

    // Work Queue Management

    
    // Create/destroy work queues
    bool createWorkQueue(GuCContextState* state);
    void destroyWorkQueue(GuCContextState* state);
    
    // Work queue operations
    bool submitWorkItem(GuCContextState* state, GuCWorkItem* item);
    bool processCompletions(GuCContextState* state);
    

    // Doorbell Management

    
    // Allocate/release doorbells
    bool allocateDoorbell(GuCContextState* state);
    void releaseDoorbell(GuCContextState* state);
    
    // Ring doorbell to notify GuC
    bool ringDoorbell(GuCContextState* state);
    bool ringDoorbellForContext(IntelContext* context);
    

    // Command Submission

    
    // Submit GPU commands via GuC
    bool submitRequest(IntelRequest* request);
    bool submitBatch(IntelContext* context, uint64_t batchAddress, uint32_t batchLength);
    
    // Submission helpers
    bool buildWorkItem(IntelRequest* request, GuCWorkItem* item);
    bool queueWorkItem(GuCContextState* state, GuCWorkItem* item);
    

    // Priority Scheduling

    
    // Set context priorities
    bool setContextPriority(IntelContext* context, uint32_t priority);
    uint32_t getContextPriority(IntelContext* context);
    
    // Priority levels
    static const uint32_t PriorityLow      = GUC_CTX_PRIORITY_LOW;
    static const uint32_t PriorityNormal   = GUC_CTX_PRIORITY_NORMAL;
    static const uint32_t PriorityHigh     = GUC_CTX_PRIORITY_HIGH;
    static const uint32_t PriorityRealtime = GUC_CTX_PRIORITY_REALTIME;
    

    // Preemption Support

    
    // Enable/disable preemption
    bool enablePreemption();
    bool disablePreemption();
    bool isPreemptionEnabled() { return preemptionEnabled; }
    
    // Preempt context
    bool preemptContext(IntelContext* context);
    

    // Stage Descriptors

    
    // Setup stage descriptors for all contexts
    bool initializeStageDescriptors();
    void cleanupStageDescriptors();
    
    bool allocateStageDescriptor(GuCContextState* state, uint32_t index);
    void releaseStageDescriptor(GuCContextState* state);
    
    // Fence buffer for GPU completion signaling
    bool initializeFenceBuffer();
    void cleanupFenceBuffer();
    

    // Status and Debug

    
    // Get context state
    GuCContextState* getContextState(IntelContext* context);
    
    // Statistics
    struct SubmissionStats {
        uint64_t totalSubmissions;
        uint64_t totalCompletions;
        uint64_t doorbellRings;
        uint64_t preemptions;
        uint64_t queueFull;
        uint64_t errors;
    };
    
    void getStatistics(SubmissionStats* stats);
    void resetStatistics();
    
    // Debug
    void dumpContextState(IntelContext* context);
    void dumpWorkQueue(GuCContextState* state);
    

    // Fence Synchronization (NEW)

    
    // Check if a fence has been signaled
    bool isFenceSignaled(uint32_t fenceID);
    
    // Wait for fence with timeout
    IOReturn waitForFence(uint32_t fenceID, uint32_t timeoutMs);
    
    // Get current fence value
    uint32_t getCurrentFenceValue();
    
    // Signal a fence (for testing/manual completion)
    void signalFence(uint32_t fenceID);
    

    // GPU Hang Detection and Recovery

    
    // Check if GPU is hung
    bool isGPUHung();
    
    // Reset the GPU after a hang
    IOReturn resetGPU();
    
    // Reinitialize GuC after reset
    IOReturn reinitializeGuC();
    
private:
    // Core components
    IntelGuC* guc;
    AppleIntelTGLController* controller;
    bool initialized;
    
    // Context tracking
    OSArray* contexts;              // Array of GuCContextState
    IOLock* contextsLock;
    uint32_t nextContextId;
    
    // Doorbell management
    uint32_t doorbellBitmap[32];    // 1024 doorbells (32 * 32 bits)
    IOLock* doorbellLock;
    
    // Stage descriptors
    IOBufferMemoryDescriptor* stageDescriptorPool;
    void* stageDescriptorBase;
    uint32_t stageDescriptorCount;
    
    // Process descriptor
    IOBufferMemoryDescriptor* processDescMemory;
    GuCProcessDescriptor* processDesc;
    
    // Fence buffer (for GPU completion signaling)
    IOBufferMemoryDescriptor* fenceBuffer;
    uint64_t fenceBufferSize;
    uint32_t* fenceBufferPtr;
    
    // Preemption
    bool preemptionEnabled;
    
    // Statistics
    SubmissionStats stats;
    
    // Helper methods
    uint32_t allocateContextId();
    void releaseContextId(uint32_t contextId);
    
    int allocateDoorbellId();
    void releaseDoorbellId(int doorbellId);
    
    bool sendContextAction(uint32_t action, uint32_t contextId, uint32_t data);
};

#endif // INTEL_GUC_SUBMISSION_H
