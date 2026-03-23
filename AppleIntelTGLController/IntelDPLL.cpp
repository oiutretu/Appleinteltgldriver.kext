/*
 * IntelDPLL.cpp
 */

#include "IntelDPLL.h"
#include "AppleIntelTGLController.h"
#include <IOKit/IOLib.h>

IntelDPLL::IntelDPLL()
    : controller(NULL)
    , dpllIndex(0)
    , enabled(false)
    , frequencyHz(0)
{
}

IntelDPLL::~IntelDPLL()
{
    cleanup();
}

bool IntelDPLL::init(AppleIntelTGLController *ctrl, u32 index)
{
    if (!ctrl) {
        return false;
    }
    
    controller = ctrl;
    dpllIndex = index;
    
    IOLog("IntelDPLL: Initialized DPLL %u\n", index);
    return true;
}

void IntelDPLL::cleanup()
{
    if (enabled) {
        disable();
    }
}

bool IntelDPLL::enable()
{
    IOLog("IntelDPLL: Enabling DPLL %u\n", dpllIndex);
    enabled = true;
    return true;
}

bool IntelDPLL::disable()
{
    IOLog("IntelDPLL: Disabling DPLL %u\n", dpllIndex);
    enabled = false;
    return true;
}

bool IntelDPLL::setFrequency(u64 freq)
{
    IOLog("IntelDPLL: Setting DPLL %u frequency to %llu Hz\n",
          dpllIndex, freq);
    frequencyHz = freq;
    return true;
}
