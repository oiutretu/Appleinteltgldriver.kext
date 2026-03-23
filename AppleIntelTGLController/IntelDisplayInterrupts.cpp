/*
 * IntelDisplayInterrupts.cpp
 * Intel Display Interrupt Handling Implementation
 */

#include "IntelDisplayInterrupts.h"
#include "AppleIntelTGLController.h"
#include "IntelGTInterrupts.h"
#include "IntelDisplay.h"
#include "IntelPipe.h"
#include "IntelPort.h"
#include <IOKit/IOLib.h>
#include <mach/mach_time.h>





// Static timebase info to avoid compiler bug with mach_timebase_info
static struct {
    uint32_t numer;
    uint32_t denom;
} s_timebase = {1, 1};

static bool s_timebase_initialized = false;

static void init_timebase() {
    if (!s_timebase_initialized) {
        // Use default timebase values for x86_64
        // On x86 platforms, timebase is typically 1:1 with nanoseconds
        s_timebase.numer = 1;
        s_timebase.denom = 1;
        s_timebase_initialized = true;
    }
}

#define super OSObject
OSDefineMetaClassAndStructors(IntelDisplayInterrupts, OSObject)

/* Initialization */
bool IntelDisplayInterrupts::init() {
    if (!super::init()) {
        return false;
    }
    
    controller = nullptr;
    display = nullptr;
    workLoop = nullptr;
    interruptSource = nullptr;
    
    // Initialize handler lists
    for (int i = 0; i < 3; i++) {
        vblankHandlers[i] = nullptr;
        vblankCounter[i] = 0;
        lastVblankTime[i] = 0;
        vblankPending[i] = false;
        flipPending[i] = false;
        flipStartTime[i] = 0;
    }
    
    for (int i = 0; i < 5; i++) {
        hotplugHandlers[i] = nullptr;
    }
    
    enabledInterrupts = 0;
    vblankEnabled = 0;
    hotplugEnabled = 0;
    
    // Clear statistics
    memset(&stats, 0, sizeof(stats));
    handlerStartTime = 0;
    
    // Create locks
    interruptLock = IOLockAlloc();
    vblankLock = IOLockAlloc();
    hotplugLock = IOLockAlloc();
    
    if (!interruptLock || !vblankLock || !hotplugLock) {
        return false;
    }
    
    isStarted = false;
    interruptsRegistered = false;
    
    return true;
}

void IntelDisplayInterrupts::free() {
    // Stop if still running
    if (isStarted) {
        stop();
    }
    
    // Free locks
    if (interruptLock) {
        IOLockFree(interruptLock);
        interruptLock = nullptr;
    }
    if (vblankLock) {
        IOLockFree(vblankLock);
        vblankLock = nullptr;
    }
    if (hotplugLock) {
        IOLockFree(hotplugLock);
        hotplugLock = nullptr;
    }
    
    super::free();
}

bool IntelDisplayInterrupts::initWithController(AppleIntelTGLController* ctrl) {
    if (!ctrl) {
        return false;
    }
    
    controller = ctrl;
    display = controller->getDisplay();
    workLoop = controller->getWorkLoop();
    
    if (!display || !workLoop) {
        return false;
    }
    
    return true;
}

bool IntelDisplayInterrupts::start() {
    if (isStarted) {
        return true;
    }
    
    IOLog("IntelDisplayInterrupts::start()\n");
    
    // Register interrupt handler
    if (!registerInterruptHandler()) {
        IOLog("Failed to register interrupt handler\n");
        return false;
    }
    
    // Clear any pending interrupts
    uint32_t status = readInterruptStatus();
    clearInterruptStatus(status);
    
    // Initially disable all interrupts
    writeInterruptMask(0xFFFFFFFF);
    writeInterruptEnable(0);
    
    isStarted = true;
    IOLog("Display interrupts started\n");
    
    return true;
}

void IntelDisplayInterrupts::stop() {
    if (!isStarted) {
        return;
    }
    
    IOLog("IntelDisplayInterrupts::stop()\n");
    
    // Disable all interrupts
    disableInterrupts(DISP_INT_ALL);
    
    // Unregister interrupt handler
    unregisterInterruptHandler();
    
    // Free all handler lists
    IOLockLock(vblankLock);
    for (int i = 0; i < 3; i++) {
        VblankHandler* handler = vblankHandlers[i];
        while (handler) {
            VblankHandler* next = handler->next;
            IOFree(handler, sizeof(VblankHandler));
            handler = next;
        }
        vblankHandlers[i] = nullptr;
    }
    IOLockUnlock(vblankLock);
    
    IOLockLock(hotplugLock);
    for (int i = 0; i < 5; i++) {
        HotplugHandler* handler = hotplugHandlers[i];
        while (handler) {
            HotplugHandler* next = handler->next;
            IOFree(handler, sizeof(HotplugHandler));
            handler = next;
        }
        hotplugHandlers[i] = nullptr;
    }
    IOLockUnlock(hotplugLock);
    
    isStarted = false;
    IOLog("Display interrupts stopped\n");
}

/* Interrupt registration */
bool IntelDisplayInterrupts::registerInterruptHandler() {
    if (interruptsRegistered) {
        return true;
    }
    
    // Get PCI device from controller (which got it from IntelIOFramebuffer)
    IOPCIDevice* pciDevice = controller->getPCIDevice();
    if (!pciDevice) {
        IOLog("ERR  No PCI device available for interrupts\n");
        return false;
    }
    
    // Create hardware interrupt event source on shared interrupt line 0
        // Both display and GT interrupts share this line on integrated GPUs
        IOLog(" Registering hardware interrupt handler on PCI interrupt line 0 (shared display+GT)\n");
        
        interruptSource = IOInterruptEventSource::interruptEventSource(
            this,
            (IOInterruptEventAction)&IntelDisplayInterrupts::interruptOccurred,
            pciDevice,
            0  // Interrupt index 0 - shared line for display + GT
        );
        
    
    if (!interruptSource) {
        IOLog("ERR  Failed to create interrupt event source\n");
        return false;
    }
    
    // Add to work loop
    if (workLoop->addEventSource(interruptSource) != kIOReturnSuccess) {
        IOLog("ERR  Failed to add interrupt source to work loop\n");
        interruptSource->release();
        interruptSource = nullptr;
        return false;
    }
    
    // Enable the interrupt source
    interruptSource->enable();
    
    interruptsRegistered = true;
    IOLog("OK  Hardware interrupt registered on line 0 - Display+GT shared dispatch ready\n");
    
    return true;
}

void IntelDisplayInterrupts::unregisterInterruptHandler() {
    if (!interruptsRegistered || !interruptSource) {
        return;
    }
    
    // Disable interrupt source
    interruptSource->disable();
    
    // Remove from work loop and release
    workLoop->removeEventSource(interruptSource);
    interruptSource->release();
    interruptSource = nullptr;
    
    interruptsRegistered = false;
    IOLog("Hardware interrupt unregistered\n");
}

/* Hardware interrupt callback */
void IntelDisplayInterrupts::interruptOccurred(OSObject* owner, IOInterruptEventSource* sender, int count) {
    IntelDisplayInterrupts* self = OSDynamicCast(IntelDisplayInterrupts, owner);
    if (self) {
        self->handleInterrupt();
    }
}

/* Enable/disable interrupts */
bool IntelDisplayInterrupts::enableInterrupts(uint32_t types) {
    IOLockLock(interruptLock);
    
    enabledInterrupts |= types;
    
    // Build hardware interrupt enable mask
    uint32_t hwMask = 0;
    
    if (types & DISP_INT_VBLANK) {
        // Enable vblank for all pipes that are enabled
        for (int pipe = 0; pipe < 3; pipe++) {
            if (vblankEnabled & (1 << pipe)) {
                uint32_t pipeIER = controller->readRegister32(GEN11_DE_PIPE_IER(pipe));
                pipeIER |= GEN8_PIPE_VBLANK;
                controller->writeRegister32(GEN11_DE_PIPE_IER(pipe), pipeIER);
            }
        }
    }
    
    if (types & DISP_INT_HOTPLUG) {
        // Enable hotplug for all ports that are enabled
        uint32_t portIER = controller->readRegister32(GEN11_DE_PORT_IER);
        portIER |= (GEN8_PORT_DP_A_HOTPLUG | GEN8_PORT_DP_B_HOTPLUG |
                    GEN8_PORT_DP_C_HOTPLUG | GEN8_PORT_DP_D_HOTPLUG);
        controller->writeRegister32(GEN11_DE_PORT_IER, portIER);
    }
    
    if (types & DISP_INT_FLIP_DONE) {
        for (int pipe = 0; pipe < 3; pipe++) {
            uint32_t pipeIER = controller->readRegister32(GEN11_DE_PIPE_IER(pipe));
            pipeIER |= GEN8_PIPE_FLIP_DONE;
            controller->writeRegister32(GEN11_DE_PIPE_IER(pipe), pipeIER);
        }
    }
    
    if (types & DISP_INT_FIFO_UNDERRUN) {
        for (int pipe = 0; pipe < 3; pipe++) {
            uint32_t pipeIER = controller->readRegister32(GEN11_DE_PIPE_IER(pipe));
            pipeIER |= GEN8_PIPE_FIFO_UNDERRUN;
            controller->writeRegister32(GEN11_DE_PIPE_IER(pipe), pipeIER);
        }
    }
    
    // Enable master interrupt
    uint32_t masterCtl = controller->readRegister32(GEN11_DISPLAY_INT_CTL);
    masterCtl |= (1 << 31);  // Master interrupt enable
    controller->writeRegister32(GEN11_DISPLAY_INT_CTL, masterCtl);
    
    IOLockUnlock(interruptLock);
    
    IOLog("Enabled display interrupts: 0x%x\n", types);
    return true;
}

void IntelDisplayInterrupts::disableInterrupts(uint32_t types) {
    IOLockLock(interruptLock);
    
    enabledInterrupts &= ~types;
    
    if (types & DISP_INT_VBLANK) {
        for (int pipe = 0; pipe < 3; pipe++) {
            uint32_t pipeIER = controller->readRegister32(GEN11_DE_PIPE_IER(pipe));
            pipeIER &= ~GEN8_PIPE_VBLANK;
            controller->writeRegister32(GEN11_DE_PIPE_IER(pipe), pipeIER);
        }
    }
    
    if (types & DISP_INT_HOTPLUG) {
        uint32_t portIER = controller->readRegister32(GEN11_DE_PORT_IER);
        portIER &= ~(GEN8_PORT_DP_A_HOTPLUG | GEN8_PORT_DP_B_HOTPLUG |
                     GEN8_PORT_DP_C_HOTPLUG | GEN8_PORT_DP_D_HOTPLUG);
        controller->writeRegister32(GEN11_DE_PORT_IER, portIER);
    }
    
    if (types & DISP_INT_FLIP_DONE) {
        for (int pipe = 0; pipe < 3; pipe++) {
            uint32_t pipeIER = controller->readRegister32(GEN11_DE_PIPE_IER(pipe));
            pipeIER &= ~GEN8_PIPE_FLIP_DONE;
            controller->writeRegister32(GEN11_DE_PIPE_IER(pipe), pipeIER);
        }
    }
    
    // Disable master interrupt if all interrupts disabled
    if (enabledInterrupts == 0) {
        uint32_t masterCtl = controller->readRegister32(GEN11_DISPLAY_INT_CTL);
        masterCtl &= ~(1 << 31);
        controller->writeRegister32(GEN11_DISPLAY_INT_CTL, masterCtl);
    }
    
    IOLockUnlock(interruptLock);
    
    IOLog("Disabled display interrupts: 0x%x\n", types);
}

bool IntelDisplayInterrupts::isInterruptEnabled(uint32_t type) {
    IOLockLock(interruptLock);
    bool enabled = (enabledInterrupts & type) != 0;
    IOLockUnlock(interruptLock);
    return enabled;
}

/* Vblank management */
bool IntelDisplayInterrupts::enableVblank(uint32_t pipe) {
    if (pipe >= 3) {
        return false;
    }
    
    IOLockLock(interruptLock);
    
    vblankEnabled |= (1 << pipe);
    
    // Enable vblank interrupt for this pipe
    if (enabledInterrupts & DISP_INT_VBLANK) {
        uint32_t pipeIER = controller->readRegister32(GEN11_DE_PIPE_IER(pipe));
        pipeIER |= GEN8_PIPE_VBLANK;
        controller->writeRegister32(GEN11_DE_PIPE_IER(pipe), pipeIER);
    }
    
    IOLockUnlock(interruptLock);
    
    IOLog("Vblank enabled for pipe %d\n", pipe);
    return true;
}

void IntelDisplayInterrupts::disableVblank(uint32_t pipe) {
    if (pipe >= 3) {
        return;
    }
    
    IOLockLock(interruptLock);
    
    vblankEnabled &= ~(1 << pipe);
    
    // Disable vblank interrupt for this pipe
    uint32_t pipeIER = controller->readRegister32(GEN11_DE_PIPE_IER(pipe));
    pipeIER &= ~GEN8_PIPE_VBLANK;
    controller->writeRegister32(GEN11_DE_PIPE_IER(pipe), pipeIER);
    
    IOLockUnlock(interruptLock);
    
    IOLog("Vblank disabled for pipe %d\n", pipe);
}

bool IntelDisplayInterrupts::registerVblankHandler(uint32_t pipe, VblankCallback callback, void* context) {
    if (pipe >= 3 || !callback) {
        return false;
    }
    
    // Allocate new handler
    VblankHandler* handler = (VblankHandler*)IOMalloc(sizeof(VblankHandler));
    if (!handler) {
        return false;
    }
    
    handler->callback = callback;
    handler->context = context;
    handler->pipe = pipe;
    handler->enabled = true;
    
    // Add to list
    IOLockLock(vblankLock);
    handler->next = vblankHandlers[pipe];
    vblankHandlers[pipe] = handler;
    IOLockUnlock(vblankLock);
    
    IOLog("Registered vblank handler for pipe %d\n", pipe);
    return true;
}

void IntelDisplayInterrupts::unregisterVblankHandler(uint32_t pipe, VblankCallback callback) {
    if (pipe >= 3 || !callback) {
        return;
    }
    
    IOLockLock(vblankLock);
    
    VblankHandler** prev = &vblankHandlers[pipe];
    VblankHandler* handler = vblankHandlers[pipe];
    
    while (handler) {
        if (handler->callback == callback) {
            *prev = handler->next;
            IOFree(handler, sizeof(VblankHandler));
            IOLog("Unregistered vblank handler for pipe %d\n", pipe);
            break;
        }
        prev = &handler->next;
        handler = handler->next;
    }
    
    IOLockUnlock(vblankLock);
}

uint32_t IntelDisplayInterrupts::getVblankCounter(uint32_t pipe) {
    if (pipe >= 3) {
        return 0;
    }
    
    IOLockLock(interruptLock);
    uint32_t counter = vblankCounter[pipe];
    IOLockUnlock(interruptLock);
    
    return counter;
}

bool IntelDisplayInterrupts::waitForVblank(uint32_t pipe, uint32_t timeout_ms) {
    if (pipe >= 3) {
        return false;
    }
    
    uint32_t startCounter = getVblankCounter(pipe);
    uint64_t startTime = mach_absolute_time();
    
    // Convert timeout to absolute time
    init_timebase();
    uint64_t timeoutNs = timeout_ms * 1000000ULL;
    uint64_t timeoutAbs = (timeoutNs * s_timebase.denom) / s_timebase.numer;
    
    // Wait for counter to increment
    while (getVblankCounter(pipe) == startCounter) {
        uint64_t now = mach_absolute_time();
        if ((now - startTime) >= timeoutAbs) {
            stats.vblankMissed[pipe]++;
            return false;  // Timeout
        }
        IOSleep(1);  // Sleep 1ms
    }
    
    return true;
}

/* Hotplug management */
bool IntelDisplayInterrupts::enableHotplug(uint32_t port) {
    if (port >= 5) {
        return false;
    }
    
    IOLockLock(interruptLock);
    
    hotplugEnabled |= (1 << port);
    
    // Enable hotplug interrupt for this port
    if (enabledInterrupts & DISP_INT_HOTPLUG) {
        uint32_t portIER = controller->readRegister32(GEN11_DE_PORT_IER);
        portIER |= (1 << (port + 3));  // DP_A starts at bit 3
        controller->writeRegister32(GEN11_DE_PORT_IER, portIER);
    }
    
    IOLockUnlock(interruptLock);
    
    IOLog("Hotplug enabled for port %d\n", port);
    return true;
}

void IntelDisplayInterrupts::disableHotplug(uint32_t port) {
    if (port >= 5) {
        return;
    }
    
    IOLockLock(interruptLock);
    
    hotplugEnabled &= ~(1 << port);
    
    uint32_t portIER = controller->readRegister32(GEN11_DE_PORT_IER);
    portIER &= ~(1 << (port + 3));
    controller->writeRegister32(GEN11_DE_PORT_IER, portIER);
    
    IOLockUnlock(interruptLock);
    
    IOLog("Hotplug disabled for port %d\n", port);
}

bool IntelDisplayInterrupts::registerHotplugHandler(uint32_t port, HotplugCallback callback, void* context) {
    if (port >= 5 || !callback) {
        return false;
    }
    
    HotplugHandler* handler = (HotplugHandler*)IOMalloc(sizeof(HotplugHandler));
    if (!handler) {
        return false;
    }
    
    handler->callback = callback;
    handler->context = context;
    handler->port = port;
    handler->enabled = true;
    
    IOLockLock(hotplugLock);
    handler->next = hotplugHandlers[port];
    hotplugHandlers[port] = handler;
    IOLockUnlock(hotplugLock);
    
    IOLog("Registered hotplug handler for port %d\n", port);
    return true;
}

void IntelDisplayInterrupts::unregisterHotplugHandler(uint32_t port, HotplugCallback callback) {
    if (port >= 5 || !callback) {
        return;
    }
    
    IOLockLock(hotplugLock);
    
    HotplugHandler** prev = &hotplugHandlers[port];
    HotplugHandler* handler = hotplugHandlers[port];
    
    while (handler) {
        if (handler->callback == callback) {
            *prev = handler->next;
            IOFree(handler, sizeof(HotplugHandler));
            IOLog("Unregistered hotplug handler for port %d\n", port);
            break;
        }
        prev = &handler->next;
        handler = handler->next;
    }
    
    IOLockUnlock(hotplugLock);
}

void IntelDisplayInterrupts::triggerHotplugDetection(uint32_t port) {
    if (port >= 5) {
        return;
    }
    
    IOLog("Triggering hotplug detection for port %d\n", port);
    handleHotplug(port);
}

/* Page flip management */
bool IntelDisplayInterrupts::waitForFlipComplete(uint32_t pipe, uint32_t timeout_ms) {
    if (pipe >= 3) {
        return false;
    }
    
    if (!flipPending[pipe]) {
        return true;  // No flip pending
    }
    
    uint64_t startTime = mach_absolute_time();
    init_timebase();
    uint64_t timeoutNs = timeout_ms * 1000000ULL;
    uint64_t timeoutAbs = (timeoutNs * s_timebase.denom) / s_timebase.numer;
    
    while (flipPending[pipe]) {
        uint64_t now = mach_absolute_time();
        if ((now - startTime) >= timeoutAbs) {
            updateFlipStats(pipe, true);
            return false;  // Timeout
        }
        IOSleep(1);
    }
    
    updateFlipStats(pipe, false);
    return true;
}

void IntelDisplayInterrupts::notifyFlipPending(uint32_t pipe) {
    if (pipe >= 3) {
        return;
    }
    
    IOLockLock(interruptLock);
    flipPending[pipe] = true;
    flipStartTime[pipe] = mach_absolute_time();
    IOLockUnlock(interruptLock);
}

/* Error handling */
void IntelDisplayInterrupts::clearPipeErrors(uint32_t pipe) {
    if (pipe >= 3) {
        return;
    }
    
    // Clear all error bits for this pipe
    uint32_t pipeIIR = controller->readRegister32(GEN11_DE_PIPE_IIR(pipe));
    controller->writeRegister32(GEN11_DE_PIPE_IIR(pipe), pipeIIR);
    
    IOLog("Cleared pipe %d errors\n", pipe);
}

bool IntelDisplayInterrupts::hasPipeError(uint32_t pipe) {
    if (pipe >= 3) {
        return false;
    }
    
    uint32_t pipeISR = controller->readRegister32(GEN11_DE_PIPE_ISR(pipe));
    return (pipeISR & (GEN8_PIPE_FIFO_UNDERRUN)) != 0;
}

void IntelDisplayInterrupts::enableErrorReporting(uint32_t pipe) {
    if (pipe >= 3) {
        return;
    }
    
    uint32_t pipeIER = controller->readRegister32(GEN11_DE_PIPE_IER(pipe));
    pipeIER |= GEN8_PIPE_FIFO_UNDERRUN;
    controller->writeRegister32(GEN11_DE_PIPE_IER(pipe), pipeIER);
    
    IOLog("Error reporting enabled for pipe %d\n", pipe);
}

void IntelDisplayInterrupts::disableErrorReporting(uint32_t pipe) {
    if (pipe >= 3) {
        return;
    }
    
    uint32_t pipeIER = controller->readRegister32(GEN11_DE_PIPE_IER(pipe));
    pipeIER &= ~GEN8_PIPE_FIFO_UNDERRUN;
    controller->writeRegister32(GEN11_DE_PIPE_IER(pipe), pipeIER);
    
    IOLog("Error reporting disabled for pipe %d\n", pipe);
}



void IntelDisplayInterrupts::handleInterrupt() {
    handlerStartTime = mach_absolute_time();
    
    // Read master interrupt status
    uint32_t masterStatus = controller->readRegister32(GEN11_DISPLAY_INT_CTL);
    if (!(masterStatus & (1 << 31))) {
        stats.spuriousInterrupts++;
        return;  // Not our interrupt
    }
    
    stats.totalInterrupts++;
    
    // Handle pipe interrupts
    for (int pipe = 0; pipe < 3; pipe++) {
        uint32_t pipeIIR = controller->readRegister32(GEN11_DE_PIPE_IIR(pipe));
        if (pipeIIR == 0) {
            continue;
        }
        
        // Vblank
        if (pipeIIR & GEN8_PIPE_VBLANK) {
            handleVblank(pipe);
        }
        
        // Flip complete
        if (pipeIIR & GEN8_PIPE_FLIP_DONE) {
            handleFlipComplete(pipe);
        }
        
        // FIFO underrun
        if (pipeIIR & GEN8_PIPE_FIFO_UNDERRUN) {
            handleFifoUnderrun(pipe);
        }
        
        // Clear handled interrupts
        controller->writeRegister32(GEN11_DE_PIPE_IIR(pipe), pipeIIR);
    }
    
    // Handle port interrupts (hotplug)
    uint32_t portIIR = controller->readRegister32(GEN11_DE_PORT_IIR);
    if (portIIR != 0) {
        for (int port = 0; port < 4; port++) {
            if (portIIR & (1 << (port + 3))) {
                handleHotplug(port);
            }
        }
        controller->writeRegister32(GEN11_DE_PORT_IIR, portIIR);
    }
    
    //  CRITICAL: Also dispatch GT interrupts on shared interrupt line!
    IntelGTInterrupts* gtInts = controller->getGTInterrupts();
    if (gtInts) {
        gtInts->handleInterrupt();  // Dispatch to GT interrupt handler for render completion
    }
    
    // Update handler time stats
    uint64_t handlerEnd = mach_absolute_time();
    uint64_t elapsed = handlerEnd - handlerStartTime;
    init_timebase();
    uint64_t elapsedUs = (elapsed * s_timebase.numer) / (s_timebase.denom * 1000);
    stats.handlerTime += elapsedUs;
}

/* Specific interrupt handlers */
void IntelDisplayInterrupts::handleVblank(uint32_t pipe) {
    // Update counter
    IOLockLock(interruptLock);
    vblankCounter[pipe]++;
    uint64_t now = mach_absolute_time();
    uint64_t lastTime = lastVblankTime[pipe];
    lastVblankTime[pipe] = now;
    IOLockUnlock(interruptLock);
    
    // Calculate latency
    uint64_t latency = 0;
    if (lastTime > 0) {
        uint64_t elapsed = now - lastTime;
        init_timebase();
        latency = (elapsed * s_timebase.numer) / (s_timebase.denom * 1000);  // microseconds
    }
    
    updateVblankStats(pipe, latency);
    
    // Invoke handlers
    invokeVblankHandlers(pipe);
    
    // Clear pending flag
    vblankPending[pipe] = false;
}

void IntelDisplayInterrupts::handleHotplug(uint32_t port) {
    // Detect connection state
    bool connected = detectPortConnection(port);
    
    updateHotplugStats(port, connected);
    
    // Invoke handlers
    invokeHotplugHandlers(port, connected);
    
    IOLog("Hotplug detected on port %d: %s\n", port, connected ? "connected" : "disconnected");
}

void IntelDisplayInterrupts::handleFlipComplete(uint32_t pipe) {
    IOLockLock(interruptLock);
    flipPending[pipe] = false;
    IOLockUnlock(interruptLock);
    
    stats.flipComplete[pipe]++;
}

void IntelDisplayInterrupts::handlePipeError(uint32_t pipe) {
    stats.pipeErrors[pipe]++;
    IOLog("Pipe %d error detected\n", pipe);
}

void IntelDisplayInterrupts::handleFifoUnderrun(uint32_t pipe) {
    stats.fifoUnderrun[pipe]++;
    IOLog("FIFO underrun on pipe %d\n", pipe);
}

/* Hardware register access */
void IntelDisplayInterrupts::writeInterruptMask(uint32_t mask) {
    for (int pipe = 0; pipe < 3; pipe++) {
        controller->writeRegister32(GEN11_DE_PIPE_IMR(pipe), mask);
    }
    controller->writeRegister32(GEN11_DE_PORT_IMR, mask);
    controller->writeRegister32(GEN11_DE_MISC_IMR, mask);
}

void IntelDisplayInterrupts::writeInterruptEnable(uint32_t enable) {
    for (int pipe = 0; pipe < 3; pipe++) {
        controller->writeRegister32(GEN11_DE_PIPE_IER(pipe), enable);
    }
    controller->writeRegister32(GEN11_DE_PORT_IER, enable);
    controller->writeRegister32(GEN11_DE_MISC_IER, enable);
}

uint32_t IntelDisplayInterrupts::readInterruptStatus() {
    uint32_t status = 0;
    for (int pipe = 0; pipe < 3; pipe++) {
        status |= controller->readRegister32(GEN11_DE_PIPE_ISR(pipe));
    }
    status |= controller->readRegister32(GEN11_DE_PORT_ISR);
    status |= controller->readRegister32(GEN11_DE_MISC_ISR);
    return status;
}

void IntelDisplayInterrupts::clearInterruptStatus(uint32_t status) {
    for (int pipe = 0; pipe < 3; pipe++) {
        controller->writeRegister32(GEN11_DE_PIPE_IIR(pipe), 0xFFFFFFFF);
    }
    controller->writeRegister32(GEN11_DE_PORT_IIR, 0xFFFFFFFF);
    controller->writeRegister32(GEN11_DE_MISC_IIR, 0xFFFFFFFF);
}

uint32_t IntelDisplayInterrupts::readVblankCounter(uint32_t pipe) {
    // Read hardware vblank counter register
    // Tiger Lake: PIPE_FRMCOUNT(pipe)
    uint32_t frmCount = controller->readRegister32(0x70040 + (pipe * 0x1000));
    return frmCount;
}

uint32_t IntelDisplayInterrupts::readHotplugStatus() {
    return controller->readRegister32(GEN11_DE_PORT_ISR);
}

void IntelDisplayInterrupts::clearHotplugStatus(uint32_t port) {
    uint32_t bit = (1 << (port + 3));
    controller->writeRegister32(GEN11_DE_PORT_IIR, bit);
}

/* Helpers */
void IntelDisplayInterrupts::invokeVblankHandlers(uint32_t pipe) {
    IOLockLock(vblankLock);
    
    VblankHandler* handler = vblankHandlers[pipe];
    while (handler) {
        if (handler->enabled && handler->callback) {
            handler->callback(handler->context, vblankCounter[pipe]);
        }
        handler = handler->next;
    }
    
    IOLockUnlock(vblankLock);
}

void IntelDisplayInterrupts::updateVblankStats(uint32_t pipe, uint64_t latency) {
    stats.vblankCount[pipe]++;
    
    // Update running average latency
    if (stats.vblankCount[pipe] > 1) {
        stats.vblankLatency[pipe] = (stats.vblankLatency[pipe] * 9 + latency) / 10;
    } else {
        stats.vblankLatency[pipe] = latency;
    }
}

void IntelDisplayInterrupts::invokeHotplugHandlers(uint32_t port, bool connected) {
    IOLockLock(hotplugLock);
    
    HotplugHandler* handler = hotplugHandlers[port];
    while (handler) {
        if (handler->enabled && handler->callback) {
            handler->callback(handler->context, port, connected);
        }
        handler = handler->next;
    }
    
    IOLockUnlock(hotplugLock);
}

void IntelDisplayInterrupts::updateHotplugStats(uint32_t port, bool connected) {
    stats.hotplugCount[port]++;
    if (connected) {
        stats.hotplugConnect[port]++;
    } else {
        stats.hotplugDisconnect[port]++;
    }
}

bool IntelDisplayInterrupts::detectPortConnection(uint32_t port) {
    // Read port DDI buffer status
    // Tiger Lake: DDI_BUF_CTL(port)
    uint32_t bufCtl = controller->readRegister32(0x64000 + (port * 0x100));
    return (bufCtl & (1 << 31)) != 0;  // Buffer enabled = connected
}

void IntelDisplayInterrupts::updateFlipStats(uint32_t pipe, bool timeout) {
    if (timeout) {
        stats.flipTimeout[pipe]++;
    } else {
        stats.flipComplete[pipe]++;
    }
}

/* Statistics */
void IntelDisplayInterrupts::getStats(DisplayInterruptStats* outStats) {
    if (!outStats) {
        return;
    }
    
    IOLockLock(interruptLock);
    memcpy(outStats, &stats, sizeof(DisplayInterruptStats));
    IOLockUnlock(interruptLock);
}

void IntelDisplayInterrupts::resetStats() {
    IOLockLock(interruptLock);
    memset(&stats, 0, sizeof(DisplayInterruptStats));
    IOLockUnlock(interruptLock);
    
    IOLog("Display interrupt statistics reset\n");
}

void IntelDisplayInterrupts::printStats() {
    IOLog("Total interrupts: %llu\n", stats.totalInterrupts);
    IOLog("Spurious: %llu\n", stats.spuriousInterrupts);
    IOLog("Handler time: %llu us\n", stats.handlerTime);
    
    IOLog("\nVblank:\n");
    for (int pipe = 0; pipe < 3; pipe++) {
        if (stats.vblankCount[pipe] > 0) {
            IOLog("  Pipe %d: %llu vblanks, %llu missed, %llu us avg latency\n",
                  pipe, stats.vblankCount[pipe], stats.vblankMissed[pipe],
                  stats.vblankLatency[pipe]);
        }
    }
    
    IOLog("\nHotplug:\n");
    for (int port = 0; port < 5; port++) {
        if (stats.hotplugCount[port] > 0) {
            IOLog("  Port %d: %llu events (%llu connect, %llu disconnect)\n",
                  port, stats.hotplugCount[port], stats.hotplugConnect[port],
                  stats.hotplugDisconnect[port]);
        }
    }
    
    IOLog("\nFlips:\n");
    for (int pipe = 0; pipe < 3; pipe++) {
        if (stats.flipComplete[pipe] > 0 || stats.flipTimeout[pipe] > 0) {
            IOLog("  Pipe %d: %llu complete, %llu timeout\n",
                  pipe, stats.flipComplete[pipe], stats.flipTimeout[pipe]);
        }
    }
    
    IOLog("\nErrors:\n");
    for (int pipe = 0; pipe < 3; pipe++) {
        if (stats.fifoUnderrun[pipe] > 0 || stats.pipeErrors[pipe] > 0) {
            IOLog("  Pipe %d: %llu underruns, %llu errors\n",
                  pipe, stats.fifoUnderrun[pipe], stats.pipeErrors[pipe]);
        }
    }
    
}
