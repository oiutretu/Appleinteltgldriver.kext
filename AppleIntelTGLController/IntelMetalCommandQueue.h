/*
 * IntelMetalCommandQueue.h - Metal Command Queue Interface
 * Week 42: Metal Commands - Full Implementation
 * 
 * Implements MTLCommandQueue interface for Metal command buffer management.
 * Handles command buffer lifecycle, priority scheduling, and submission tracking.
 */

#ifndef IntelMetalCommandQueue_h
#define IntelMetalCommandQueue_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

class IntelIOAccelerator;
class IntelMetalCommandBuffer;
class IntelGuCSubmission;


// MARK: - Command Queue Types


// Queue types (matches MTLCommandQueueType)
typedef enum {
    kMetalCommandQueueTypeRender   = 0,  // Render + compute + blit
    kMetalCommandQueueTypeCompute  = 1,  // Compute + blit only
    kMetalCommandQueueTypeBlit     = 2,  // Blit only
} MetalCommandQueueType;

// Queue priority (matches MTLCommandQueuePriority)
typedef enum {
    kMetalQueuePriorityLow      = 0,  // Background tasks
    kMetalQueuePriorityNormal   = 1,  // Default
    kMetalQueuePriorityHigh     = 2,  // Time-sensitive
    kMetalQueuePriorityRealtime = 3,  // Critical path
} MetalQueuePriority;

// Queue flags
#define kMetalQueueFlagNone           0x0000
#define kMetalQueueFlagNoTracking     0x0001  // Disable command buffer tracking
#define kMetalQueueFlagLowPower       0x0002  // Optimize for power
#define kMetalQueueFlagHighThroughput 0x0004  // Optimize for throughput


// MARK: - Queue Statistics


struct MetalQueueStatistics {
    uint64_t commandBuffersCreated;      // Total created
    uint64_t commandBuffersSubmitted;    // Total submitted
    uint64_t commandBuffersCompleted;    // Total completed
    uint64_t commandBuffersFailed;       // Total failed
    uint64_t totalGPUTime;               // Total GPU execution time (uss)
    uint64_t totalWaitTime;              // Total CPU wait time (uss)
    uint32_t peakPendingBuffers;         // Maximum pending at once
    uint32_t currentPendingBuffers;      // Currently pending
    uint64_t averageSubmissionLatency;   // Average submit latency (uss)
    uint64_t averageCompletionLatency;   // Average complete latency (uss)
};


// MARK: - Command Queue Configuration


struct MetalQueueConfiguration {
    MetalCommandQueueType type;          // Queue type
    MetalQueuePriority priority;         // Base priority
    uint32_t flags;                      // Queue flags
    uint32_t maxPendingBuffers;          // Max pending (default: 256)
    uint32_t completionCheckInterval;    // Completion polling (ms, default: 10)
    bool enableStatistics;               // Track statistics
    bool enableProfiling;                // Enable detailed profiling
    const char* label;                   // Debug label
};


// MARK: - IntelMetalCommandQueue Class


class IntelMetalCommandQueue : public OSObject {
    OSDeclareDefaultStructors(IntelMetalCommandQueue)
    
public:

    // MARK: - Factory & Lifecycle

    
    // Create command queue
    static IntelMetalCommandQueue* withAccelerator(IntelIOAccelerator* accel,
                                                   MetalCommandQueueType type,
                                                   MetalQueuePriority priority);
    
    // Initialize with configuration
    virtual bool initWithConfiguration(IntelIOAccelerator* accel,
                                      const MetalQueueConfiguration* config);
    
    // Cleanup
    virtual void free() override;
    

    // MARK: - Command Buffer Management

    
    // Create command buffer
    IntelMetalCommandBuffer* commandBuffer();
    IntelMetalCommandBuffer* commandBufferWithUnretainedReferences();
    
    // Submit command buffer
    IOReturn submitCommandBuffer(IntelMetalCommandBuffer* cmdBuffer);
    
    // Command buffer completion
    IOReturn waitForCommandBuffer(IntelMetalCommandBuffer* cmdBuffer,
                                  uint64_t timeoutNs);
    void notifyCommandBufferCompleted(IntelMetalCommandBuffer* cmdBuffer,
                                     IOReturn status);
    

    // MARK: - Queue Control

    
    // Suspend/resume queue
    IOReturn suspend();
    IOReturn resume();
    bool isSuspended() const;
    
    // Wait for all pending command buffers
    IOReturn waitUntilIdle(uint64_t timeoutNs);
    
    // Flush pending submissions
    IOReturn flush();
    

    // MARK: - Priority Management

    
    // Get/set queue priority
    MetalQueuePriority getPriority() const;
    IOReturn setPriority(MetalQueuePriority priority);
    
    // Boost priority temporarily
    IOReturn boostPriority(uint64_t durationNs);
    

    // MARK: - Queue Properties

    
    // Queue type
    MetalCommandQueueType getType() const { return queueType; }
    
    // Queue label
    const char* getLabel() const { return label; }
    void setLabel(const char* newLabel);
    
    // Queue configuration
    const MetalQueueConfiguration* getConfiguration() const { return &config; }
    

    // MARK: - Statistics & Profiling

    
    // Get statistics
    void getStatistics(MetalQueueStatistics* outStats);
    void resetStatistics();
    
    // Get pending count
    uint32_t getPendingCommandBufferCount() const;
    
    // Profiling markers
    IOReturn insertDebugCaptureBoundary();
    

    // MARK: - Internal Accessors

    
    IntelIOAccelerator* getAccelerator() const { return accelerator; }
    IntelGuCSubmission* getSubmission() const { return submission; }
    
private:

    // MARK: - Internal Methods

    
    // Completion tracking
    IOReturn trackCommandBuffer(IntelMetalCommandBuffer* cmdBuffer);
    IOReturn untrackCommandBuffer(IntelMetalCommandBuffer* cmdBuffer);
    
    // Completion polling
    static void completionTimerFired(OSObject* owner,
                                    IOTimerEventSource* timer);
    void checkCompletedCommandBuffers();
    
    // Priority boost management
    static void priorityBoostExpired(OSObject* owner,
                                    IOTimerEventSource* timer);
    void restoreBasePriority();
    
    // Statistics tracking
    void updateSubmissionStatistics();
    void updateCompletionStatistics(uint64_t gpuTime);
    

    // MARK: - Member Variables

    
    // Core references
    IntelIOAccelerator* accelerator;
    IntelGuCSubmission* submission;
    
    // Configuration
    MetalQueueConfiguration config;
    MetalCommandQueueType queueType;
    MetalQueuePriority basePriority;
    MetalQueuePriority currentPriority;
    uint32_t queueFlags;
    char label[64];
    
    // Command buffer tracking
    OSArray* pendingCommandBuffers;      // Active command buffers
    IOLock* trackingLock;                // Protects pending array
    
    // Completion polling
    IOTimerEventSource* completionTimer;
    IOWorkLoop* workLoop;
    
    // Priority boost
    IOTimerEventSource* priorityTimer;
    bool priorityBoosted;
    
    // State
    bool suspended;
    bool initialized;
    
    // Statistics
    MetalQueueStatistics stats;
    IOLock* statsLock;
    uint64_t lastSubmitTime;
    uint64_t lastCompleteTime;
};

#endif /* IntelMetalCommandQueue_h */
