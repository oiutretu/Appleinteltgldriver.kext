//
//  IntelRuntimePM.cpp
//  AppleIntelTGL
//
//  Runtime Power Management Implementation
//

#include "IntelRuntimePM.h"
#include "AppleIntelTGLController.h"
#include "IntelPowerManagement.h"
#include "IntelGTPowerManagement.h"
#include <IOKit/pwr_mgt/IOPM.h>
#include <IOKit/IOLib.h>

#define super IOService
OSDefineMetaClassAndStructors(IntelRuntimePM, IOService)

// Power state definitions for IOKit
static IOPMPowerState powerStates[kPowerStateCount] = {
    // kPowerStateOff - D3
    {
        kIOPMPowerStateVersion1,                    // version
        0,                                          // capabilityFlags
        0,                                          // outputPowerCharacter
        0,                                          // inputPowerRequirement
        0,                                          // staticPower
        0,                                          // unbudgetedPower
        0,                                          // powerToAttain
        0,                                          // timeToAttain
        0,                                          // settleUpTime
        0,                                          // timeToLower
        0,                                          // settleDownTime
        0                                           // powerDomainBudget
    },
    // kPowerStateDoze - D1
    {
        kIOPMPowerStateVersion1,
        kIOPMDoze,
        kIOPMDoze,
        kIOPMDoze,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0
    },
    // kPowerStateOn - D0
    {
        kIOPMPowerStateVersion1,
        kIOPMPowerOn | kIOPMDeviceUsable,
        kIOPMPowerOn,
        kIOPMPowerOn,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0
    }
};

#pragma mark - Initialization

bool IntelRuntimePM::init(OSDictionary* dictionary) {
    if (!super::init(dictionary)) {
        return false;
    }
    
    controller = nullptr;
    displayPower = nullptr;
    gtPower = nullptr;
    pmRootDomain = nullptr;
    systemPowerNotifier = nullptr;
    powerChangeThread = nullptr;
    idleTimer = nullptr;
    pmLock = nullptr;
    
    currentState = kPowerStateOff;
    activeDomains = 0;
    currentPolicy = PM_POLICY_BALANCED;
    onBatteryPower = false;
    idleTimeoutMs = 5000;  // 5 seconds default
    lastActivityTime = 0;
    idleTimerActive = false;
    stateStartTime = 0;
    
    memset(&stats, 0, sizeof(stats));
    
    return true;
}

void IntelRuntimePM::free() {
    if (idleTimer) {
        idleTimer->cancelTimeout();
        idleTimer->release();
        idleTimer = nullptr;
    }
    
    if (systemPowerNotifier) {
        systemPowerNotifier->remove();
        systemPowerNotifier = nullptr;
    }
    
    if (pmLock) {
        IOLockFree(pmLock);
        pmLock = nullptr;
    }
    
    super::free();
}

bool IntelRuntimePM::start(IOService* provider) {
    if (!super::start(provider)) {
        return false;
    }
    
    pmLock = IOLockAlloc();
    if (!pmLock) {
        IOLog("IntelRuntimePM: Failed to allocate PM lock\n");
        return false;
    }
    
    return true;
}

void IntelRuntimePM::stop(IOService* provider) {
    stopIdleTimer();
    
    if (systemPowerNotifier) {
        systemPowerNotifier->remove();
        systemPowerNotifier = nullptr;
    }
    
    super::stop(provider);
}

bool IntelRuntimePM::initWithController(AppleIntelTGLController* ctrl) {
    if (!ctrl) {
        return false;
    }
    
    controller = ctrl;
    displayPower = ctrl->getPowerManagement();
    gtPower = ctrl->getGTPower();
    
    IOLog("IntelRuntimePM: Initialized with controller\n");
    return true;
}

bool IntelRuntimePM::registerPowerManagement() {
    // Register with IOKit power management
    PMinit();
    
    // Register as power controller
    registerPowerDriver(this, powerStates, kPowerStateCount);
    
    // Join PM tree (use controller instead of provider)
    if (controller) {
        controller->joinPMtree(this);
    }
    
    // Get root domain for system power notifications
    pmRootDomain = getPMRootDomain();
    if (pmRootDomain) {
        pmRootDomain->registerInterestedDriver(this);
    }
    
    // Start in full power state
    makeUsable();
    
    IOLog("IntelRuntimePM: Registered with IOKit PM\n");
    return true;
}

#pragma mark - IOKit Power Management

IOReturn IntelRuntimePM::setPowerState(unsigned long powerStateOrdinal,
                                       IOService* whatDevice) {
    IOLockLock(pmLock);
    
    IOLog("IntelRuntimePM: Power state change: %u -> %lu\n",
          currentState, powerStateOrdinal);
    
    IOReturn result = kIOReturnSuccess;
    
    switch (powerStateOrdinal) {
        case kPowerStateOff:
            result = transitionToOff();
            break;
            
        case kPowerStateDoze:
            result = transitionToDoze();
            break;
            
        case kPowerStateOn:
            result = transitionToOn();
            break;
            
        default:
            IOLog("IntelRuntimePM: Invalid power state %lu\n", powerStateOrdinal);
            result = kIOReturnBadArgument;
            break;
    }
    
    if (result == kIOReturnSuccess) {
        currentState = powerStateOrdinal;
    }
    
    IOLockUnlock(pmLock);
    return result;
}

unsigned long IntelRuntimePM::maxCapabilityForDomainState(IOPMPowerFlags domainState) {
    if (domainState & kIOPMPowerOn) {
        return kPowerStateOn;
    } else if (domainState & kIOPMDoze) {
        return kPowerStateDoze;
    }
    return kPowerStateOff;
}

unsigned long IntelRuntimePM::initialPowerStateForDomainState(IOPMPowerFlags domainState) {
    return kPowerStateOn;
}

#pragma mark - Power State Transitions

IOReturn IntelRuntimePM::transitionToOff() {
    IOLog("IntelRuntimePM: Transitioning to OFF (D3)\n");
    
    // Stop idle timer
    stopIdleTimer();
    
    // Suspend all domains
    if (!suspend(POWER_DOMAIN_ALL)) {
        IOLog("IntelRuntimePM: Failed to suspend all domains\n");
        stats.suspendFailures++;
        return kIOReturnError;
    }
    
    // Update statistics
    uint64_t now = mach_absolute_time();
    if (stateStartTime > 0) {
        uint64_t duration = now - stateStartTime;
        stats.totalActiveTime += duration;
    }
    stateStartTime = now;
    stats.suspendCount++;
    stats.lastSuspendTime = now;
    
    IOLog("IntelRuntimePM: Transitioned to OFF\n");
    return kIOReturnSuccess;
}

IOReturn IntelRuntimePM::transitionToDoze() {
    IOLog("IntelRuntimePM: Transitioning to DOZE (D1)\n");
    
    // Suspend display, keep GT in RC6
    if (!suspendDomain(POWER_DOMAIN_DISPLAY)) {
        return kIOReturnError;
    }
    
    // Ensure GT is in RC6
    if (gtPower && !gtPower->isInRC6()) {
        gtPower->enterRC6();
    }
    
    // Restart idle timer with longer timeout
    stopIdleTimer();
    setIdleTimeout(10000);  // 10 seconds in doze
    startIdleTimer();
    
    IOLog("IntelRuntimePM: Transitioned to DOZE\n");
    return kIOReturnSuccess;
}

IOReturn IntelRuntimePM::transitionToOn() {
    IOLog("IntelRuntimePM: Transitioning to ON (D0)\n");
    
    // Resume all domains
    if (!resume(POWER_DOMAIN_ALL)) {
        IOLog("IntelRuntimePM: Failed to resume all domains\n");
        stats.resumeFailures++;
        return kIOReturnError;
    }
    
    // Start idle timer based on policy
    setIdleTimeout(getIdleTimeoutForPolicy(currentPolicy));
    startIdleTimer();
    
    // Update statistics
    uint64_t now = mach_absolute_time();
    if (stateStartTime > 0) {
        uint64_t duration = now - stateStartTime;
        stats.totalIdleTime += duration;
    }
    stateStartTime = now;
    stats.resumeCount++;
    stats.lastResumeTime = now;
    
    IOLog("IntelRuntimePM: Transitioned to ON\n");
    return kIOReturnSuccess;
}

#pragma mark - Runtime PM Control

bool IntelRuntimePM::suspend(uint32_t domains) {
    IOLockLock(pmLock);
    
    bool success = true;
    
    if (domains & POWER_DOMAIN_DISPLAY) {
        success &= suspendDisplay();
    }
    
    if (domains & POWER_DOMAIN_GT) {
        success &= suspendGT();
    }
    
    if (success) {
        activeDomains &= ~domains;
    }
    
    IOLockUnlock(pmLock);
    return success;
}

bool IntelRuntimePM::resume(uint32_t domains) {
    IOLockLock(pmLock);
    
    bool success = true;
    
    if (domains & POWER_DOMAIN_GT) {
        success &= resumeGT();
    }
    
    if (domains & POWER_DOMAIN_DISPLAY) {
        success &= resumeDisplay();
    }
    
    if (success) {
        activeDomains |= domains;
    }
    
    IOLockUnlock(pmLock);
    return success;
}

#pragma mark - Domain Operations

bool IntelRuntimePM::suspendDisplay() {
    if (!displayPower) {
        return true;
    }
    
    IOLog("IntelRuntimePM: Suspending display\n");
    
    // Turn off display
    displayPower->setDPMSState(DPMS_OFF);
    
    return true;
}

bool IntelRuntimePM::resumeDisplay() {
    if (!displayPower) {
        return true;
    }
    
    IOLog("IntelRuntimePM: Resuming display\n");
    
    // Turn on display
    displayPower->setDPMSState(DPMS_ON);
    
    return true;
}

bool IntelRuntimePM::suspendGT() {
    if (!gtPower) {
        return true;
    }
    
    IOLog("IntelRuntimePM: Suspending GT\n");
    
    // Enter deepest RC6 state
    if (!gtPower->enterRC6()) {
        IOLog("IntelRuntimePM: Failed to enter RC6\n");
        return false;
    }
    
    // Set minimum frequency
    gtPower->setFrequency(gtPower->getMinFrequency());
    
    return true;
}

bool IntelRuntimePM::resumeGT() {
    if (!gtPower) {
        return true;
    }
    
    IOLog("IntelRuntimePM: Resuming GT\n");
    
    // Exit RC6
    if (!gtPower->exitRC6()) {
        IOLog("IntelRuntimePM: Failed to exit RC6\n");
        return false;
    }
    
    // Restore base frequency
    gtPower->setFrequency(gtPower->getBaseFrequency());
    
    return true;
}

bool IntelRuntimePM::suspendDomain(uint32_t domain) {
    return suspend(domain);
}

bool IntelRuntimePM::resumeDomain(uint32_t domain) {
    return resume(domain);
}

#pragma mark - Idle Detection

void IntelRuntimePM::startIdleTimer() {
    if (!idleTimer) {
        idleTimer = IOTimerEventSource::timerEventSource(this,
            reinterpret_cast<IOTimerEventSource::Action>(
                &IntelRuntimePM::idleTimerFired));
        
        if (!idleTimer) {
            IOLog("IntelRuntimePM: Failed to create idle timer\n");
            return;
        }
        
        getWorkLoop()->addEventSource(idleTimer);
    }
    
    // Set timeout
    idleTimer->setTimeoutMS(idleTimeoutMs);
    idleTimerActive = true;
    lastActivityTime = mach_absolute_time();
    
    IOLog("IntelRuntimePM: Idle timer started (%u ms)\n", idleTimeoutMs);
}

void IntelRuntimePM::stopIdleTimer() {
    if (idleTimer) {
        idleTimer->cancelTimeout();
        idleTimerActive = false;
    }
}

void IntelRuntimePM::resetIdleTimer() {
    if (idleTimerActive && idleTimer) {
        idleTimer->setTimeoutMS(idleTimeoutMs);
        lastActivityTime = mach_absolute_time();
    }
}

void IntelRuntimePM::idleTimerFired(OSObject* owner, IOTimerEventSource* timer) {
    IntelRuntimePM* self = OSDynamicCast(IntelRuntimePM, owner);
    if (self) {
        self->handleIdleTimeout();
    }
}

void IntelRuntimePM::handleIdleTimeout() {
    IOLog("IntelRuntimePM: Idle timeout - considering suspend\n");
    
    IOLockLock(pmLock);
    
    // Check if we should suspend based on policy
    if (shouldAggressivelySuspend()) {
        IOLog("IntelRuntimePM: Transitioning to DOZE due to idle\n");
        changePowerStateToPriv(kPowerStateDoze);
    }
    
    IOLockUnlock(pmLock);
}

void IntelRuntimePM::onActivity() {
    // Reset idle timer on activity
    resetIdleTimer();
    
    // If in doze state, wake up
    if (currentState == kPowerStateDoze) {
        IOLog("IntelRuntimePM: Activity detected, waking from DOZE\n");
        changePowerStateToPriv(kPowerStateOn);
    }
}

#pragma mark - Policy Management

void IntelRuntimePM::setPolicy(RuntimePMPolicy policy) {
    IOLockLock(pmLock);
    
    currentPolicy = policy;
    
    // Update idle timeout based on new policy
    uint32_t newTimeout = getIdleTimeoutForPolicy(policy);
    setIdleTimeout(newTimeout);
    
    IOLog("IntelRuntimePM: Policy changed to %d, timeout=%u ms\n",
          policy, newTimeout);
    
    IOLockUnlock(pmLock);
}

void IntelRuntimePM::updatePolicyForPowerSource() {
    if (currentPolicy != PM_POLICY_AUTO) {
        return;
    }
    
    // Check if on battery
    if (pmRootDomain) {
        // Use getPowerState() to check current power state
        // In kernel extensions, we can't easily query battery status,
        // so assume AC power for now
        bool nowOnBattery = false;  // TODO: Implement proper battery detection
        
        if (nowOnBattery != onBatteryPower) {
            onBatteryPower = nowOnBattery;
            
            IOLog("IntelRuntimePM: Power source changed to %s\n",
                  onBatteryPower ? "battery" : "AC");
            
            // Adjust policy
            if (onBatteryPower) {
                setPolicy(PM_POLICY_POWER_SAVE);
            } else {
                setPolicy(PM_POLICY_BALANCED);
            }
        }
    }
}

uint32_t IntelRuntimePM::getIdleTimeoutForPolicy(RuntimePMPolicy policy) {
    switch (policy) {
        case PM_POLICY_PERFORMANCE:
            return 30000;  // 30 seconds
            
        case PM_POLICY_BALANCED:
            return 5000;   // 5 seconds
            
        case PM_POLICY_POWER_SAVE:
            return 1000;   // 1 second
            
        case PM_POLICY_AUTO:
            return onBatteryPower ? 1000 : 5000;
            
        default:
            return 5000;
    }
}

bool IntelRuntimePM::shouldAggressivelySuspend() {
    // Don't suspend if we're in performance mode
    if (currentPolicy == PM_POLICY_PERFORMANCE) {
        return false;
    }
    
    // Always suspend in power save mode
    if (currentPolicy == PM_POLICY_POWER_SAVE) {
        return true;
    }
    
    // In balanced mode, suspend if on battery
    if (currentPolicy == PM_POLICY_BALANCED) {
        return onBatteryPower;
    }
    
    return true;
}

void IntelRuntimePM::setIdleTimeout(uint32_t timeoutMs) {
    idleTimeoutMs = timeoutMs;
    
    // Restart timer with new timeout if active
    if (idleTimerActive) {
        stopIdleTimer();
        startIdleTimer();
    }
}

#pragma mark - Wake Events

void IntelRuntimePM::enableWakeEvents() {
    // Enable display hot-plug wake
    if (displayPower) {
        // Enable hot-plug interrupt as wake source
    }
    
    // Enable GT wake events
    if (gtPower) {
        // Enable GT interrupts as wake source
    }
    
    IOLog("IntelRuntimePM: Wake events enabled\n");
}

void IntelRuntimePM::disableWakeEvents() {
    IOLog("IntelRuntimePM: Wake events disabled\n");
}

void IntelRuntimePM::handleWakeEvent(uint32_t source) {
    IOLog("IntelRuntimePM: Wake event from source 0x%x\n", source);
    
    // Wake from current power state
    if (currentState != kPowerStateOn) {
        changePowerStateToPriv(kPowerStateOn);
    }
}

#pragma mark - Statistics

void IntelRuntimePM::resetStats() {
    IOLockLock(pmLock);
    memset(&stats, 0, sizeof(stats));
    stateStartTime = mach_absolute_time();
    IOLockUnlock(pmLock);
    
    IOLog("IntelRuntimePM: Statistics reset\n");
}

void IntelRuntimePM::printStats() {
    IOLog("IntelRuntimePM Statistics:\n");
    IOLog("  Total idle time: %llu us\n", stats.totalIdleTime);
    IOLog("  Total active time: %llu us\n", stats.totalActiveTime);
    IOLog("  Suspend count: %u\n", stats.suspendCount);
    IOLog("  Resume count: %u\n", stats.resumeCount);
    IOLog("  Suspend failures: %u\n", stats.suspendFailures);
    IOLog("  Resume failures: %u\n", stats.resumeFailures);
    IOLog("  Last suspend: %llu\n", stats.lastSuspendTime);
    IOLog("  Last resume: %llu\n", stats.lastResumeTime);
    
    // Calculate uptime percentage
    uint64_t totalTime = stats.totalIdleTime + stats.totalActiveTime;
    if (totalTime > 0) {
        uint32_t activePercent = (stats.totalActiveTime * 100) / totalTime;
        IOLog("  Active time: %u%%\n", activePercent);
    }
}

#pragma mark - System Power Notifications

IOReturn IntelRuntimePM::systemWillSleep() {
    IOLog("IntelRuntimePM: System will sleep\n");
    
    // Stop idle timer
    stopIdleTimer();
    
    // Suspend all domains
    suspend(POWER_DOMAIN_ALL);
    
    return kIOReturnSuccess;
}

IOReturn IntelRuntimePM::systemDidWake() {
    IOLog("IntelRuntimePM: System did wake\n");
    
    // Resume all domains
    resume(POWER_DOMAIN_ALL);
    
    // Restart idle timer
    startIdleTimer();
    
    // Update policy for power source
    updatePolicyForPowerSource();
    
    return kIOReturnSuccess;
}

IOReturn IntelRuntimePM::systemWillPowerOff() {
    IOLog("IntelRuntimePM: System will power off\n");
    
    // Stop everything
    stopIdleTimer();
    suspend(POWER_DOMAIN_ALL);
    
    return kIOReturnSuccess;
}
