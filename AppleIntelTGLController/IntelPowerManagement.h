/*
 * IntelPowerManagement.h
 * Intel Graphics Driver - Power Management
 *
 * Week 16: Display Power Management
 * Implements DPMS states, backlight control, and panel power sequencing
 */

#ifndef INTEL_POWER_MANAGEMENT_H
#define INTEL_POWER_MANAGEMENT_H

#include <IOKit/IOService.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include "AppleIntelTGLController.h"

// DPMS (Display Power Management Signaling) States
enum IntelDPMSState {
    DPMS_ON        = 0,  // D0: Full power
    DPMS_STANDBY   = 1,  // D1: Quick wake (blank screen)
    DPMS_SUSPEND   = 2,  // D2: Power save (HSYNC off)
    DPMS_OFF       = 3   // D3: Maximum power save (all off)
};

// Power state enumeration
enum IntelPowerState {
    POWER_STATE_OFF      = 0,  // System off
    POWER_STATE_SLEEP    = 1,  // System sleep
    POWER_STATE_DOZE     = 2,  // Display doze
    POWER_STATE_ON       = 3,  // Full power
    POWER_STATE_COUNT    = 4
};

// Panel power sequencing delays (microseconds)
struct PanelPowerTiming {
    uint32_t powerUpDelay;      // T1+T2: Power up time
    uint32_t powerDownDelay;    // T3: Power down time
    uint32_t backlightOnDelay;  // T4: Backlight enable delay
    uint32_t backlightOffDelay; // T5: Backlight disable delay
    uint32_t powerCycleDelay;   // T6: Power cycle delay
};

// Backlight control structure
struct BacklightControl {
    uint32_t maxLevel;          // Maximum brightness level
    uint32_t currentLevel;      // Current brightness (0-100%)
    uint32_t pwmFrequency;      // PWM frequency (Hz)
    bool     enabled;           // Backlight enable state
    bool     usesPWM;           // Use PWM control vs register
};

class IntelPowerManagement : public IOService {
    OSDeclareDefaultStructors(IntelPowerManagement)
    
public:
    // Initialization
    virtual bool init(OSDictionary* dictionary = nullptr) override;
    virtual void free() override;
    
    virtual bool start(IOService* provider) override;
    virtual void stop(IOService* provider) override;
    
    // Power management
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal, IOService* whatDevice) override;
    virtual unsigned long maxCapabilityForDomainState(IOPMPowerFlags domainState) override;
    virtual unsigned long initialPowerStateForDomainState(IOPMPowerFlags domainState) override;
    virtual IOReturn powerStateWillChangeTo(IOPMPowerFlags capabilities, unsigned long stateNumber, IOService* whatDevice) override;
    virtual IOReturn powerStateDidChangeTo(IOPMPowerFlags capabilities, unsigned long stateNumber, IOService* whatDevice) override;
    
    // DPMS state management
    IOReturn setDPMSState(IntelDPMSState state);
    IntelDPMSState getDPMSState() const { return currentDPMSState; }
    const char* getDPMSStateName(IntelDPMSState state) const;
    
    // Display power control
    IOReturn enableDisplayPower();
    IOReturn disableDisplayPower();
    bool isDisplayPowered() const { return displayPowered; }
    
    // Panel power sequencing
    IOReturn panelPowerOn();
    IOReturn panelPowerOff();
    bool isPanelPowered() const { return panelPowered; }
    
    // Backlight control
    IOReturn setBacklightLevel(uint32_t level);  // 0-100%
    uint32_t getBacklightLevel() const { return backlight.currentLevel; }
    IOReturn enableBacklight();
    IOReturn disableBacklight();
    bool isBacklightEnabled() const { return backlight.enabled; }
    
    // Power state queries
    bool isFullyPowered() const { return currentPowerState == POWER_STATE_ON; }
    IntelPowerState getCurrentPowerState() const { return currentPowerState; }
    
    // Panel timing configuration
    void configurePanelTiming(const PanelPowerTiming& timing);
    const PanelPowerTiming& getPanelTiming() const { return panelTiming; }
    
private:
    // Helper methods
    void initializePowerManagement();
    void setupPowerStates();
    void readPanelTimingFromHardware();
    void applyDPMSState(IntelDPMSState state);
    void updateBacklightPWM();
    void waitForPanelPowerCycle();
    
    // Hardware control
    void enablePanelPower();
    void disablePanelPower();
    void enablePanelBacklight();
    void disablePanelBacklight();
    void configureDDIPower(bool enable);
    void configurePipePower(bool enable);
    
    // Power state tracking
    IntelPowerState     currentPowerState;
    IntelDPMSState      currentDPMSState;
    bool                displayPowered;
    bool                panelPowered;
    
    // Panel timing
    PanelPowerTiming    panelTiming;
    uint64_t            lastPanelPowerChange;  // Timestamp for power cycle delay
    
    // Backlight
    BacklightControl    backlight;
    
    // Power management
    IOPMPowerState      powerStates[POWER_STATE_COUNT];
    unsigned long       currentPowerStateOrdinal;
    
    // Provider
    AppleIntelTGLController* controller;
    
    // Locks
    IOLock*             powerLock;
    
    // Statistics
    uint32_t            powerTransitions;
    uint32_t            dpmsChanges;
    uint64_t            totalOnTime;
    uint64_t            totalOffTime;
};

#endif // INTEL_POWER_MANAGEMENT_H
