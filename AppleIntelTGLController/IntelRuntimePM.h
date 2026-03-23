//
//  IntelRuntimePM.h
//  AppleIntelTGL
//
//  Runtime Power Management Integration
//  Coordinates display and GT power with IOKit PM framework
//

#ifndef IntelRuntimePM_h
#define IntelRuntimePM_h

#include <IOKit/IOService.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/IOTimerEventSource.h>

class AppleIntelTGLController;
class IntelPowerManagement;
class IntelGTPowerManagement;

// Power states for IOKit power management
enum {
    kPowerStateOff = 0,     // D3 - Everything off
    kPowerStateDoze = 1,    // D1 - Light sleep (display off, GT RC6)
    kPowerStateOn = 2,      // D0 - Full power
    kPowerStateCount = 3
};

// Runtime PM policies
enum RuntimePMPolicy {
    PM_POLICY_PERFORMANCE = 0,  // Minimal power saving, max performance
    PM_POLICY_BALANCED = 1,     // Balance power and performance (default)
    PM_POLICY_POWER_SAVE = 2,   // Aggressive power saving
    PM_POLICY_AUTO = 3          // System decides based on power source
};

// Power domain flags
enum PowerDomainFlags {
    POWER_DOMAIN_DISPLAY = (1 << 0),
    POWER_DOMAIN_GT = (1 << 1),
    POWER_DOMAIN_AUDIO = (1 << 2),
    POWER_DOMAIN_ALL = 0x7
};

// Runtime PM statistics
struct RuntimePMStats {
    uint64_t totalIdleTime;          // Total time in idle states (us)
    uint64_t totalActiveTime;        // Total time in active states (us)
    uint32_t suspendCount;           // Number of runtime suspends
    uint32_t resumeCount;            // Number of runtime resumes
    uint64_t lastSuspendTime;        // Timestamp of last suspend
    uint64_t lastResumeTime;         // Timestamp of last resume
    uint32_t suspendFailures;        // Failed suspend attempts
    uint32_t resumeFailures;         // Failed resume attempts
};

class IntelRuntimePM : public IOService {
    OSDeclareDefaultStructors(IntelRuntimePM)
    
public:
    // Initialization
    virtual bool init(OSDictionary* dictionary = nullptr) override;
    virtual void free() override;
    virtual bool start(IOService* provider) override;
    virtual void stop(IOService* provider) override;
    
    // Setup
    bool initWithController(AppleIntelTGLController* controller);
    bool registerPowerManagement();
    
    // IOKit power management
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal,
                                   IOService* whatDevice) override;
    virtual unsigned long maxCapabilityForDomainState(IOPMPowerFlags domainState) override;
    virtual unsigned long initialPowerStateForDomainState(IOPMPowerFlags domainState) override;
    
    // Runtime PM control
    bool suspend(uint32_t domains = POWER_DOMAIN_ALL);
    bool resume(uint32_t domains = POWER_DOMAIN_ALL);
    bool isActive() const { return currentState == kPowerStateOn; }
    bool isSuspended() const { return currentState == kPowerStateOff; }
    
    // Idle detection
    void startIdleTimer();
    void stopIdleTimer();
    void resetIdleTimer();
    void onActivity();  // Called when GPU/display activity detected
    
    // Policy management
    void setPolicy(RuntimePMPolicy policy);
    RuntimePMPolicy getPolicy() const { return currentPolicy; }
    void updatePolicyForPowerSource();  // Adjust based on AC/battery
    
    // Power domain control
    bool suspendDomain(uint32_t domain);
    bool resumeDomain(uint32_t domain);
    uint32_t getActiveDomains() const { return activeDomains; }
    
    // Wake events
    void enableWakeEvents();
    void disableWakeEvents();
    void handleWakeEvent(uint32_t source);
    
    // Statistics
    const RuntimePMStats& getStats() const { return stats; }
    void resetStats();
    void printStats();
    
    // Accessors
    AppleIntelTGLController* getController() const { return controller; }
    uint32_t getIdleTimeout() const { return idleTimeoutMs; }
    void setIdleTimeout(uint32_t timeoutMs);
    
private:
    // Power state transitions
    IOReturn transitionToOff();
    IOReturn transitionToDoze();
    IOReturn transitionToOn();
    
    // Domain operations
    bool suspendDisplay();
    bool resumeDisplay();
    bool suspendGT();
    bool resumeGT();
    
    // Idle timer callback
    static void idleTimerFired(OSObject* owner, IOTimerEventSource* timer);
    void handleIdleTimeout();
    
    // System power notifications
    IOReturn systemWillSleep();
    IOReturn systemDidWake();
    IOReturn systemWillPowerOff();
    
    // Policy helpers
    uint32_t getIdleTimeoutForPolicy(RuntimePMPolicy policy);
    bool shouldAggressivelySuspend();
    
    // Member variables
    AppleIntelTGLController* controller;
    IntelPowerManagement* displayPower;
    IntelGTPowerManagement* gtPower;
    
    // IOKit PM
    IOPMrootDomain* pmRootDomain;
    IONotifier* systemPowerNotifier;
    thread_call_t powerChangeThread;
    
    // State tracking
    uint32_t currentState;
    uint32_t activeDomains;
    RuntimePMPolicy currentPolicy;
    bool onBatteryPower;
    
    // Idle detection
    IOTimerEventSource* idleTimer;
    uint32_t idleTimeoutMs;
    uint64_t lastActivityTime;
    bool idleTimerActive;
    
    // Statistics
    RuntimePMStats stats;
    uint64_t stateStartTime;
    
    // Synchronization
    IOLock* pmLock;
};

#endif /* IntelRuntimePM_h */
