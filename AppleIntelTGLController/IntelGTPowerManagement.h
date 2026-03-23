//
//  IntelGTPowerManagement.h
// Graphics Driver
//
//  GT (Graphics Technology) Power Management




//
//  Week 17 - Phase 3: Power Management
//

#ifndef IntelGTPowerManagement_h
#define IntelGTPowerManagement_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>  // Fixed: IOLock.h -> IOLocks.h
#include <IOKit/IOTimerEventSource.h>

// Forward declarations
class IntelUncore;

// RC6 Render Standby States
enum IntelRC6State {
    RC_STATE_RC0 = 0,      // Fully powered (rendering active)
    RC_STATE_RC1,          // Light sleep (quick wake, <1ms)
    RC_STATE_RC6,          // Deep sleep (context saved, ~5ms wake)
    RC_STATE_RC6p,         // Package C6 (deeper sleep)
    RC_STATE_RC6pp         // Package C6+ (deepest sleep)
};

// RP Frequency States
enum IntelRPState {
    RP_STATE_RPn = 0,      // Minimum frequency (most efficient)
    RP_STATE_RP1,          // Base frequency (guaranteed)
    RP_STATE_RP0           // Maximum frequency (turbo)
};

// Clock Gating Units
enum ClockGatingUnit {
    CLOCK_GATE_RENDER = (1 << 0),
    CLOCK_GATE_MEDIA = (1 << 1),
    CLOCK_GATE_BLITTER = (1 << 2),
    CLOCK_GATE_VIDEO = (1 << 3),
    CLOCK_GATE_ALL = 0xFFFFFFFF
};

// Power Context Structure
struct IntelGTPowerContext {
    uint32_t* contextData;      // Saved GPU context
    size_t contextSize;         // Size in bytes
    bool isValid;               // Context valid flag
    uint64_t savedTimestamp;    // When context was saved
};

// Frequency Information
struct FrequencyInfo {
    uint32_t minFrequency;      // RPn (MHz)
    uint32_t baseFrequency;     // RP1 (MHz)
    uint32_t maxFrequency;      // RP0 (MHz)
    uint32_t currentFrequency;  // Current (MHz)
    uint32_t requestedFrequency; // Requested (MHz)
    bool turboEnabled;          // Turbo available
};

// RC6 Statistics
struct RC6Statistics {
    uint64_t rc6_residency;     // Time in RC6 (microseconds)
    uint64_t rc6p_residency;    // Time in RC6p
    uint64_t rc6pp_residency;   // Time in RC6pp
    uint32_t rc6_entries;       // Number of RC6 entries
    uint32_t rc6_exits;         // Number of RC6 exits
    uint64_t lastUpdateTime;    // Last statistics update
};

// Power Management Statistics
struct PowerStatistics {
    RC6Statistics rc6Stats;
    uint32_t frequencyChanges;  // Number of frequency transitions
    uint32_t clockGatingEvents; // Number of clock gate operations
    uint64_t totalIdleTime;     // Total time GPU idle (us)
    uint64_t totalActiveTime;   // Total time GPU active (us)
};

class IntelGTPowerManagement : public OSObject {
    OSDeclareDefaultStructors(IntelGTPowerManagement)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    
    bool initWithUncore(IntelUncore* uncore);
    bool readHardwareCapabilities();
    
    // RC6 Render Standby Management
    bool enableRC6();
    bool disableRC6();
    bool isRC6Enabled();
    bool isInRC6();  // Check if currently in RC6 state
    IntelRC6State getCurrentRC6State();
    bool enterRC6();
    bool exitRC6();
    
    // Power Context Management
    bool allocatePowerContext(size_t size);
    bool savePowerContext();
    bool restorePowerContext();
    void freePowerContext();
    
    // Frequency Scaling (RP States)
    bool setFrequency(uint32_t frequencyMHz);
    uint32_t getCurrentFrequency();
    uint32_t getMinFrequency();     // RPn
    uint32_t getBaseFrequency();    // RP1
    uint32_t getMaxFrequency();     // RP0
    bool enableTurbo();
    bool disableTurbo();
    bool isTurboEnabled();
    
    // Frequency Management
    bool scaleFrequencyUp();
    bool scaleFrequencyDown();
    bool setFrequencyRatio(uint32_t ratio);  // 0-31
    uint32_t getFrequencyRatio();
    
    // Clock Gating
    bool enableClockGating(ClockGatingUnit units);
    bool disableClockGating(ClockGatingUnit units);
    bool isClockGatingEnabled(ClockGatingUnit unit);
    
    bool gateRenderClocks(bool enable);
    bool gateMediaClocks(bool enable);
    bool gateBlitterClocks(bool enable);
    bool gateVideoClocks(bool enable);
    
    // Workload Detection
    void detectIdleState();
    void detectWorkloadLevel();
    void adjustFrequencyForWorkload();
    void scheduleIdleCheck(uint32_t delayMs);
    
    // Idle Management
    bool isGPUIdle();
    void setIdleThreshold(uint32_t thresholdMs);
    uint32_t getIdleThreshold();
    
    // Thermal Management
    uint32_t getCurrentTemperature();
    bool isThrottlingRequired();
    void applyThermalThrottling();
    void clearThermalThrottling();
    
    // Statistics
    void getRC6Statistics(RC6Statistics* stats);
    void getPowerStatistics(PowerStatistics* stats);
    void resetStatistics();
    void updateStatistics();
    
    // Frequency Information
    void getFrequencyInfo(FrequencyInfo* info);
    
    // Power State Management
    void onSystemWake();
    void onSystemSleep();
    void onDisplayPowerChange(bool displayOn);
    void onGPUActivity();
    void onGPUIdle();
    
private:
    // Hardware Access
    IntelUncore* uncore;
    IOLock* lock;
    IOTimerEventSource* idleTimer;
    
    // RC6 State
    bool rc6Enabled;
    IntelRC6State currentRC6State;
    IntelGTPowerContext powerContext;
    uint32_t rc6IdleThreshold;      // Milliseconds
    
    // Frequency State
    FrequencyInfo freqInfo;
    uint32_t currentFreqRatio;      // 0-31
    bool autoFreqScaling;
    
    // Clock Gating State
    uint32_t clockGatingEnabled;    // Bitmask of enabled units
    
    // Workload Detection
    uint64_t lastActivityTime;
    uint64_t lastIdleTime;
    bool gpuIdle;
    uint32_t workloadLevel;         // 0-100%
    
    // Statistics
    PowerStatistics statistics;
    
    // Thermal State
    uint32_t temperatureThreshold;  // Celsius
    bool thermalThrottling;
    
    // Hardware Methods
    bool readFrequencyLimits();
    bool writeFrequencyRequest(uint32_t ratio);
    bool readCurrentFrequency(uint32_t* frequencyMHz);
    
    bool writeRC6Control(bool enable);
    bool readRC6State(IntelRC6State* state);
    bool readRC6Residency(uint64_t* residency);
    
    bool writeClockGateControl(ClockGatingUnit unit, bool enable);
    bool readClockGateStatus(ClockGatingUnit unit, bool* enabled);
    
    bool readTemperature(uint32_t* tempCelsius);
    
    // Helper Methods
    uint32_t ratioToFrequency(uint32_t ratio);
    uint32_t frequencyToRatio(uint32_t frequencyMHz);
    uint64_t getCurrentTimeUs();
    void updateIdleStatistics();
    void updateActiveStatistics();
    
    // Timer Callback
    static void idleCheckCallback(OSObject* owner, IOTimerEventSource* timer);
    void handleIdleCheck();
};

#endif /* IntelGTPowerManagement_h */
