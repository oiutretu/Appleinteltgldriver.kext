/*
 * IntelPipe.cpp
 */

#include "IntelPipe.h"
#include "AppleIntelTGLController.h"
#include "IntelUncore.h"
#include <IOKit/IOLib.h>

IntelPipe::IntelPipe()
    : controller(NULL)
    , pipeIndex(0)
    , enabled(false)
    , regBase(0)
{
}

IntelPipe::~IntelPipe()
{
    cleanup();
}

bool IntelPipe::init(AppleIntelTGLController *ctrl, u32 index)
{
    if (!ctrl || index >= 3) {
        return false;
    }
    
    controller = ctrl;
    pipeIndex = index;
    
    // Set register base
    switch (index) {
        case 0: regBase = PIPE_A_OFFSET; break;
        case 1: regBase = PIPE_B_OFFSET; break;
        case 2: regBase = PIPE_C_OFFSET; break;
    }
    
    IOLog("IntelPipe: Initialized pipe %c (base 0x%x)\n",
          'A' + index, regBase);
    
    return true;
}

void IntelPipe::cleanup()
{
    if (enabled) {
        disable();
    }
}

bool IntelPipe::enable()
{
    if (enabled) {
        return true;
    }
    
    IOLog("IntelPipe: Enabling pipe %c\n", 'A' + pipeIndex);
    
    // Write pipe configuration register
    // Bit 31: Enable
    u32 pipeConf = readRegister(0x008);  // PIPE_CONF offset
    pipeConf |= (1U << 31);
    writeRegister(0x008, pipeConf);
    
    enabled = true;
    
    return true;
}

bool IntelPipe::disable()
{
    if (!enabled) {
        return true;
    }
    
    IOLog("IntelPipe: Disabling pipe %c\n", 'A' + pipeIndex);
    
    // Clear enable bit
    u32 pipeConf = readRegister(0x008);
    pipeConf &= ~(1U << 31);
    writeRegister(0x008, pipeConf);
    
    enabled = false;
    
    return true;
}

const char* IntelPipe::getPipeName() const
{
    static const char *names[] = { "Pipe A", "Pipe B", "Pipe C" };
    return names[pipeIndex];
}

void IntelPipe::writeRegister(u32 offset, u32 value)
{
    IntelUncore *uncore = controller->getUncore();
    if (uncore) {
        uncore->writeRegister32_fw(regBase + offset, value);
    }
}

u32 IntelPipe::readRegister(u32 offset)
{
    IntelUncore *uncore = controller->getUncore();
    if (uncore) {
        return uncore->readRegister32_fw(regBase + offset);
    }
    return 0;
}
