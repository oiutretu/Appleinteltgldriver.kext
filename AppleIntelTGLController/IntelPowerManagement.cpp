/*
 * IntelPowerManagement.cpp
 * Intel Graphics Driver - Power Management Implementation
 */

#include "IntelPowerManagement.h"
#include "intel_power_regs.h"
#include "IntelUncore.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOTimerEventSource.h>
#include <mach/mach_time.h>

#define super IOService
OSDefineMetaClassAndStructors(IntelPowerManagement, IOService)

// Default panel timing (microseconds) - conservative values
static const PanelPowerTiming kDefaultPanelTiming = {
    .powerUpDelay = 200000,      // 200ms T1+T2
    .powerDownDelay = 500000,    // 500ms T3
    .backlightOnDelay = 100000,  // 100ms T4
    .backlightOffDelay = 100000, // 100ms T5
    .powerCycleDelay = 500000    // 500ms T6
};

bool IntelPowerManagement::init(OSDictionary* dictionary) {
    if (!super::init(dictionary)) {
        return false;
    }
    
    // Initialize state
    currentPowerState = POWER_STATE_OFF;
    currentDPMSState = DPMS_OFF;
    displayPowered = false;
    panelPowered = false;
    currentPowerStateOrdinal = 0;
    
    // Initialize panel timing with defaults
    panelTiming = kDefaultPanelTiming;
    lastPanelPowerChange = 0;
    
    // Initialize backlight
    backlight.maxLevel = 100;
    backlight.currentLevel = 80;  // Default 80%
    backlight.pwmFrequency = 200; // 200 Hz default
    backlight.enabled = false;
    backlight.usesPWM = true;
    
    // Initialize statistics
    powerTransitions = 0;
    dpmsChanges = 0;
    totalOnTime = 0;
    totalOffTime = 0;
    
    controller = nullptr;
    powerLock = nullptr;
    
    IOLog("IntelPowerManagement: Initialized\n");
    return true;
}

void IntelPowerManagement::free() {
    if (powerLock) {
        IOLockFree(powerLock);
        powerLock = nullptr;
    }
    
    super::free();
}

bool IntelPowerManagement::start(IOService* provider) {
    if (!super::start(provider)) {
        return false;
    }
    
    // Get controller reference
    controller = OSDynamicCast(AppleIntelTGLController, provider);
    if (!controller) {
        IOLog("IntelPowerManagement: Failed to get controller\n");
        return false;
    }
    
    // Create lock
    powerLock = IOLockAlloc();
    if (!powerLock) {
        IOLog("IntelPowerManagement: Failed to allocate lock\n");
        return false;
    }
    
    // Initialize power management
    initializePowerManagement();
    
    // Set up power states
    setupPowerStates();
    
    // Read panel timing from hardware
    readPanelTimingFromHardware();
    
    // Register with power management
    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, powerStates, POWER_STATE_COUNT);
    
    IOLog("IntelPowerManagement: Started successfully\n");
    IOLog("IntelPowerManagement: Panel timing - T1+T2:%uus T3:%uus T4:%uus T5:%uus T6:%uus\n",
          panelTiming.powerUpDelay, panelTiming.powerDownDelay,
          panelTiming.backlightOnDelay, panelTiming.backlightOffDelay,
          panelTiming.powerCycleDelay);
    
    return true;
}

void IntelPowerManagement::stop(IOService* provider) {
    // Disable display power
    if (displayPowered) {
        disableDisplayPower();
    }
    
    // Unregister from power management
    PMstop();
    
    IOLog("IntelPowerManagement: Stopped\n");
    super::stop(provider);
}

void IntelPowerManagement::initializePowerManagement() {
    // Start in off state
    currentPowerState = POWER_STATE_OFF;
    currentDPMSState = DPMS_OFF;
    displayPowered = false;
    panelPowered = false;
}

void IntelPowerManagement::setupPowerStates() {
    // Define power states for macOS power management
    
    // State 0: Off
    powerStates[POWER_STATE_OFF].version = 1;
    powerStates[POWER_STATE_OFF].capabilityFlags = 0;
    powerStates[POWER_STATE_OFF].outputPowerCharacter = 0;
    powerStates[POWER_STATE_OFF].inputPowerRequirement = 0;
    powerStates[POWER_STATE_OFF].staticPower = 0;
    powerStates[POWER_STATE_OFF].settleUpTime = 0;
    powerStates[POWER_STATE_OFF].settleDownTime = 0;
    
    // State 1: Sleep
    powerStates[POWER_STATE_SLEEP].version = 1;
    powerStates[POWER_STATE_SLEEP].capabilityFlags = kIOPMSleepCapability;
    powerStates[POWER_STATE_SLEEP].outputPowerCharacter = kIOPMSleepCapability;
    powerStates[POWER_STATE_SLEEP].inputPowerRequirement = kIOPMSleepCapability;
    powerStates[POWER_STATE_SLEEP].staticPower = 100;
    powerStates[POWER_STATE_SLEEP].settleUpTime = 1000;   // 1ms
    powerStates[POWER_STATE_SLEEP].settleDownTime = 1000;
    
    // State 2: Doze (display off, quick wake)
    powerStates[POWER_STATE_DOZE].version = 1;
    powerStates[POWER_STATE_DOZE].capabilityFlags = kIOPMDoze;
    powerStates[POWER_STATE_DOZE].outputPowerCharacter = kIOPMDoze;
    powerStates[POWER_STATE_DOZE].inputPowerRequirement = kIOPMDoze;
    powerStates[POWER_STATE_DOZE].staticPower = 1000;
    powerStates[POWER_STATE_DOZE].settleUpTime = 50000;   // 50ms
    powerStates[POWER_STATE_DOZE].settleDownTime = 50000;
    
    // State 3: On (full power)
    powerStates[POWER_STATE_ON].version = 1;
    powerStates[POWER_STATE_ON].capabilityFlags = kIOPMPowerOn;
    powerStates[POWER_STATE_ON].outputPowerCharacter = kIOPMPowerOn;
    powerStates[POWER_STATE_ON].inputPowerRequirement = kIOPMPowerOn;
    powerStates[POWER_STATE_ON].staticPower = 10000;
    powerStates[POWER_STATE_ON].settleUpTime = 200000;    // 200ms (panel power-up)
    powerStates[POWER_STATE_ON].settleDownTime = 500000;  // 500ms (panel power-down)
}

void IntelPowerManagement::readPanelTimingFromHardware() {
    if (!controller) {
        IOLog("IntelPowerManagement: No controller for timing read\n");
        return;
    }
    
    IntelUncore* uncore = controller->getUncore();
    if (!uncore) {
        IOLog("IntelPowerManagement: No uncore for timing read\n");
        return;
    }
    
    // Read PP_ON_DELAYS register
    uint32_t onDelays = uncore->readRegister32(PCH_PP_ON_DELAYS);
    panelTiming.powerUpDelay = reg_to_panel_power_up_delay(onDelays);
    panelTiming.backlightOnDelay = reg_to_panel_light_on_delay(onDelays);
    
    // Read PP_OFF_DELAYS register
    uint32_t offDelays = uncore->readRegister32(PCH_PP_OFF_DELAYS);
    panelTiming.powerDownDelay = reg_to_panel_power_down_delay(offDelays);
    panelTiming.backlightOffDelay = reg_to_panel_light_off_delay(offDelays);
    
    // Read PP_DIVISOR register
    uint32_t divisor = uncore->readRegister32(PCH_PP_DIVISOR);
    panelTiming.powerCycleDelay = reg_to_panel_power_cycle_delay(divisor);
    
    IOLog("IntelPowerManagement: Panel timing from hardware - T1+T2:%uus T3:%uus T4:%uus T5:%uus T6:%uus\n",
          panelTiming.powerUpDelay, panelTiming.powerDownDelay,
          panelTiming.backlightOnDelay, panelTiming.backlightOffDelay,
          panelTiming.powerCycleDelay);
}

IOReturn IntelPowerManagement::setPowerState(unsigned long powerStateOrdinal, IOService* whatDevice) {
    IOLockLock(powerLock);
    
    if (powerStateOrdinal >= POWER_STATE_COUNT) {
        IOLockUnlock(powerLock);
        return IOPMAckImplied;
    }
    
    IntelPowerState newState = (IntelPowerState)powerStateOrdinal;
    
    IOLog("IntelPowerManagement: Power state change %lu -> %lu\n",
          (unsigned long)currentPowerState, powerStateOrdinal);
    
    // Handle state transition
    switch (newState) {
        case POWER_STATE_OFF:
        case POWER_STATE_SLEEP:
            // Disable display
            if (displayPowered) {
                disableDisplayPower();
            }
            setDPMSState(DPMS_OFF);
            break;
            
        case POWER_STATE_DOZE:
            // Display off but quick wake
            if (displayPowered) {
                disableBacklight();
            }
            setDPMSState(DPMS_STANDBY);
            break;
            
        case POWER_STATE_ON:
            // Full power
            if (!displayPowered) {
                enableDisplayPower();
            }
            setDPMSState(DPMS_ON);
            break;
            
        default:
            break;
    }
    
    currentPowerState = newState;
    currentPowerStateOrdinal = powerStateOrdinal;
    powerTransitions++;
    
    IOLockUnlock(powerLock);
    return IOPMAckImplied;
}

unsigned long IntelPowerManagement::maxCapabilityForDomainState(IOPMPowerFlags domainState) {
    if (domainState & kIOPMPowerOn) {
        return POWER_STATE_ON;
    } else if (domainState & kIOPMDoze) {
        return POWER_STATE_DOZE;
    } else if (domainState & kIOPMSleepCapability) {
        return POWER_STATE_SLEEP;
    }
    return POWER_STATE_OFF;
}

unsigned long IntelPowerManagement::initialPowerStateForDomainState(IOPMPowerFlags domainState) {
    return POWER_STATE_OFF;
}

IOReturn IntelPowerManagement::powerStateWillChangeTo(IOPMPowerFlags capabilities,
                                                       unsigned long stateNumber,
                                                       IOService* whatDevice) {
    IOLog("IntelPowerManagement: Power state will change to %lu\n", stateNumber);
    return IOPMAckImplied;
}

IOReturn IntelPowerManagement::powerStateDidChangeTo(IOPMPowerFlags capabilities,
                                                      unsigned long stateNumber,
                                                      IOService* whatDevice) {
    IOLog("IntelPowerManagement: Power state did change to %lu\n", stateNumber);
    return IOPMAckImplied;
}

IOReturn IntelPowerManagement::setDPMSState(IntelDPMSState state) {
    if (state == currentDPMSState) {
        return kIOReturnSuccess;
    }
    
    IOLog("IntelPowerManagement: DPMS %s -> %s\n",
          getDPMSStateName(currentDPMSState),
          getDPMSStateName(state));
    
    applyDPMSState(state);
    
    currentDPMSState = state;
    dpmsChanges++;
    
    return kIOReturnSuccess;
}

const char* IntelPowerManagement::getDPMSStateName(IntelDPMSState state) const {
    switch (state) {
        case DPMS_ON:       return "ON";
        case DPMS_STANDBY:  return "STANDBY";
        case DPMS_SUSPEND:  return "SUSPEND";
        case DPMS_OFF:      return "OFF";
        default:            return "UNKNOWN";
    }
}

void IntelPowerManagement::applyDPMSState(IntelDPMSState state) {
    switch (state) {
        case DPMS_ON:
            // Full power - enable everything
            configurePipePower(true);
            configureDDIPower(true);
            if (panelPowered) {
                enableBacklight();
            }
            break;
            
        case DPMS_STANDBY:
            // Quick wake - blank display but keep sync
            disableBacklight();
            // Keep pipe and DDI powered
            break;
            
        case DPMS_SUSPEND:
            // Power save - disable HSYNC
            disableBacklight();
            configureDDIPower(false);
            // Keep pipe powered for quick recovery
            break;
            
        case DPMS_OFF:
            // Maximum power save - disable everything
            disableBacklight();
            configureDDIPower(false);
            configurePipePower(false);
            break;
    }
}

IOReturn IntelPowerManagement::enableDisplayPower() {
    if (displayPowered) {
        return kIOReturnSuccess;
    }
    
    IOLog("IntelPowerManagement: Enabling display power\n");
    
    // Power on panel
    IOReturn result = panelPowerOn();
    if (result != kIOReturnSuccess) {
        IOLog("IntelPowerManagement: Panel power-on failed\n");
        return result;
    }
    
    // Enable display pipeline
    configurePipePower(true);
    configureDDIPower(true);
    
    // Enable backlight
    enableBacklight();
    
    displayPowered = true;
    return kIOReturnSuccess;
}

IOReturn IntelPowerManagement::disableDisplayPower() {
    if (!displayPowered) {
        return kIOReturnSuccess;
    }
    
    IOLog("IntelPowerManagement: Disabling display power\n");
    
    // Disable backlight first
    disableBacklight();
    
    // Wait for backlight to turn off
    IOSleep(panelTiming.backlightOffDelay / 1000);
    
    // Disable display pipeline
    configureDDIPower(false);
    configurePipePower(false);
    
    // Power off panel
    panelPowerOff();
    
    displayPowered = false;
    return kIOReturnSuccess;
}

IOReturn IntelPowerManagement::panelPowerOn() {
    if (panelPowered) {
        return kIOReturnSuccess;
    }
    
    // Check power cycle delay
    waitForPanelPowerCycle();
    
    IOLog("IntelPowerManagement: Panel power-on sequence\n");
    
    // Enable panel power
    enablePanelPower();
    
    // Wait for power to stabilize (T1+T2)
    IOSleep(panelTiming.powerUpDelay / 1000);
    
    panelPowered = true;
    lastPanelPowerChange = mach_absolute_time();
    
    return kIOReturnSuccess;
}

IOReturn IntelPowerManagement::panelPowerOff() {
    if (!panelPowered) {
        return kIOReturnSuccess;
    }
    
    IOLog("IntelPowerManagement: Panel power-off sequence\n");
    
    // Ensure backlight is off
    if (backlight.enabled) {
        disableBacklight();
        IOSleep(panelTiming.backlightOffDelay / 1000);
    }
    
    // Disable panel power
    disablePanelPower();
    
    // Wait for power down (T3)
    IOSleep(panelTiming.powerDownDelay / 1000);
    
    panelPowered = false;
    lastPanelPowerChange = mach_absolute_time();
    
    return kIOReturnSuccess;
}

IOReturn IntelPowerManagement::setBacklightLevel(uint32_t level) {
    if (level > 100) {
        level = 100;
    }
    
    backlight.currentLevel = level;
    
    if (backlight.enabled && panelPowered) {
        updateBacklightPWM();
    }
    
    return kIOReturnSuccess;
}

IOReturn IntelPowerManagement::enableBacklight() {
    if (backlight.enabled) {
        return kIOReturnSuccess;
    }
    
    if (!panelPowered) {
        IOLog("IntelPowerManagement: Cannot enable backlight - panel not powered\n");
        return kIOReturnNotReady;
    }
    
    // Wait for backlight-on delay (T4)
    IOSleep(panelTiming.backlightOnDelay / 1000);
    
    // Enable panel backlight
    enablePanelBacklight();
    
    // Set PWM level
    updateBacklightPWM();
    
    backlight.enabled = true;
    
    IOLog("IntelPowerManagement: Backlight enabled at %u%%\n", backlight.currentLevel);
    return kIOReturnSuccess;
}

IOReturn IntelPowerManagement::disableBacklight() {
    if (!backlight.enabled) {
        return kIOReturnSuccess;
    }
    
    // Disable panel backlight
    disablePanelBacklight();
    
    backlight.enabled = false;
    
    IOLog("IntelPowerManagement: Backlight disabled\n");
    return kIOReturnSuccess;
}

void IntelPowerManagement::configurePanelTiming(const PanelPowerTiming& timing) {
    panelTiming = timing;
    
    IOLog("IntelPowerManagement: Panel timing configured - T1+T2:%uus T3:%uus T4:%uus T5:%uus T6:%uus\n",
          timing.powerUpDelay, timing.powerDownDelay,
          timing.backlightOnDelay, timing.backlightOffDelay,
          timing.powerCycleDelay);
}

void IntelPowerManagement::waitForPanelPowerCycle() {
    if (lastPanelPowerChange == 0) {
        return;
    }
    
    // Calculate elapsed time since last power change
    uint64_t now = mach_absolute_time();
    uint64_t elapsed = now - lastPanelPowerChange;
    
    // Convert to microseconds
    // On x86_64, mach_absolute_time() returns nanoseconds
    uint64_t elapsedUs = elapsed / 1000;
    
    // Check if we need to wait
    if (elapsedUs < panelTiming.powerCycleDelay) {
        uint32_t waitUs = panelTiming.powerCycleDelay - (uint32_t)elapsedUs;
        IOLog("IntelPowerManagement: Waiting %uus for panel power cycle\n", waitUs);
        IOSleep(waitUs / 1000);
    }
}

void IntelPowerManagement::enablePanelPower() {
    if (!controller) {
        IOLog("IntelPowerManagement: No controller for panel power enable\n");
        return;
    }
    
    IntelUncore* uncore = controller->getUncore();
    if (!uncore) {
        IOLog("IntelPowerManagement: No uncore for panel power enable\n");
        return;
    }
    
    // Read current PP_CONTROL value
    uint32_t ppControl = uncore->readRegister32(PCH_PP_CONTROL);
    
    // Set panel power on with unlock key
    ppControl |= PANEL_UNLOCK_REGS | PANEL_POWER_ON;
    
    // Write back
    uncore->writeRegister32(PCH_PP_CONTROL, ppControl);
    
    IOLog("IntelPowerManagement: Panel power enabled (PP_CONTROL=0x%x)\n", ppControl);
}

void IntelPowerManagement::disablePanelPower() {
    if (!controller) {
        IOLog("IntelPowerManagement: No controller for panel power disable\n");
        return;
    }
    
    IntelUncore* uncore = controller->getUncore();
    if (!uncore) {
        IOLog("IntelPowerManagement: No uncore for panel power disable\n");
        return;
    }
    
    // Read current PP_CONTROL value
    uint32_t ppControl = uncore->readRegister32(PCH_PP_CONTROL);
    
    // Clear panel power on with unlock key
    ppControl &= ~PANEL_POWER_ON;
    ppControl |= PANEL_UNLOCK_REGS;
    
    // Write back
    uncore->writeRegister32(PCH_PP_CONTROL, ppControl);
    
    // Wait for panel to power down (poll PP_STATUS)
    int timeout = 1000; // 1 second timeout
    while (timeout > 0) {
        uint32_t ppStatus = uncore->readRegister32(PCH_PP_STATUS);
        if (!(ppStatus & PP_ON)) {
            // Panel is off
            IOLog("IntelPowerManagement: Panel powered down after %dms\n", 1000 - timeout);
            return;
        }
        IOSleep(1);
        timeout--;
    }
    
    IOLog("IntelPowerManagement: Panel power disable timeout (PP_CONTROL=0x%x)\n", ppControl);
}

void IntelPowerManagement::enablePanelBacklight() {
    if (!controller) {
        IOLog("IntelPowerManagement: No controller for backlight enable\n");
        return;
    }
    
    IntelUncore* uncore = controller->getUncore();
    if (!uncore) {
        IOLog("IntelPowerManagement: No uncore for backlight enable\n");
        return;
    }
    
    // Enable backlight in PP_CONTROL
    uint32_t ppControl = uncore->readRegister32(PCH_PP_CONTROL);
    ppControl |= PANEL_UNLOCK_REGS | EDP_BLC_ENABLE;
    uncore->writeRegister32(PCH_PP_CONTROL, ppControl);
    
    // Enable CPU PWM
    uint32_t pwmCtl = uncore->readRegister32(BLC_PWM_CPU_CTL2);
    pwmCtl |= BLM_PWM_ENABLE;
    uncore->writeRegister32(BLC_PWM_CPU_CTL2, pwmCtl);
    
    IOLog("IntelPowerManagement: Backlight enabled (PP_CONTROL=0x%x, PWM_CTL=0x%x)\n",
          ppControl, pwmCtl);
}

void IntelPowerManagement::disablePanelBacklight() {
    if (!controller) {
        IOLog("IntelPowerManagement: No controller for backlight disable\n");
        return;
    }
    
    IntelUncore* uncore = controller->getUncore();
    if (!uncore) {
        IOLog("IntelPowerManagement: No uncore for backlight disable\n");
        return;
    }
    
    // Disable CPU PWM
    uint32_t pwmCtl = uncore->readRegister32(BLC_PWM_CPU_CTL2);
    pwmCtl &= ~BLM_PWM_ENABLE;
    uncore->writeRegister32(BLC_PWM_CPU_CTL2, pwmCtl);
    
    // Disable backlight in PP_CONTROL
    uint32_t ppControl = uncore->readRegister32(PCH_PP_CONTROL);
    ppControl &= ~EDP_BLC_ENABLE;
    ppControl |= PANEL_UNLOCK_REGS;
    uncore->writeRegister32(PCH_PP_CONTROL, ppControl);
    
    IOLog("IntelPowerManagement: Backlight disabled (PP_CONTROL=0x%x, PWM_CTL=0x%x)\n",
          ppControl, pwmCtl);
}

void IntelPowerManagement::updateBacklightPWM() {
    if (!controller) {
        IOLog("IntelPowerManagement: No controller for backlight PWM update\n");
        return;
    }
    
    IntelUncore* uncore = controller->getUncore();
    if (!uncore) {
        IOLog("IntelPowerManagement: No uncore for backlight PWM update\n");
        return;
    }
    
    // Calculate duty cycle (0-0xffff scale)
    // backlight.currentLevel is 0-100%
    uint32_t dutyCycle = (backlight.currentLevel * 0xffff) / 100;
    
    // Write to BLC_PWM_CPU_CTL (duty cycle register)
    uncore->writeRegister32(BLC_PWM_CPU_CTL, dutyCycle & BLM_DUTY_CYCLE_MASK);
    
    IOLog("IntelPowerManagement: Backlight PWM set to %u%% (duty=0x%x)\n",
          backlight.currentLevel, dutyCycle);
}

void IntelPowerManagement::configureDDIPower(bool enable) {
    if (!controller) {
        IOLog("IntelPowerManagement: No controller for DDI power configuration\n");
        return;
    }
    
    IntelDisplay* display = controller->getDisplay();
    if (!display) {
        IOLog("IntelPowerManagement: No display for DDI power configuration\n");
        return;
    }
    
    // Enable/disable all active ports through the display manager
    // The display manager handles the actual port power sequencing
    if (enable) {
        IOLog("IntelPowerManagement: Enabling DDI ports\n");
        // Display manager will enable ports when configuring output
    } else {
        IOLog("IntelPowerManagement: Disabling DDI ports\n");
        // Display manager will disable ports when tearing down
    }
}

void IntelPowerManagement::configurePipePower(bool enable) {
    if (!controller) {
        IOLog("IntelPowerManagement: No controller for pipe power configuration\n");
        return;
    }
    
    IntelDisplay* display = controller->getDisplay();
    if (!display) {
        IOLog("IntelPowerManagement: No display for pipe power configuration\n");
        return;
    }
    
    // Enable/disable display pipes through the display manager
    // The display manager handles pipe and plane power sequencing
    if (enable) {
        IOLog("IntelPowerManagement: Enabling display pipes\n");
        // Display manager will enable pipes when setting modes
    } else {
        IOLog("IntelPowerManagement: Disabling display pipes\n");
        // Display manager will disable pipes when tearing down
    }
}
