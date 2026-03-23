//
//  IntelGTPowerManagement.cpp
// Graphics Driver
//
//  GT Power Management Implementation
//

#include "IntelGTPowerManagement.h"
#include "intel_gt_power_regs.h"

// Undefine conflicting macros before including IntelUncore.h
#undef FORCEWAKE_RENDER
#undef FORCEWAKE_GT
#undef FORCEWAKE_MEDIA

#include "IntelUncore.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelGTPowerManagement, OSObject)

// MARK: - Initialization

bool IntelGTPowerManagement::init() {
    if (!super::init()) {
        return false;
    }
    
    // Initialize member variables
    uncore = nullptr;
    lock = nullptr;
    idleTimer = nullptr;
    
    rc6Enabled = false;
    currentRC6State = RC_STATE_RC0;
    rc6IdleThreshold = 100;  // 100ms default
    
    memset(&powerContext, 0, sizeof(powerContext));
    memset(&freqInfo, 0, sizeof(freqInfo));
    memset(&statistics, 0, sizeof(statistics));
    
    currentFreqRatio = 0;
    autoFreqScaling = true;
    clockGatingEnabled = 0;
    
    lastActivityTime = 0;
    lastIdleTime = 0;
    gpuIdle = true;
    workloadLevel = 0;
    
    temperatureThreshold = 90;  // 90degC
    thermalThrottling = false;
    
    IOLog("IntelGTPowerManagement: Initialized\n");
    return true;
}

void IntelGTPowerManagement::free() {
    if (idleTimer) {
        idleTimer->cancelTimeout();
        idleTimer->release();
        idleTimer = nullptr;
    }
    
    if (lock) {
        IOLockFree(lock);
        lock = nullptr;
    }
    
    freePowerContext();
    
    IOLog("IntelGTPowerManagement: Freed\n");
    super::free();
}

bool IntelGTPowerManagement::initWithUncore(IntelUncore* uncoreInstance) {
    if (!uncoreInstance) {
        IOLog("IntelGTPowerManagement: Invalid uncore instance\n");
        return false;
    }
    
    uncore = uncoreInstance;
    // Note: IntelUncore doesn't have retain, it's not an OSObject
    
    // Create lock for thread safety
    lock = IOLockAlloc();
    if (!lock) {
        IOLog("IntelGTPowerManagement: Failed to allocate lock\n");
        return false;
    }
    
    // Read hardware capabilities
    if (!readHardwareCapabilities()) {
        IOLog("IntelGTPowerManagement: Failed to read hardware capabilities\n");
        return false;
    }
    
    IOLog("IntelGTPowerManagement: Initialized with uncore\n");
    IOLog("  Min Frequency (RPn): %u MHz\n", freqInfo.minFrequency);
    IOLog("  Base Frequency (RP1): %u MHz\n", freqInfo.baseFrequency);
    IOLog("  Max Frequency (RP0): %u MHz\n", freqInfo.maxFrequency);
    IOLog("  Turbo: %s\n", freqInfo.turboEnabled ? "Available" : "Not available");
    
    return true;
}

bool IntelGTPowerManagement::readHardwareCapabilities() {
    // Read actual hardware frequency limits from RP_STATE_CAP
    if (!readFrequencyLimits()) {
        // Fallback to typical Tiger Lake values
        IOLog("IntelGTPowerManagement: Failed to read frequency limits, using defaults\n");
        freqInfo.minFrequency = 300;     // 300 MHz (RPn)
        freqInfo.baseFrequency = 1100;   // 1.1 GHz (RP1)
        freqInfo.maxFrequency = 1300;    // 1.3 GHz (RP0)
    }
    
    freqInfo.turboEnabled = true;
    freqInfo.currentFrequency = freqInfo.baseFrequency;
    freqInfo.requestedFrequency = freqInfo.baseFrequency;
    
    // Calculate default frequency ratio (RP1)
    currentFreqRatio = frequencyToRatio(freqInfo.baseFrequency);
    
    // Enable RP (frequency scaling) in hardware
    uint32_t rp_control = READ_RP_CONTROL(uncore);
    rp_control |= GEN6_RP_ENABLE;
    rp_control |= GEN6_RP_UP_BUSY_AVG;    // Up on busy average
    rp_control |= GEN6_RP_DOWN_IDLE_AVG;  // Down on idle average
    WRITE_RP_CONTROL(uncore, rp_control);
    
    IOLog("IntelGTPowerManagement: RP (frequency scaling) enabled\n");
    
    return true;
}

// MARK: - RC6 Render Standby Management

bool IntelGTPowerManagement::enableRC6() {
    IOLockLock(lock);
    
    IOLog("IntelGTPowerManagement: Enabling RC6\n");
    
    // Allocate power context if needed
    if (!powerContext.isValid) {
        if (!allocatePowerContext(4 * 1024 * 1024)) {  // 4MB context
            IOLog("IntelGTPowerManagement: Failed to allocate power context\n");
            IOLockUnlock(lock);
            return false;
        }
    }
    
    // Write RC6 control register
    if (!writeRC6Control(true)) {
        IOLog("IntelGTPowerManagement: Failed to enable RC6\n");
        IOLockUnlock(lock);
        return false;
    }
    
    rc6Enabled = true;
    
    IOLog("IntelGTPowerManagement: RC6 enabled (threshold: %u ms)\n", rc6IdleThreshold);
    
    IOLockUnlock(lock);
    return true;
}

bool IntelGTPowerManagement::disableRC6() {
    IOLockLock(lock);
    
    IOLog("IntelGTPowerManagement: Disabling RC6\n");
    
    // Exit RC6 if currently in it
    if (currentRC6State != RC_STATE_RC0) {
        exitRC6();
    }
    
    // Write RC6 control register
    if (!writeRC6Control(false)) {
        IOLog("IntelGTPowerManagement: Failed to disable RC6\n");
        IOLockUnlock(lock);
        return false;
    }
    
    rc6Enabled = false;
    
    IOLog("IntelGTPowerManagement: RC6 disabled\n");
    
    IOLockUnlock(lock);
    return true;
}

bool IntelGTPowerManagement::isRC6Enabled() {
    IOLockLock(lock);
    bool enabled = rc6Enabled;
    IOLockUnlock(lock);
    return enabled;
}

bool IntelGTPowerManagement::isInRC6() {
    IntelRC6State state = getCurrentRC6State();
    return (state == RC_STATE_RC6 || state == RC_STATE_RC6p || state == RC_STATE_RC6pp);
}

IntelRC6State IntelGTPowerManagement::getCurrentRC6State() {
    IOLockLock(lock);
    IntelRC6State state = currentRC6State;
    IOLockUnlock(lock);
    return state;
}

bool IntelGTPowerManagement::enterRC6() {
    IOLockLock(lock);
    
    if (!rc6Enabled) {
        IOLockUnlock(lock);
        return false;
    }
    
    if (currentRC6State != RC_STATE_RC0) {
        IOLockUnlock(lock);
        return true;  // Already in RC6
    }
    
    IOLog("IntelGTPowerManagement: Entering RC6\n");
    
    // Save power context
    if (!savePowerContext()) {
        IOLog("IntelGTPowerManagement: Failed to save context\n");
        IOLockUnlock(lock);
        return false;
    }
    
    // Hardware will automatically enter RC6 when idle
    // We just track the state here
    currentRC6State = RC_STATE_RC6;
    statistics.rc6Stats.rc6_entries++;
    
    updateIdleStatistics();
    
    IOLockUnlock(lock);
    return true;
}

bool IntelGTPowerManagement::exitRC6() {
    IOLockLock(lock);
    
    if (currentRC6State == RC_STATE_RC0) {
        IOLockUnlock(lock);
        return true;  // Already active
    }
    
    IOLog("IntelGTPowerManagement: Exiting RC6\n");
    
    // Restore power context
    if (!restorePowerContext()) {
        IOLog("IntelGTPowerManagement: Failed to restore context\n");
        IOLockUnlock(lock);
        return false;
    }
    
    currentRC6State = RC_STATE_RC0;
    statistics.rc6Stats.rc6_exits++;
    
    updateActiveStatistics();
    
    IOLockUnlock(lock);
    return true;
}

// MARK: - Power Context Management

bool IntelGTPowerManagement::allocatePowerContext(size_t size) {
    if (powerContext.isValid) {
        freePowerContext();
    }
    
    powerContext.contextData = (uint32_t*)IOMalloc(size);
    if (!powerContext.contextData) {
        IOLog("IntelGTPowerManagement: Failed to allocate context (%zu bytes)\n", size);
        return false;
    }
    
    powerContext.contextSize = size;
    powerContext.isValid = false;  // Not saved yet
    powerContext.savedTimestamp = 0;
    
    IOLog("IntelGTPowerManagement: Allocated power context (%zu bytes)\n", size);
    return true;
}

bool IntelGTPowerManagement::savePowerContext() {
    if (!powerContext.contextData) {
        return false;
    }
    
    // TODO: Save actual GPU context registers
    // For now, just mark as valid
    
    powerContext.isValid = true;
    powerContext.savedTimestamp = getCurrentTimeUs();
    
    IOLog("IntelGTPowerManagement: Power context saved\n");
    return true;
}

bool IntelGTPowerManagement::restorePowerContext() {
    if (!powerContext.isValid) {
        IOLog("IntelGTPowerManagement: No valid context to restore\n");
        return false;
    }
    
    // TODO: Restore actual GPU context registers
    // For now, just mark as used
    
    uint64_t sleepTime = getCurrentTimeUs() - powerContext.savedTimestamp;
    IOLog("IntelGTPowerManagement: Power context restored (slept %llu us)\n", sleepTime);
    
    return true;
}

void IntelGTPowerManagement::freePowerContext() {
    if (powerContext.contextData) {
        IOFree(powerContext.contextData, powerContext.contextSize);
        powerContext.contextData = nullptr;
    }
    
    powerContext.contextSize = 0;
    powerContext.isValid = false;
    powerContext.savedTimestamp = 0;
}

// MARK: - Frequency Scaling

bool IntelGTPowerManagement::setFrequency(uint32_t frequencyMHz) {
    IOLockLock(lock);
    
    // Clamp to hardware limits
    if (frequencyMHz < freqInfo.minFrequency) {
        frequencyMHz = freqInfo.minFrequency;
    }
    if (frequencyMHz > freqInfo.maxFrequency) {
        frequencyMHz = freqInfo.maxFrequency;
    }
    
    uint32_t ratio = frequencyToRatio(frequencyMHz);
    
    IOLog("IntelGTPowerManagement: Setting frequency %u MHz (ratio %u)\n", 
          frequencyMHz, ratio);
    
    if (!writeFrequencyRequest(ratio)) {
        IOLog("IntelGTPowerManagement: Failed to set frequency\n");
        IOLockUnlock(lock);
        return false;
    }
    
    freqInfo.requestedFrequency = frequencyMHz;
    currentFreqRatio = ratio;
    statistics.frequencyChanges++;
    
    IOLockUnlock(lock);
    return true;
}

uint32_t IntelGTPowerManagement::getCurrentFrequency() {
    IOLockLock(lock);
    uint32_t freq = 0;
    readCurrentFrequency(&freq);
    freqInfo.currentFrequency = freq;
    IOLockUnlock(lock);
    return freq;
}

uint32_t IntelGTPowerManagement::getMinFrequency() {
    return freqInfo.minFrequency;
}

uint32_t IntelGTPowerManagement::getBaseFrequency() {
    return freqInfo.baseFrequency;
}

uint32_t IntelGTPowerManagement::getMaxFrequency() {
    return freqInfo.maxFrequency;
}

bool IntelGTPowerManagement::enableTurbo() {
    IOLockLock(lock);
    
    if (!freqInfo.turboEnabled) {
        IOLog("IntelGTPowerManagement: Turbo not available on this hardware\n");
        IOLockUnlock(lock);
        return false;
    }
    
    IOLog("IntelGTPowerManagement: Enabling turbo boost\n");
    
    // TODO: Write turbo enable to hardware
    
    IOLockUnlock(lock);
    return true;
}

bool IntelGTPowerManagement::disableTurbo() {
    IOLockLock(lock);
    IOLog("IntelGTPowerManagement: Disabling turbo boost\n");
    
    // TODO: Write turbo disable to hardware
    
    IOLockUnlock(lock);
    return true;
}

bool IntelGTPowerManagement::isTurboEnabled() {
    return freqInfo.turboEnabled;
}

bool IntelGTPowerManagement::scaleFrequencyUp() {
    uint32_t current = getCurrentFrequency();
    uint32_t target = current + 100;  // +100 MHz
    
    if (target > freqInfo.maxFrequency) {
        target = freqInfo.maxFrequency;
    }
    
    return setFrequency(target);
}

bool IntelGTPowerManagement::scaleFrequencyDown() {
    uint32_t current = getCurrentFrequency();
    uint32_t target = current - 100;  // -100 MHz
    
    if (target < freqInfo.minFrequency) {
        target = freqInfo.minFrequency;
    }
    
    return setFrequency(target);
}

bool IntelGTPowerManagement::setFrequencyRatio(uint32_t ratio) {
    if (ratio > 31) {
        ratio = 31;
    }
    
    return writeFrequencyRequest(ratio);
}

uint32_t IntelGTPowerManagement::getFrequencyRatio() {
    IOLockLock(lock);
    uint32_t ratio = currentFreqRatio;
    IOLockUnlock(lock);
    return ratio;
}

// MARK: - Clock Gating

bool IntelGTPowerManagement::enableClockGating(ClockGatingUnit units) {
    IOLockLock(lock);
    
    IOLog("IntelGTPowerManagement: Enabling clock gating (units: 0x%x)\n", units);
    
    bool success = true;
    
    if (units & CLOCK_GATE_RENDER) {
        success &= gateRenderClocks(true);
    }
    if (units & CLOCK_GATE_MEDIA) {
        success &= gateMediaClocks(true);
    }
    if (units & CLOCK_GATE_BLITTER) {
        success &= gateBlitterClocks(true);
    }
    if (units & CLOCK_GATE_VIDEO) {
        success &= gateVideoClocks(true);
    }
    
    if (success) {
        clockGatingEnabled |= units;
        statistics.clockGatingEvents++;
    }
    
    IOLockUnlock(lock);
    return success;
}

bool IntelGTPowerManagement::disableClockGating(ClockGatingUnit units) {
    IOLockLock(lock);
    
    IOLog("IntelGTPowerManagement: Disabling clock gating (units: 0x%x)\n", units);
    
    bool success = true;
    
    if (units & CLOCK_GATE_RENDER) {
        success &= gateRenderClocks(false);
    }
    if (units & CLOCK_GATE_MEDIA) {
        success &= gateMediaClocks(false);
    }
    if (units & CLOCK_GATE_BLITTER) {
        success &= gateBlitterClocks(false);
    }
    if (units & CLOCK_GATE_VIDEO) {
        success &= gateVideoClocks(false);
    }
    
    if (success) {
        clockGatingEnabled &= ~units;
    }
    
    IOLockUnlock(lock);
    return success;
}

bool IntelGTPowerManagement::isClockGatingEnabled(ClockGatingUnit unit) {
    IOLockLock(lock);
    bool enabled = (clockGatingEnabled & unit) != 0;
    IOLockUnlock(lock);
    return enabled;
}

bool IntelGTPowerManagement::gateRenderClocks(bool enable) {
    // TODO: Write render clock gating registers
    IOLog("IntelGTPowerManagement: Render clock gating %s\n", enable ? "enabled" : "disabled");
    return true;
}

bool IntelGTPowerManagement::gateMediaClocks(bool enable) {
    // TODO: Write media clock gating registers
    IOLog("IntelGTPowerManagement: Media clock gating %s\n", enable ? "enabled" : "disabled");
    return true;
}

bool IntelGTPowerManagement::gateBlitterClocks(bool enable) {
    // TODO: Write blitter clock gating registers
    IOLog("IntelGTPowerManagement: Blitter clock gating %s\n", enable ? "enabled" : "disabled");
    return true;
}

bool IntelGTPowerManagement::gateVideoClocks(bool enable) {
    // TODO: Write video clock gating registers
    IOLog("IntelGTPowerManagement: Video clock gating %s\n", enable ? "enabled" : "disabled");
    return true;
}

// MARK: - Workload Detection

void IntelGTPowerManagement::detectIdleState() {
    IOLockLock(lock);
    
    uint64_t now = getCurrentTimeUs();
    uint64_t idleTime = now - lastActivityTime;
    
    if (idleTime > (rc6IdleThreshold * 1000)) {
        if (!gpuIdle) {
            gpuIdle = true;
            lastIdleTime = now;
            
            // Enter RC6 if enabled
            if (rc6Enabled && currentRC6State == RC_STATE_RC0) {
                enterRC6();
            }
            
            // Scale frequency down
            if (autoFreqScaling) {
                setFrequency(freqInfo.minFrequency);
            }
        }
    }
    
    IOLockUnlock(lock);
}

void IntelGTPowerManagement::detectWorkloadLevel() {
    // TODO: Analyze GPU usage to determine workload
    // For now, simple binary: idle or active
    
    IOLockLock(lock);
    workloadLevel = gpuIdle ? 0 : 100;
    IOLockUnlock(lock);
}

void IntelGTPowerManagement::adjustFrequencyForWorkload() {
    IOLockLock(lock);
    
    if (!autoFreqScaling) {
        IOLockUnlock(lock);
        return;
    }
    
    uint32_t targetFreq;
    
    if (workloadLevel == 0) {
        // Idle: minimum frequency
        targetFreq = freqInfo.minFrequency;
    } else if (workloadLevel < 30) {
        // Light load: base frequency
        targetFreq = freqInfo.baseFrequency;
    } else if (workloadLevel < 70) {
        // Medium load: between base and max
        targetFreq = (freqInfo.baseFrequency + freqInfo.maxFrequency) / 2;
    } else {
        // Heavy load: maximum frequency
        targetFreq = freqInfo.maxFrequency;
    }
    
    if (targetFreq != freqInfo.requestedFrequency) {
        IOLockUnlock(lock);
        setFrequency(targetFreq);
    } else {
        IOLockUnlock(lock);
    }
}

void IntelGTPowerManagement::scheduleIdleCheck(uint32_t delayMs) {
    if (!idleTimer) {
        return;
    }
    
    idleTimer->setTimeoutMS(delayMs);
}

// MARK: - Idle Management

bool IntelGTPowerManagement::isGPUIdle() {
    IOLockLock(lock);
    bool idle = gpuIdle;
    IOLockUnlock(lock);
    return idle;
}

void IntelGTPowerManagement::setIdleThreshold(uint32_t thresholdMs) {
    IOLockLock(lock);
    rc6IdleThreshold = thresholdMs;
    IOLog("IntelGTPowerManagement: Idle threshold set to %u ms\n", thresholdMs);
    IOLockUnlock(lock);
}

uint32_t IntelGTPowerManagement::getIdleThreshold() {
    IOLockLock(lock);
    uint32_t threshold = rc6IdleThreshold;
    IOLockUnlock(lock);
    return threshold;
}

// MARK: - Thermal Management

uint32_t IntelGTPowerManagement::getCurrentTemperature() {
    uint32_t temp = 0;
    readTemperature(&temp);
    return temp;
}

bool IntelGTPowerManagement::isThrottlingRequired() {
    uint32_t temp = getCurrentTemperature();
    return temp >= temperatureThreshold;
}

void IntelGTPowerManagement::applyThermalThrottling() {
    IOLockLock(lock);
    
    if (thermalThrottling) {
        IOLockUnlock(lock);
        return;
    }
    
    IOLog("IntelGTPowerManagement: Applying thermal throttling\n");
    
    // Reduce frequency to base or below
    uint32_t targetFreq = freqInfo.baseFrequency * 80 / 100;  // 80% of base
    
    IOLockUnlock(lock);
    setFrequency(targetFreq);
    
    IOLockLock(lock);
    thermalThrottling = true;
    IOLockUnlock(lock);
}

void IntelGTPowerManagement::clearThermalThrottling() {
    IOLockLock(lock);
    
    if (!thermalThrottling) {
        IOLockUnlock(lock);
        return;
    }
    
    IOLog("IntelGTPowerManagement: Clearing thermal throttling\n");
    thermalThrottling = false;
    
    IOLockUnlock(lock);
}

// MARK: - Statistics

void IntelGTPowerManagement::getRC6Statistics(RC6Statistics* stats) {
    if (!stats) return;
    
    IOLockLock(lock);
    updateStatistics();
    memcpy(stats, &statistics.rc6Stats, sizeof(RC6Statistics));
    IOLockUnlock(lock);
}

void IntelGTPowerManagement::getPowerStatistics(PowerStatistics* stats) {
    if (!stats) return;
    
    IOLockLock(lock);
    updateStatistics();
    memcpy(stats, &statistics, sizeof(PowerStatistics));
    IOLockUnlock(lock);
}

void IntelGTPowerManagement::resetStatistics() {
    IOLockLock(lock);
    memset(&statistics, 0, sizeof(statistics));
    statistics.rc6Stats.lastUpdateTime = getCurrentTimeUs();
    IOLog("IntelGTPowerManagement: Statistics reset\n");
    IOLockUnlock(lock);
}

void IntelGTPowerManagement::updateStatistics() {
    // Read RC6 residency from hardware
    uint64_t residency = 0;
    if (readRC6Residency(&residency)) {
        statistics.rc6Stats.rc6_residency = residency;
    }
    
    statistics.rc6Stats.lastUpdateTime = getCurrentTimeUs();
}

void IntelGTPowerManagement::getFrequencyInfo(FrequencyInfo* info) {
    if (!info) return;
    
    IOLockLock(lock);
    memcpy(info, &freqInfo, sizeof(FrequencyInfo));
    IOLockUnlock(lock);
}

// MARK: - Power State Management

void IntelGTPowerManagement::onSystemWake() {
    IOLog("IntelGTPowerManagement: System wake\n");
    
    // Exit RC6
    if (currentRC6State != RC_STATE_RC0) {
        exitRC6();
    }
    
    // Restore frequency to base
    setFrequency(freqInfo.baseFrequency);
}

void IntelGTPowerManagement::onSystemSleep() {
    IOLog("IntelGTPowerManagement: System sleep\n");
    
    // Save context and enter RC6
    if (rc6Enabled) {
        enterRC6();
    }
}

void IntelGTPowerManagement::onDisplayPowerChange(bool displayOn) {
    IOLog("IntelGTPowerManagement: Display power %s\n", displayOn ? "on" : "off");
    
    if (!displayOn) {
        // Display off: reduce frequency
        setFrequency(freqInfo.minFrequency);
    } else {
        // Display on: restore frequency
        setFrequency(freqInfo.baseFrequency);
    }
}

void IntelGTPowerManagement::onGPUActivity() {
    IOLockLock(lock);
    
    lastActivityTime = getCurrentTimeUs();
    
    if (gpuIdle) {
        gpuIdle = false;
        
        // Exit RC6 if in it
        if (currentRC6State != RC_STATE_RC0) {
            IOLockUnlock(lock);
            exitRC6();
            IOLockLock(lock);
        }
        
        updateActiveStatistics();
    }
    
    IOLockUnlock(lock);
}

void IntelGTPowerManagement::onGPUIdle() {
    IOLockLock(lock);
    
    if (!gpuIdle) {
        gpuIdle = true;
        lastIdleTime = getCurrentTimeUs();
        
        updateIdleStatistics();
    }
    
    IOLockUnlock(lock);
    
    // Schedule idle check for RC6 entry
    scheduleIdleCheck(rc6IdleThreshold);
}

// MARK: - Hardware Methods (Stubs)

bool IntelGTPowerManagement::readFrequencyLimits() {
    if (!uncore) {
        return false;
    }
    
    // Read RP_STATE_CAP register
    uint32_t rp_state_cap = READ_RP_STATE_CAP(uncore);
    
    // Extract RP0, RP1, RPn ratios
    uint32_t rp0_ratio, rp1_ratio, rpn_ratio;
    extract_rp_capabilities(rp_state_cap, &rp0_ratio, &rp1_ratio, &rpn_ratio);
    
    // Convert to frequencies
    freqInfo.maxFrequency = ratio_to_frequency_mhz(rp0_ratio);    // RP0
    freqInfo.baseFrequency = ratio_to_frequency_mhz(rp1_ratio);   // RP1
    freqInfo.minFrequency = ratio_to_frequency_mhz(rpn_ratio);    // RPn
    
    IOLog("IntelGTPowerManagement: Read frequency limits from hardware\n");
    IOLog("  RP0 (max): %u MHz (ratio %u)\n", freqInfo.maxFrequency, rp0_ratio);
    IOLog("  RP1 (base): %u MHz (ratio %u)\n", freqInfo.baseFrequency, rp1_ratio);
    IOLog("  RPn (min): %u MHz (ratio %u)\n", freqInfo.minFrequency, rpn_ratio);
    
    return true;
}

bool IntelGTPowerManagement::writeFrequencyRequest(uint32_t ratio) {
    if (!uncore) {
        return false;
    }
    
    // Clamp ratio to valid range (0-31)
    if (ratio > 31) {
        ratio = 31;
    }
    
    // Build frequency request value
    uint32_t request = build_frequency_request(ratio, freqInfo.turboEnabled);
    
    // Write GEN6_RPNSWREQ register
    WRITE_FREQUENCY_REQUEST(uncore, request);
    
    IOLog("IntelGTPowerManagement: Wrote frequency request (ratio %u = %u MHz)\n",
          ratio, ratio_to_frequency_mhz(ratio));
    
    return true;
}

bool IntelGTPowerManagement::readCurrentFrequency(uint32_t* frequencyMHz) {
    if (!uncore || !frequencyMHz) {
        return false;
    }
    
    // Read GEN6_RPSTAT1 register
    uint32_t rpstat1 = READ_RPSTAT1(uncore);
    
    // Extract current actual frequency
    *frequencyMHz = extract_current_frequency(rpstat1);
    
    return true;
}

bool IntelGTPowerManagement::writeRC6Control(bool enable) {
    if (!uncore) {
        return false;
    }
    
    // Build RC6 control value
    // Enable RC6, optionally enable RC6p for deeper sleep
    uint32_t rc6_control = build_rc6_control(enable, enable, false);
    
    // Write GEN6_RC_CONTROL register
    WRITE_RC6_CONTROL(uncore, rc6_control);
    
    // Set RC6 idle threshold (100ms default)
    uint32_t threshold = us_to_rc6_threshold(rc6IdleThreshold * 1000);
    uncore->writeRegister32(GEN6_RC6_THRESHOLD, threshold);
    
    IOLog("IntelGTPowerManagement: RC6 control written (enable=%d, threshold=%u us)\n",
          enable, rc6IdleThreshold * 1000);
    
    return true;
}

bool IntelGTPowerManagement::readRC6State(IntelRC6State* state) {
    if (!uncore || !state) {
        return false;
    }
    
    // Read GEN6_RC_STATE register
    uint32_t rc_state = READ_RC6_STATE(uncore);
    
    // Extract RC6 state
    uint32_t state_value = extract_rc6_state(rc_state);
    
    // Map to our enum
    switch (state_value) {
        case GEN6_RC_STATE_RC0:
            *state = RC_STATE_RC0;
            break;
        case GEN6_RC_STATE_RC1:
            *state = RC_STATE_RC1;
            break;
        case GEN6_RC_STATE_RC6:
            *state = RC_STATE_RC6;
            break;
        case GEN6_RC_STATE_RC6p:
            *state = RC_STATE_RC6p;
            break;
        case GEN6_RC_STATE_RC6pp:
            *state = RC_STATE_RC6pp;
            break;
        default:
            *state = RC_STATE_RC0;
            break;
    }
    
    return true;
}

bool IntelGTPowerManagement::readRC6Residency(uint64_t* residency) {
    if (!uncore || !residency) {
        return false;
    }
    
    // Read RC6 residency counter
    uint32_t rc6_counter = READ_RC6_RESIDENCY(uncore);
    
    // Convert to microseconds
    *residency = rc6_residency_to_us(rc6_counter);
    
    // Also read RC6p and RC6pp if available
    uint32_t rc6p_counter = READ_RC6p_RESIDENCY(uncore);
    uint32_t rc6pp_counter = READ_RC6pp_RESIDENCY(uncore);
    
    statistics.rc6Stats.rc6_residency = *residency;
    statistics.rc6Stats.rc6p_residency = rc6_residency_to_us(rc6p_counter);
    statistics.rc6Stats.rc6pp_residency = rc6_residency_to_us(rc6pp_counter);
    
    return true;
}

bool IntelGTPowerManagement::writeClockGateControl(ClockGatingUnit unit, bool enable) {
    if (!uncore) {
        return false;
    }
    
    bool success = true;
    
    // Handle each clock gating unit
    if (unit & CLOCK_GATE_RENDER) {
        // Read-modify-write GEN6_UCGCTL1
        uint32_t ucgctl1 = uncore->readRegister32(GEN6_UCGCTL1);
        if (enable) {
            ucgctl1 &= ~(GEN6_BLBUNIT_CLOCK_GATE_DISABLE | GEN6_CSUNIT_CLOCK_GATE_DISABLE);
        } else {
            ucgctl1 |= (GEN6_BLBUNIT_CLOCK_GATE_DISABLE | GEN6_CSUNIT_CLOCK_GATE_DISABLE);
        }
        uncore->writeRegister32(GEN6_UCGCTL1, ucgctl1);
    }
    
    if (unit & CLOCK_GATE_MEDIA) {
        // Read-modify-write GEN8_UCGCTL6
        uint32_t ucgctl6 = uncore->readRegister32(GEN8_UCGCTL6);
        if (enable) {
            ucgctl6 &= ~GEN8_SDEUNIT_CLOCK_GATE_DISABLE;
        } else {
            ucgctl6 |= GEN8_SDEUNIT_CLOCK_GATE_DISABLE;
        }
        uncore->writeRegister32(GEN8_UCGCTL6, ucgctl6);
        
        // Also handle GEN8_UCGCTL4 for media
        uint32_t ucgctl4 = uncore->readRegister32(GEN8_UCGCTL4);
        if (enable) {
            ucgctl4 &= ~GEN8_HDCUNIT_CLOCK_GATE_DISABLE_HD0;
        } else {
            ucgctl4 |= GEN8_HDCUNIT_CLOCK_GATE_DISABLE_HD0;
        }
        uncore->writeRegister32(GEN8_UCGCTL4, ucgctl4);
    }
    
    if (unit & CLOCK_GATE_BLITTER) {
        // Read-modify-write GEN6_UCGCTL2
        uint32_t ucgctl2 = uncore->readRegister32(GEN6_UCGCTL2);
        if (enable) {
            ucgctl2 &= ~(GEN6_RCCUNIT_CLOCK_GATE_DISABLE | GEN6_RCPBUNIT_CLOCK_GATE_DISABLE);
        } else {
            ucgctl2 |= (GEN6_RCCUNIT_CLOCK_GATE_DISABLE | GEN6_RCPBUNIT_CLOCK_GATE_DISABLE);
        }
        uncore->writeRegister32(GEN6_UCGCTL2, ucgctl2);
    }
    
    if (unit & CLOCK_GATE_VIDEO) {
        // Video uses display clock gating
        uint32_t dspclk = uncore->readRegister32(DSPCLK_GATE_D);
        if (enable) {
            dspclk &= ~(DPFUNIT_CLOCK_GATE_DISABLE | DPCUNIT_CLOCK_GATE_DISABLE);
        } else {
            dspclk |= (DPFUNIT_CLOCK_GATE_DISABLE | DPCUNIT_CLOCK_GATE_DISABLE);
        }
        uncore->writeRegister32(DSPCLK_GATE_D, dspclk);
    }
    
    IOLog("IntelGTPowerManagement: Clock gating %s for units 0x%x\n",
          enable ? "enabled" : "disabled", unit);
    
    return success;
}

bool IntelGTPowerManagement::readClockGateStatus(ClockGatingUnit unit, bool* enabled) {
    if (!uncore || !enabled) {
        return false;
    }
    
    bool is_enabled = false;
    
    // Check each unit's status
    if (unit & CLOCK_GATE_RENDER) {
        uint32_t ucgctl1 = uncore->readRegister32(GEN6_UCGCTL1);
        is_enabled = !(ucgctl1 & GEN6_BLBUNIT_CLOCK_GATE_DISABLE);
    } else if (unit & CLOCK_GATE_MEDIA) {
        uint32_t ucgctl6 = uncore->readRegister32(GEN8_UCGCTL6);
        is_enabled = !(ucgctl6 & GEN8_SDEUNIT_CLOCK_GATE_DISABLE);
    } else if (unit & CLOCK_GATE_BLITTER) {
        uint32_t ucgctl2 = uncore->readRegister32(GEN6_UCGCTL2);
        is_enabled = !(ucgctl2 & GEN6_RCCUNIT_CLOCK_GATE_DISABLE);
    } else if (unit & CLOCK_GATE_VIDEO) {
        uint32_t dspclk = uncore->readRegister32(DSPCLK_GATE_D);
        is_enabled = !(dspclk & DPFUNIT_CLOCK_GATE_DISABLE);
    }
    
    *enabled = is_enabled;
    return true;
}

bool IntelGTPowerManagement::readTemperature(uint32_t* tempCelsius) {
    if (!uncore || !tempCelsius) {
        return false;
    }
    
    // Read package thermal status
    uint32_t therm_status = READ_PACKAGE_THERM_STATUS(uncore);
    
    // Extract temperature (Tj_max - offset)
    uint32_t temp = extract_temperature_celsius(therm_status, TIGER_LAKE_TJ_MAX);
    
    if (temp == 0) {
        // Try GT performance status as fallback
        uint32_t gt_perf = READ_GT_PERF_STATUS(uncore);
        temp = (gt_perf & GT_PERF_TEMP_MASK) >> GT_PERF_TEMP_SHIFT;
    }
    
    *tempCelsius = temp;
    
    // Check for thermal limits
    if (therm_status & THERM_STATUS_THERMAL_LIMIT) {
        IOLog("IntelGTPowerManagement: WARNING - Thermal limit reached (%udegC)\n", temp);
    }
    
    // Check performance limit reasons
    uint32_t perf_limit = READ_PERF_LIMIT_REASONS(uncore);
    if (is_thermal_throttling(perf_limit)) {
        IOLog("IntelGTPowerManagement: WARNING - Thermal throttling active\n");
    }
    
    return true;
}

// MARK: - Helper Methods

uint32_t IntelGTPowerManagement::ratioToFrequency(uint32_t ratio) {
    // Tiger Lake: ratio * 50 MHz
    return ratio * 50;
}

uint32_t IntelGTPowerManagement::frequencyToRatio(uint32_t frequencyMHz) {
    // Tiger Lake: frequency / 50 MHz
    return (frequencyMHz + 25) / 50;  // Round to nearest
}

uint64_t IntelGTPowerManagement::getCurrentTimeUs() {
    uint64_t abstime;
    uint64_t nanoseconds;
    
    clock_get_uptime(&abstime);
    absolutetime_to_nanoseconds(abstime, &nanoseconds);
    
    return nanoseconds / 1000;  // Convert to microseconds
}

void IntelGTPowerManagement::updateIdleStatistics() {
    uint64_t now = getCurrentTimeUs();
    if (lastActivityTime > 0) {
        uint64_t idleDuration = now - lastActivityTime;
        statistics.totalIdleTime += idleDuration;
    }
}

void IntelGTPowerManagement::updateActiveStatistics() {
    uint64_t now = getCurrentTimeUs();
    if (lastIdleTime > 0) {
        uint64_t activeDuration = now - lastIdleTime;
        statistics.totalActiveTime += activeDuration;
    }
}

// MARK: - Timer Callback

void IntelGTPowerManagement::idleCheckCallback(OSObject* owner, IOTimerEventSource* timer) {
    IntelGTPowerManagement* self = OSDynamicCast(IntelGTPowerManagement, owner);
    if (self) {
        self->handleIdleCheck();
    }
}

void IntelGTPowerManagement::handleIdleCheck() {
    detectIdleState();
    detectWorkloadLevel();
    adjustFrequencyForWorkload();
    
    // Check thermal throttling
    if (isThrottlingRequired()) {
        applyThermalThrottling();
    } else if (thermalThrottling) {
        clearThermalThrottling();
    }
    
    // Schedule next check
    scheduleIdleCheck(100);  // Check every 100ms
}
