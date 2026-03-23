/*
 * IntelDisplay.cpp
 *
 * Display engine management implementation
 */

#include "IntelDisplay.h"
#include "AppleIntelTGLController.h"
#include "IntelUncore.h"
#include "IntelPipe.h"
#include "IntelPort.h"
#include "IntelDPLL.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelDisplay, OSObject)

bool IntelDisplay::initWithController(AppleIntelTGLController *ctrl)
{
    if (!ctrl) {
        IOLog("IntelDisplay: Invalid controller\n");
        return false;
    }
    
    controller = ctrl;
    
    // Initialize framebuffer variables (but don't allocate - IOFramebuffer handles that)
    framebufferMemory = NULL;
    fbPhysical = 0;
    fbVirtual = NULL;
    fbSize = 0;
    
    IOLog("IntelDisplay:   PASSIVE MODE - Display management handled by IntelIOFramebuffer\n");
    IOLog("IntelDisplay: AppleIntelTGLController is for GPU command submission only\n");
    
    // Create statistics lock
    statsLock = IOLockAlloc();
    if (!statsLock) {
        IOLog("IntelDisplay: Failed to allocate stats lock\n");
        return false;
    }
    
    // SKIP all display hardware initialization - IOFramebuffer does this
    // We're only here to provide a placeholder for future display-related GPU commands
    
    initialized = true;
    
    IOLog("IntelDisplay: OK  Passive display manager initialized (no hardware touched)\n");
    return true;
}

void IntelDisplay::cleanup()
{
    IOLog("IntelDisplay: Cleanup\n");
    
    // Release framebuffer memory descriptor
    if (framebufferMemory) {
        framebufferMemory->complete();
        framebufferMemory->release();
        framebufferMemory = NULL;
    }
    fbPhysical = 0;
    fbVirtual = NULL;
    fbSize = 0;
    
    // Release framebuffer (unused placeholder)
    // primaryFB was never actually allocated, just a placeholder
    primaryFB = NULL;
    
    // Disable display
    if (enabled) {
        disable();
    }
    
    // Cleanup ports
    for (int i = 0; i < MAX_PORTS; i++) {
        if (ports[i]) {
            ports[i]->cleanup();
            delete ports[i];
            ports[i] = NULL;
        }
    }
    
    // Cleanup pipes
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i]) {
            pipes[i]->cleanup();
            delete pipes[i];
            pipes[i] = NULL;
        }
    }
    
    // Cleanup DPLLs
    for (int i = 0; i < 2; i++) {
        if (dplls[i]) {
            dplls[i]->cleanup();
            delete dplls[i];
            dplls[i] = NULL;
        }
    }
    
    // Free locks
    if (statsLock) {
        IOLockFree(statsLock);
        statsLock = NULL;
    }
    
    printStats();
    IOLog("IntelDisplay: Cleanup complete\n");
}

bool IntelDisplay::initializeDPLLs()
{
    IOLog("IntelDisplay: Initializing DPLLs\n");
    
    for (int i = 0; i < 2; i++) {
        dplls[i] = new IntelDPLL();
        if (!dplls[i]) {
            IOLog("IntelDisplay: Failed to allocate DPLL %d\n", i);
            return false;
        }
        
        if (!dplls[i]->init(controller, i)) {
            IOLog("IntelDisplay: Failed to initialize DPLL %d\n", i);
            return false;
        }
    }
    
    IOLog("IntelDisplay: DPLLs initialized\n");
    return true;
}

bool IntelDisplay::initializePipes()
{
    IOLog("IntelDisplay: Initializing display pipes\n");
    
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i] = new IntelPipe();
        if (!pipes[i]) {
            IOLog("IntelDisplay: Failed to allocate pipe %c\n", 'A' + i);
            return false;
        }
        
        if (!pipes[i]->init(controller, i)) {
            IOLog("IntelDisplay: Failed to initialize pipe %c\n", 'A' + i);
            return false;
        }
    }
    
    IOLog("IntelDisplay: Pipes initialized (A, B, C)\n");
    return true;
}

bool IntelDisplay::initializePorts()
{
    IOLog("IntelDisplay: Initializing display ports\n");
    
    // Tiger Lake has multiple ports: HDMI, DP, eDP
    // For now, create basic port objects
    
    for (int i = 0; i < MAX_PORTS; i++) {
        ports[i] = new IntelPort();
        if (!ports[i]) {
            IOLog("IntelDisplay: Failed to allocate port %d\n", i);
            return false;
        }
        
        if (!ports[i]->init(controller, i)) {
            IOLog("IntelDisplay: Failed to initialize port %d\n", i);
            return false;
        }
    }
    
    IOLog("IntelDisplay: Ports initialized\n");
    return true;
}

bool IntelDisplay::enable()
{
    if (enabled) {
        return true;
    }
    
    IOLog("IntelDisplay:   enable() called - SKIPPING (display managed by IntelIOFramebuffer)\n");
    IOLog("IntelDisplay: AppleIntelTGLController handles GPU commands only, not display hardware\n");
    
    // Mark as enabled without touching any hardware
    enabled = true;
    
    return true;
}

bool IntelDisplay::disable()
{
    if (!enabled) {
        return true;
    }
    
    IOLog("IntelDisplay: Disabling display engine\n");
    
    // Disable all pipes
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i]) {
            disablePipe(i);
        }
    }
    
    // Disable hotplug
    disableHotplug();
    
    // Power off display
    powerOffDisplay();
    
    enabled = false;
    
    IOLog("IntelDisplay: Display engine disabled\n");
    return true;
}

bool IntelDisplay::reset()
{
    IOLog("IntelDisplay: Resetting display engine\n");
    
    disable();
    IOSleep(10);  // Wait 10ms
    enable();
    
    return true;
}

IntelPipe* IntelDisplay::getPipe(u32 pipeIndex)
{
    if (pipeIndex >= MAX_PIPES) {
        return NULL;
    }
    
    return pipes[pipeIndex];
}

bool IntelDisplay::enablePipe(u32 pipeIndex)
{
    if (pipeIndex >= MAX_PIPES || !pipes[pipeIndex]) {
        return false;
    }
    
    IOLog("IntelDisplay: Enabling pipe %c\n", 'A' + pipeIndex);
    
    if (!pipes[pipeIndex]->enable()) {
        IOLog("IntelDisplay: Failed to enable pipe %c\n", 'A' + pipeIndex);
        return false;
    }
    
    // Update statistics
    IOLockLock(statsLock);
    stats.pipe_enables++;
    IOLockUnlock(statsLock);
    
    return true;
}

bool IntelDisplay::disablePipe(u32 pipeIndex)
{
    if (pipeIndex >= MAX_PIPES || !pipes[pipeIndex]) {
        return false;
    }
    
    IOLog("IntelDisplay: Disabling pipe %c\n", 'A' + pipeIndex);
    
    return pipes[pipeIndex]->disable();
}
                                                                                                                                                                
IntelPort* IntelDisplay::getPort(u32 portIndex)
{
    if (portIndex >= MAX_PORTS) {
        return NULL;
    }
    
    return ports[portIndex];
}

bool IntelDisplay::detectPorts()
{
    IOLog("IntelDisplay: Detecting Connected Displays and Hardware State\n");
    
    // Read current pipe status from hardware
    IOLog("IntelDisplay: Reading pipe configuration from hardware...\n");
    
    // Tiger Lake regis                                                                       ter addresses (proven working from FakeIrisXEFramebuffer)
    const u32 PIPECONF_A = 0x70008;
    const u32 PLANE_CTL_1_A = 0x70180;
    const u32 PLANE_SURF_1_A = 0x7019C;
    const u32 PLANE_STRIDE_1_A = 0x70188;
    const u32 PLANE_SIZE_1_A = 0x70190;
    const u32 DDI_BUF_CTL_A = 0x64000;
    
    // Safe register reads (these are proven to work on Tiger Lake)
    IOLog("IntelDisplay: Reading Tiger Lake display state (safe registers)...\n");
    
    u32 pipeAConf = readRegister(PIPECONF_A);
    u32 planeCtlA = readRegister(PLANE_CTL_1_A);
    u32 planeSurfA = readRegister(PLANE_SURF_1_A);
    u32 planeStrideA = readRegister(PLANE_STRIDE_1_A);
    u32 planeSizeA = readRegister(PLANE_SIZE_1_A);
    u32 ddiCtlA = readRegister(DDI_BUF_CTL_A);
    
    bool pipeAEnabled = (pipeAConf & 0x80000000) != 0;  // Bit 31 = enable
    bool planeAEnabled = (planeCtlA & 0x80000000) != 0;
    bool ddiAEnabled = (ddiCtlA & 0x80000000) != 0;
    
    IOLog("IntelDisplay: Pipe A (Internal Display) - Tiger Lake State:\n");
    IOLog("  PIPECONF_A:      0x%08x - %s\n", pipeAConf, pipeAEnabled ? "ENABLED OK " : "disabled");
    IOLog("  PLANE_CTL_1_A:   0x%08x - %s\n", planeCtlA, planeAEnabled ? "ENABLED OK " : "disabled");
    IOLog("  PLANE_SURF_1_A:  0x%08x (framebuffer address)\n", planeSurfA);
    IOLog("  PLANE_STRIDE_1_A:0x%08x (%u bytes = %u blocks)\n",
          planeStrideA, planeStrideA * 64, planeStrideA);
    IOLog("  PLANE_SIZE_1_A:  0x%08x (%ux%u)\n",
          planeSizeA,
          (planeSizeA & 0xFFFF) + 1,
          ((planeSizeA >> 16) & 0xFFFF) + 1);
    IOLog("  DDI_BUF_CTL_A:   0x%08x - %s\n", ddiCtlA, ddiAEnabled ? "ENABLED OK " : "disabled");
    
    // Decode pixel format from PLANE_CTL
    u32 format = (planeCtlA >> 24) & 0xF;
    const char* formatName = "Unknown";
    switch (format) {
        case 0x4: formatName = "XRGB8888"; break;
        case 0x5: formatName = "XBGR8888"; break;
        case 0x6: formatName = "ARGB8888"; break;
        case 0x7: formatName = "ABGR8888"; break;
    }
    IOLog("  Pixel Format:    %s (0x%x)\n", formatName, format);
    
    // Check tiling mode
    u32 tiling = (planeCtlA >> 10) & 0x3;
    const char* tilingName = (tiling == 0) ? "Linear (untiled)" :
                             (tiling == 1) ? "X-tiled" :
                             (tiling == 2) ? "Y-tiled" : "Reserved";
    IOLog("  Tiling Mode:     %s\n", tilingName);
    
    
    // Skip port scanning for now - we know BIOS has configured Pipe A
    IOLog("IntelDisplay: Summary: Using BIOS-configured display\n");
    IOLog("IntelDisplay:   Pipe A: %s\n", pipeAEnabled ? "ACTIVE OK " : "inactive");
    IOLog("IntelDisplay:   Plane A: %s\n", planeAEnabled ? "ACTIVE OK " : "inactive");
    IOLog("IntelDisplay:   DDI A: %s\n", ddiAEnabled ? "ACTIVE OK " : "inactive");
    
    if (pipeAEnabled || planeAEnabled) {
        IOLog("IntelDisplay: OK  Display hardware is ALREADY CONFIGURED by BIOS!\n");
        IOLog("IntelDisplay: We can use this existing configuration!\n");
    } else {
        IOLog("IntelDisplay:   Display hardware not configured by BIOS\n");
        IOLog("IntelDisplay: Will need to initialize from scratch\n");
    }
    
    
    return true;  // Always succeed - we'll use BIOS config or initialize fresh
}
IntelDPLL* IntelDisplay::getDPLL(u32 dpllIndex)
{
    if (dpllIndex >= 2) {
        return NULL;
    }
    
    return dplls[dpllIndex];
}

bool IntelDisplay::enableHotplug()
{
    IOLog("IntelDisplay: Enabling hotplug detection\n");
    
    // IMPORTANT: Don't blindly write 0xFFFFFFFF - this can break working displays!
    // Only enable specific hotplug bits that are safe
    
    // For now, skip hotplug setup entirely - it's not critical for basic display
    IOLog("IntelDisplay: Hotplug detection skipped (not needed for basic operation)\n");
    
    return true;
}

bool IntelDisplay::disableHotplug()
{
    IOLog("IntelDisplay: Disabling hotplug detection\n");
    
    writeRegister(PORT_HOTPLUG_EN, 0);
    
    return true;
}

void IntelDisplay::handleHotplug()
{
    // Read hotplug status
    u32 status = readRegister(PORT_HOTPLUG_STAT);
    
    if (status == 0) {
        return;
    }
    
    IOLog("IntelDisplay: Hotplug event: 0x%08x\n", status);
    
    // Clear status
    writeRegister(PORT_HOTPLUG_STAT, status);
    
    // Re-detect ports
    detectPorts();
    
    // Update statistics
    IOLockLock(statsLock);
    stats.hotplugs++;
    IOLockUnlock(statsLock);
}

bool IntelDisplay::powerOnDisplay()
{
    IOLog("IntelDisplay: Powering on display\n");
    
    // In a full implementation:
    // 1. Enable display power wells
    // 2. Release display reset
    // 3. Initialize display clocks
    // 4. Setup CDCLK (core display clock)
    
    // For now, just a stub
    IOSleep(1);  // Wait 1ms
    
    return true;
}

bool IntelDisplay::powerOffDisplay()
{
    IOLog("IntelDisplay: Powering off display\n");
    
    // Reverse of power on
    
    return true;
}

void IntelDisplay::writeRegister(u32 reg, u32 value)
{
    IntelUncore *uncore = controller->getUncore();
    if (!uncore) {
        return;
    }
    
    // For Tiger Lake display registers, use direct access without forcewake
    uncore->writeRegister32(reg, value);  // Direct write, no forcewake
}

u32 IntelDisplay::readRegister(u32 reg)
{
    IntelUncore *uncore = controller->getUncore();
    if (!uncore) {
        return 0;
    }
    
    // For Tiger Lake display registers, use direct access without forcewake
    // Display registers don't need GT forcewake
    return uncore->readRegister32(reg);  // Direct read, no forcewake wait
}

u32 IntelDisplay::getActivePipes() const
{
    u32 count = 0;
    
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i] && pipes[i]->isEnabled()) {
            count++;
        }
    }
    
    return count;
}

void IntelDisplay::getStats(struct display_stats *out)
{
    if (!out) {
        return;
    }
    
    IOLockLock(statsLock);
    memcpy(out, &stats, sizeof(struct display_stats));
    IOLockUnlock(statsLock);
}

void IntelDisplay::printStats()
{
    IOLog("IntelDisplay Statistics:\n");
    IOLog("  Mode sets: %u\n", stats.mode_sets);
    IOLog("  Pipe enables: %u\n", stats.pipe_enables);
    IOLog("  Port enables: %u\n", stats.port_enables);
    IOLog("  Hotplugs: %u\n", stats.hotplugs);
    IOLog("  VBlanks: %u\n", stats.vblanks);
    IOLog("  Active pipes: %u\n", getActivePipes());
}

bool IntelDisplay::setupPrimaryFramebuffer()
{
    IOLog("IntelDisplay:   setupPrimaryFramebuffer() - SKIPPED (framebuffer now allocated in IOFramebuffer::enableController())\n");
    
    // Initialize member variables to NULL/0
    // Framebuffer allocation is now done in IntelIOFramebuffer::enableController() to match reference code
    this->framebufferMemory = nullptr;
    this->fbPhysical = 0;
    this->fbVirtual = nullptr;
    this->fbSize = 0;
    
    IOLog("IntelDisplay: OK  Primary framebuffer setup deferred to IOFramebuffer\n");
    return true;
}
// NOTE: GGTT mapping and display pipeline training removed from IntelDisplay.
// These functions are now in IntelIOFramebuffer::enableController() to match
// reference working code architecture exactly.
