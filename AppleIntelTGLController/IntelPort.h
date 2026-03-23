/*
 * IntelPort.h
 * 
 * Display port (output connector)
 */

#ifndef INTEL_PORT_H
#define INTEL_PORT_H

#include "linux_compat.h"

class AppleIntelTGLController;

class IntelPort {
public:
    IntelPort();
    ~IntelPort();
    
    bool init(AppleIntelTGLController *ctrl, u32 portIndex);
    void cleanup();
    
    bool detect();  // Detect if display is connected
    bool enable();
    bool disable();
    
    bool isConnected() const { return connected; }
    bool isEnabled() const { return enabled; }
    
private:
    AppleIntelTGLController *controller;
    u32 portIndex;
    bool connected;
    bool enabled;
};

#endif // INTEL_PORT_H
