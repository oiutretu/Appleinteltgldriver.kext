//
//  IntelRequestOptimizer.h
//  Graphics Driver
//
//  Request optimization, coalescing, and load balancing.
//  Week 24: Performance optimization for command submission.
//

#ifndef IntelRequestOptimizer_h
#define IntelRequestOptimizer_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOTimerEventSource.h>
#include "AppleIntelTGLController.h"
#include "IntelRequest.h"
#include "IntelRingBuffer.h"

// Forward declarations
class AppleIntelTGLController;
class IntelRequest;
class IntelRingBuffer;
class IntelRequestManager;

//
// Optimization Strategies
//

enum OptimizationStrategy {
    OPTIMIZATION_THROUGHPUT,        // Maximize throughput
    OPTIMIZATION_LATENCY,          // Minimize latency
    OPTIMIZATION_POWER,            // Minimize power
    OPTIMIZATION_BALANCED          // Balance all factors
};

enum CoalescingPolicy {
    COALESCE_NONE,                 // No coalescing
    COALESCE_SMALL,                // Small requests only
    COALESCE_AGGRESSIVE,           // Aggressive coalescing
    COALESCE_AUTO                  // Automatic based on load
};

enum PreemptionLevel {
    PREEMPTION_DISABLED,           // No preemption
    PREEMPTION_BATCH,              // Batch-level preemption
    PREEMPTION_COMMAND,            // Command-level preemption
    PREEMPTION_CONTEXT             // Context-level preemption
};

//
// Coalesced Request
//

struct CoalescedRequest {
    IntelRequest** requests;        // Array of requests
    uint32_t requestCount;          // Number of requests
    uint32_t maxRequests;           // Max capacity
    uint64_t totalSize;             // Total batch size
    uint64_t createTime;            // Creation time
    IntelRingBuffer* targetEngine;  // Target engine
    IntelRequestPriority priority;  // Highest priority
    CoalescedRequest* next;
};

//
// Load Balancing Info
//

struct EngineLoad {
    IntelRingBuffer* engine;
    uint32_t pendingRequests;       // Pending request count
    uint32_t queueDepth;            // Current queue depth
    uint64_t averageLatencyUs;      // Average latency
    uint64_t utilizationPercent;    // 0-100%
    uint64_t lastSubmitTime;        // Last submission (ns)
    bool isIdle;                    // Engine idle flag
};

//
// Preemption Context
//

struct PreemptionContext {
    IntelRequest* preemptedRequest; // Request being preempted
    IntelRequest* preemptingRequest;// Request preempting
    uint64_t preemptTime;           // When preempted
    uint32_t batchOffset;           // Offset in batch
    void* savedState;               // Saved GPU state
    PreemptionContext* next;
};

//
// Optimization Statistics
//

struct OptimizerStats {
    // Coalescing
    uint64_t coalescedRequests;
    uint64_t coalescesSaved;        // Submissions saved
    uint64_t averageCoalesceSize;
    
    // Priority
    uint64_t priorityBumps;         // Priority increases
    uint64_t priorityDrops;         // Priority decreases
    uint64_t agingAdjustments;      // Aging adjustments
    
    // Preemption
    uint64_t preemptions;
    uint64_t preemptionRestores;
    uint64_t preemptionFailures;
    
    // Load balancing
    uint64_t loadBalanceEvents;
    uint64_t engineMigrations;
    
    // Performance
    uint64_t throughputReqsPerSec;
    uint64_t averageLatencyUs;
    uint64_t p99LatencyUs;          // 99th percentile
};

//
// IntelRequestOptimizer Class
//

class IntelRequestOptimizer : public OSObject {
    OSDeclareDefaultStructors(IntelRequestOptimizer)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    
    bool initWithController(AppleIntelTGLController* controller);
    bool start();
    void stop();
    
    // Configuration
    void setOptimizationStrategy(OptimizationStrategy strategy);
    void setCoalescingPolicy(CoalescingPolicy policy);
    void setPreemptionLevel(PreemptionLevel level);
    void setAgingThreshold(uint64_t thresholdMs);
    
    // Request Coalescing
    bool shouldCoalesce(IntelRequest* request);
    CoalescedRequest* createCoalescedRequest();
    bool addToCoalescedRequest(CoalescedRequest* coalesced, 
                               IntelRequest* request);
    bool submitCoalescedRequest(CoalescedRequest* coalesced);
    void destroyCoalescedRequest(CoalescedRequest* coalesced);
    
    // Priority Optimization
    void adjustPriority(IntelRequest* request);
    void applyAging(IntelRequest* request);
    IntelRequestPriority calculateDynamicPriority(IntelRequest* request);
    void preventStarvation(IntelRequestQueue* queue);
    
    // Preemption
    bool canPreempt(IntelRequest* current, IntelRequest* newRequest);
    bool preemptRequest(IntelRequest* current, IntelRequest* newRequest);
    bool restorePreemptedRequest(PreemptionContext* ctx);
    void abortPreemption(PreemptionContext* ctx);
    
    // Load Balancing
    IntelRingBuffer* selectOptimalEngine(IntelRequest* request);
    void updateEngineLoad(IntelRingBuffer* engine);
    bool shouldMigrateRequest(IntelRequest* request, 
                             IntelRingBuffer* currentEngine);
    IntelRingBuffer* findLeastLoadedEngine(uint32_t engineMask);
    
    // Performance Tuning
    void optimizeForThroughput();
    void optimizeForLatency();
    void optimizeForPower();
    void autoTune();
    
    // Batch Optimization
    uint32_t calculateOptimalBatchSize(IntelRingBuffer* engine);
    bool shouldSplitBatch(IntelRequest* request);
    bool shouldMergeBatches(IntelRequest* req1, IntelRequest* req2);
    
    // Statistics
    void getStatistics(OptimizerStats* stats);
    void resetStatistics();
    void printStatistics();
    
    // Monitoring
    void updatePerformanceMetrics();
    uint64_t calculateThroughput();
    uint64_t calculateP99Latency();
    
private:
    AppleIntelTGLController* controller;
    IntelRequestManager* requestManager;
    
    // Configuration
    OptimizationStrategy strategy;
    CoalescingPolicy coalescingPolicy;
    PreemptionLevel preemptionLevel;
    uint64_t agingThresholdMs;      // Default 100ms
    
    // Coalescing
    CoalescedRequest* activeCoalesced; // Active coalesced requests
    uint32_t maxCoalesceSize;       // Max 16 requests
    uint32_t maxCoalesceDelayMs;    // Max 5ms delay
    IOTimerEventSource* coalesceTimer;
    
    // Engine tracking
    EngineLoad* engineLoads;        // Load per engine
    uint32_t engineCount;           // Number of engines
    
    // Preemption tracking
    PreemptionContext* preemptionContexts;
    uint32_t activePreemptions;
    
    // Statistics
    OptimizerStats stats;
    
    // Performance history
    uint64_t* latencyHistory;       // Circular buffer
    uint32_t latencyHistorySize;    // 1000 samples
    uint32_t latencyHistoryIndex;
    
    // Locks
    IORecursiveLock* optimizerLock;
    IORecursiveLock* statsLock;
    
    // Private methods - Coalescing
    bool canCoalesceRequests(IntelRequest* req1, IntelRequest* req2);
    uint32_t estimateCoalesceBenefit(CoalescedRequest* coalesced);
    void flushCoalescedRequests();
    static void coalesceTimerFired(OSObject* owner, IOTimerEventSource* sender);
    
    // Private methods - Priority
    uint32_t calculateWaitTime(IntelRequest* request);
    bool isStarving(IntelRequest* request);
    void bumpPriority(IntelRequest* request);
    void dropPriority(IntelRequest* request);
    
    // Private methods - Preemption
    bool saveGPUState(IntelRequest* request, void** savedState);
    bool restoreGPUState(IntelRequest* request, void* savedState);
    uint32_t estimatePreemptionCost(IntelRequest* request);
    bool isPreemptionWorthwhile(IntelRequest* current, IntelRequest* newRequest);
    
    // Private methods - Load Balancing
    uint32_t calculateEngineLoad(IntelRingBuffer* engine);
    bool isEngineOverloaded(IntelRingBuffer* engine);
    bool isEngineUnderloaded(IntelRingBuffer* engine);
    void redistributeLoad();
    
    // Private methods - Statistics
    void recordLatency(uint64_t latencyUs);
    void updateThroughput();
    uint64_t calculatePercentile(uint32_t percentile);
};

//
// Constants
//

#define MAX_COALESCE_SIZE           16      // Max requests per coalesce
#define MAX_COALESCE_DELAY_MS       5       // Max coalesce delay
#define AGING_THRESHOLD_MS          100     // 100ms before aging
#define STARVATION_THRESHOLD_MS     500     // 500ms = starvation
#define LOAD_BALANCE_INTERVAL_MS    50      // Load balance every 50ms
#define ENGINE_OVERLOAD_THRESHOLD   80      // 80% utilization
#define ENGINE_UNDERLOAD_THRESHOLD  20      // 20% utilization
#define LATENCY_HISTORY_SIZE        1000    // 1000 samples
#define PREEMPTION_COST_THRESHOLD   1000    // 1ms threshold

#endif /* IntelRequestOptimizer_h */
