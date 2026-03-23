/*
 * IntelGuCSLPC.h - GuC Single Loop Power Control (SLPC)
 * Week 40: GuC SLPC & Logging
 * 
 * SLPC is GuC firmware-managed dynamic GPU frequency scaling that replaces
 * the legacy host-based frequency management. Benefits:
 * - Lower latency frequency changes (~microseconds vs milliseconds)
 * - Better power efficiency (GuC has direct access to GPU state)
 * - Reduced CPU overhead (GuC handles frequency decisions)
 * - Workload-aware scaling (GuC sees actual GPU utilization)
 * 
 * This is REQUIRED for optimal Tiger Lake performance and power management.
 */

#ifndef INTEL_GUC_SLPC_H
#define INTEL_GUC_SLPC_H

#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>

// Forward declarations
class IntelGuC;
class AppleIntelTGLController;


// MARK: - SLPC Constants


// SLPC parameters
#define SLPC_PARAM_TASK_ENABLE          0x1000
#define SLPC_PARAM_GLOBAL_STATE         0x1001
#define SLPC_PARAM_GTPERF_THRESHOLD     0x1002
#define SLPC_PARAM_GLOBAL_MAX_FREQ      0x1003
#define SLPC_PARAM_GLOBAL_MIN_FREQ      0x1004
#define SLPC_PARAM_GTPERF_LIMIT_REASONS 0x1005
#define SLPC_PARAM_EFFICIENT_FREQ       0x1006
#define SLPC_PARAM_IGNORE_EFFICIENT_FREQ 0x1007

// SLPC state values
#define SLPC_STATE_NOT_SUPPORTED        0
#define SLPC_STATE_DISABLED             1
#define SLPC_STATE_INITIALIZING         2
#define SLPC_STATE_RUNNING              3
#define SLPC_STATE_ERROR                4

// Frequency limits (MHz for Tiger Lake)
#define SLPC_MIN_FREQ_MHZ               300    // 300 MHz minimum
#define SLPC_MAX_FREQ_MHZ               1300   // 1.3 GHz maximum (Tiger Lake)
#define SLPC_EFFICIENT_FREQ_MHZ         800    // 800 MHz efficient frequency

// Performance thresholds (percentage)
#define SLPC_THRESHOLD_LOW              25     // Scale down below 25%
#define SLPC_THRESHOLD_HIGH             75     // Scale up above 75%

// SLPC Event types
#define SLPC_EVENT_FREQ_CHANGE          1
#define SLPC_EVENT_POWER_CHANGE         2
#define SLPC_EVENT_THROTTLE             3
#define SLPC_EVENT_BOOST_ENABLE         4
#define SLPC_EVENT_BOOST_DISABLE        5
#define SLPC_EVENT_POWER_STATE          6

// SLPC Parameters
#define SLPC_PARAM_MIN_FREQ             0
#define SLPC_PARAM_MAX_FREQ             1
#define SLPC_PARAM_EFFICIENT_FREQ       2
#define SLPC_PARAM_BOOST                3
#define SLPC_PARAM_POWER_STATE          4

// SLPC Throttle Reasons
#define SLPC_THROTTLE_NONE              0
#define SLPC_THROTTLE_THERMAL           (1 << 0)
#define SLPC_THROTTLE_POWER             (1 << 1)
#define SLPC_THROTTLE_CURRENT           (1 << 2)
#define SLPC_THROTTLE_VOLTAGE           (1 << 3)
#define SLPC_THROTTLE_PROCHOT           (1 << 4)
#define SLPC_THROTTLE_RATL              (1 << 5)
#define SLPC_THROTTLE_VR_TDC            (1 << 6)
#define SLPC_THROTTLE_VR_THERMALERT     (1 << 7)


// MARK: - SLPC Task IDs


enum SLPCTaskID {
    SLPC_TASK_GTPERF          = 0,  // GPU performance management
    SLPC_TASK_SPG             = 1,  // Stochastic Power Gating
    SLPC_TASK_IBC             = 2,  // Idle Balancing Controller
    SLPC_TASK_IA_FREQ         = 3,  // IA (CPU) frequency coordination
    SLPC_TASK_BALANCER        = 4,  // Load balancer
    SLPC_TASK_COUNT           = 5
};


// MARK: - SLPC Statistics


struct SLPCStatistics {
    // Frequency statistics
    uint32_t currentFreqMHz;        // Current GPU frequency
    uint32_t requestedFreqMHz;      // Requested frequency
    uint32_t minFreqMHz;            // Minimum frequency
    uint32_t maxFreqMHz;            // Maximum frequency
    uint32_t efficientFreqMHz;      // Efficient frequency
    
    // Utilization
    uint32_t gpuUtilization;        // GPU utilization (0-100%)
    uint64_t activeTimeNs;          // GPU active time (nanoseconds)
    uint64_t idleTimeNs;            // GPU idle time (nanoseconds)
    
    // Frequency transitions
    uint64_t freqUpCount;           // Frequency increase count
    uint64_t freqDownCount;         // Frequency decrease count
    uint64_t freqChangeCount;       // Total frequency changes
    
    // Power state transitions
    uint64_t powerUpCount;          // GPU power-up count
    uint64_t powerDownCount;        // GPU power-down count
    
    // Throttling
    uint32_t throttleReasons;       // Current throttle reasons
    uint64_t throttleCount;         // Throttle event count
    uint64_t throttleTimeMs;        // Time spent throttled
    
    // Performance
    uint64_t avgFreqMHz;            // Average frequency
    uint32_t powerDrawMw;           // Estimated power draw (milliwatts)
};


// MARK: - SLPC Event


struct SLPCEvent {
    uint32_t timestamp;             // Event timestamp
    uint32_t eventType;             // Event type
    uint32_t oldFreqMHz;            // Previous frequency
    uint32_t newFreqMHz;            // New frequency
    uint32_t utilization;           // GPU utilization at event
    uint32_t throttleReasons;       // Throttle reasons (if any)
};


// MARK: - IntelGuCSLPC Class


class IntelGuCSLPC : public OSObject {
    OSDeclareDefaultStructors(IntelGuCSLPC)
    
public:
    // Initialization
    bool init(IntelGuC* guc, AppleIntelTGLController* controller);
    void free() override;
    
    bool initializeSLPC();
    void shutdownSLPC();
    

    // SLPC Control

    
    // Enable/disable SLPC
    bool enable();
    bool disable();
    bool isEnabled() { return enabled; }
    
    // SLPC state
    uint32_t getState() { return state; }
    const char* getStateString();
    

    // Frequency Management

    
    // Set frequency limits
    bool setMinFrequency(uint32_t freqMHz);
    bool setMaxFrequency(uint32_t freqMHz);
    bool setEfficientFrequency(uint32_t freqMHz);
    
    // Get frequency limits
    uint32_t getMinFrequency() { return minFreqMHz; }
    uint32_t getMaxFrequency() { return maxFreqMHz; }
    uint32_t getEfficientFrequency() { return efficientFreqMHz; }
    
    // Current frequency
    uint32_t getCurrentFrequency();
    uint32_t getRequestedFrequency();
    
    // Boost frequency (temporary high performance mode)
    bool boostFrequency(uint32_t durationMs);
    bool cancelBoost();
    

    // Performance Thresholds

    
    // Set performance thresholds for frequency scaling
    bool setUpThreshold(uint32_t threshold);     // Scale up if utilization > threshold
    bool setDownThreshold(uint32_t threshold);   // Scale down if utilization < threshold
    
    uint32_t getUpThreshold() { return upThreshold; }
    uint32_t getDownThreshold() { return downThreshold; }
    

    // Efficient Frequency

    
    // Enable/disable efficient frequency mode (power saving)
    bool enableEfficientMode();
    bool disableEfficientMode();
    bool isEfficientModeEnabled() { return efficientModeEnabled; }
    

    // Task Management

    
    // Enable/disable specific SLPC tasks
    bool enableTask(SLPCTaskID taskId);
    bool disableTask(SLPCTaskID taskId);
    bool isTaskEnabled(SLPCTaskID taskId);
    

    // Statistics & Monitoring

    
    // Get statistics
    void getStatistics(SLPCStatistics* stats);
    void resetStatistics();
    
    // Update statistics (called periodically)
    void updateStatistics();
    
    // GPU utilization
    uint32_t getGPUUtilization();
    void updateUtilization(uint64_t activeNs, uint64_t totalNs);
    

    // Throttling

    
    // Get throttle status
    uint32_t getThrottleReasons();
    bool isThrottled() { return (throttleReasons != 0); }
    const char* getThrottleReasonString();
    
    // Throttle reasons (bit flags)
    enum ThrottleReason {
        THROTTLE_POWER_LIMIT      = (1 << 0),  // Power limit reached
        THROTTLE_THERMAL          = (1 << 1),  // Thermal limit
        THROTTLE_CURRENT          = (1 << 2),  // Current limit
        THROTTLE_RATL             = (1 << 3),  // Running Average Temperature Limit
        THROTTLE_VR_TDC           = (1 << 4),  // Voltage Regulator TDC
        THROTTLE_VR_THERMALERT    = (1 << 5),  // VR thermal alert
        THROTTLE_POWER_BUDGET     = (1 << 6),  // Power budget constraint
        THROTTLE_OUT_OF_SPEC      = (1 << 7),  // Out of spec operation
    };
    

    // Event History

    
    // Event history for debugging
    static const int MAX_EVENTS = 256;
    
    void recordEvent(uint32_t eventType, uint32_t oldFreq, uint32_t newFreq);
    void dumpEventHistory();
    void clearEventHistory();
    

    // Debug & Diagnostics

    
    // Dump SLPC state
    void dumpState();
    void dumpStatistics();
    
    // Force frequency (debug only, bypasses SLPC)
    bool forceFrequency(uint32_t freqMHz);
    
private:
    // Core components
    IntelGuC* guc;
    AppleIntelTGLController* controller;
    bool initialized;
    bool enabled;
    
    // SLPC state
    uint32_t state;
    uint32_t taskEnableMask;        // Which SLPC tasks are enabled
    
    // Frequency management
    uint32_t minFreqMHz;
    uint32_t maxFreqMHz;
    uint32_t currentFreqMHz;
    uint32_t requestedFreqMHz;
    uint32_t efficientFreqMHz;
    bool efficientModeEnabled;
    
    // Performance thresholds
    uint32_t upThreshold;           // Scale up if utilization > this
    uint32_t downThreshold;         // Scale down if utilization < this
    
    // Boost mode
    bool boostActive;
    IOTimerEventSource* boostTimer;
    
    // Throttling
    uint32_t throttleReasons;
    
    // Statistics
    SLPCStatistics stats;
    IOLock* statsLock;
    
    // Event history
    SLPCEvent eventHistory[MAX_EVENTS];
    uint32_t eventCount;
    uint32_t eventIndex;
    IOLock* eventLock;
    
    // Timer for periodic updates
    IOTimerEventSource* updateTimer;
    
    // Helper methods
    bool sendSLPCRequest(uint32_t param, uint32_t value);
    bool getSLPCParameter(uint32_t param, uint32_t* value);
    
    static void updateTimerFired(OSObject* owner, IOTimerEventSource* timer);
    static void boostTimerFired(OSObject* owner, IOTimerEventSource* timer);
    
    void updateFrequencyLimits();
    void checkThrottle();
    
    uint32_t freqMHzToValue(uint32_t freqMHz);
    uint32_t freqValueToMHz(uint32_t value);
};

#endif // INTEL_GUC_SLPC_H
