/*
 * IntelDPLL.h
 * 
 * Display PLL (clock generation)
 */

#ifndef INTEL_DPLL_H
#define INTEL_DPLL_H

#include "linux_compat.h"

class AppleIntelTGLController;

class IntelDPLL {
public:
    IntelDPLL();
    ~IntelDPLL();
    
    bool init(AppleIntelTGLController *ctrl, u32 dpllIndex);
    void cleanup();
    
    bool enable();
    bool disable();
    bool isEnabled() const { return enabled; }
    
    bool setFrequency(u64 frequencyHz);
    u64 getFrequency() const { return frequencyHz; }
    
private:
    AppleIntelTGLController *controller;
    u32 dpllIndex;
    bool enabled;
    u64 frequencyHz;
};

#endif // INTEL_DPLL_H
