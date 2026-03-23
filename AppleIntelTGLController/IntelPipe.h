/*
 * IntelPipe.h
 * 
 * Display pipe (scanout engine)
 * Each pipe can drive one display output
 */

#ifndef INTEL_PIPE_H
#define INTEL_PIPE_H

#include "linux_compat.h"

class AppleIntelTGLController;

/* Pipe Registers (per pipe) */
#define PIPE_A_OFFSET   0x70000
#define PIPE_B_OFFSET   0x71000
#define PIPE_C_OFFSET   0x72000

class IntelPipe {
public:
    IntelPipe();
    ~IntelPipe();
    
    bool init(AppleIntelTGLController *ctrl, u32 pipeIndex);
    void cleanup();
    
    bool enable();
    bool disable();
    bool isEnabled() const { return enabled; }
    
    u32 getPipeIndex() const { return pipeIndex; }
    const char* getPipeName() const;
    u32 getRegisterOffset() const { return regBase; }
    
private:
    AppleIntelTGLController *controller;
    u32 pipeIndex;
    bool enabled;
    u32 regBase;
    
    void writeRegister(u32 offset, u32 value);
    u32 readRegister(u32 offset);
};

#endif // INTEL_PIPE_H
