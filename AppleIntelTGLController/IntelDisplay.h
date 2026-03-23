/*
 * IntelDisplay.h
 *
 * Display engine management for Intel integrated graphics
 * Handles display pipes, ports, and mode setting
 * Ported from Linux intel_display.c
 */

#ifndef INTEL_DISPLAY_H
#define INTEL_DISPLAY_H

#include <IOKit/IOService.h>
#include <IOKit/graphics/IOFramebuffer.h>
#include "linux_compat.h"

class AppleIntelTGLController;
class IntelPipe;
class IntelPort;
class IntelDPLL;
class IntelModeSet;
// class IntelFramebuffer; // UNUSED - using IntelIOFramebuffer instead

/* Display Configuration */
#define MAX_PIPES       3   // Pipe A, B, C
#define MAX_PORTS       5   // HDMI-A, DP-1, DP-2, DP-3, eDP

/* Display Registers (Gen12 Tiger Lake) */
#define PIPE_CONF_A     0x70008
#define PIPE_CONF_B     0x71008
#define PIPE_CONF_C     0x72008

#define PORT_HOTPLUG_EN 0x61110
#define PORT_HOTPLUG_STAT 0x61114

/* Pipe States */
enum pipe_state {
    PIPE_OFF = 0,
    PIPE_ON,
    PIPE_STANDBY,
};

/* Display Statistics */
struct display_stats {
    u32 mode_sets;
    u32 pipe_enables;
    u32 port_enables;
    u32 hotplugs;
    u32 vblanks;
};

class IntelDisplay : public OSObject {
    OSDeclareDefaultStructors(IntelDisplay)
    
public:
    
    // Initialization
    bool initWithController(AppleIntelTGLController *ctrl);
    void cleanup();
    
    // Display Control
    bool enable();
    bool disable();
    bool reset();
    
    // Pipe Management
    IntelPipe* getPipe(u32 pipeIndex);
    bool enablePipe(u32 pipeIndex);
    bool disablePipe(u32 pipeIndex);
    
    // Port Management
    IntelPort* getPort(u32 portIndex);
    bool detectPorts();
    
    // DPLL (Display PLL) Management
    IntelDPLL* getDPLL(u32 dpllIndex);
    
    // Hotplug
    bool enableHotplug();
    bool disableHotplug();
    void handleHotplug();
    
    // Query
    bool isEnabled() const { return enabled; }
    u32 getActivePipes() const;
    
    // Statistics
    void getStats(struct display_stats *stats);
    void printStats();
    
    // getPrimaryFramebuffer() REMOVED - use IntelIOFramebuffer directly
    // IntelFramebuffer* getPrimaryFramebuffer() const { return primaryFB; }
    
    // Framebuffer accessor methods for IOFramebuffer
    IOBufferMemoryDescriptor* getFramebufferMemory() const { return framebufferMemory; }
    IOPhysicalAddress getFramebufferPhysicalAddress() const { return fbPhysical; }
    void* getFramebufferVirtualAddress() const { return fbVirtual; }
    IOByteCount getFramebufferSize() const { return fbSize; }
    
private:
    AppleIntelTGLController *controller;
    
    // Display State
    bool enabled;
    bool initialized;
    
    // Display Components
    IntelPipe *pipes[MAX_PIPES];
    IntelPort *ports[MAX_PORTS];
    IntelDPLL *dplls[2];  // DPLL 0, 1
    // IntelFramebuffer *primaryFB; // REMOVED - use IntelIOFramebuffer directly
    void* primaryFB;  // Placeholder (unused)
    
    // Framebuffer physical allocation (for IOFramebuffer)
    IOBufferMemoryDescriptor* framebufferMemory;
    IOPhysicalAddress fbPhysical;
    void* fbVirtual;
    IOByteCount fbSize;
    
    // Statistics
    struct display_stats stats;
    IOLock *statsLock;
    
    // Private Methods
    bool initializePipes();
    bool initializePorts();
    bool initializeDPLLs();
    
    bool powerOnDisplay();
    bool powerOffDisplay();
    
    void writeRegister(u32 reg, u32 value);
    u32 readRegister(u32 reg);
    bool setupPrimaryFramebuffer();
    // mapFramebufferIntoGGTT() and enableDisplayOutput() moved to IntelIOFramebuffer::enableController()
};

#endif // INTEL_DISPLAY_H
