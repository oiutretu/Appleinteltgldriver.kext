/*
 * AppleIntelTGLController.h
 *
 * Main driver controller for Intel graphics
 * Ported from Linux i915_driver.c
 */

#ifndef AppleIntelTGLController_h
#define AppleIntelTGLController_h

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include "linux_types.h"

// Forward declarations
class IntelPCIDevice;
class IntelDeviceInfo;
class IntelGTT;
class IntelGEM;
class IntelGEMObject;
class IntelUncore;
class IntelRingBuffer;
class IntelContext;
class IntelDisplay;
class IntelPowerManagement;
class IntelGTPowerManagement;
class IntelRuntimePM;  // NEW
class IntelDisplayInterrupts;
class IntelGTInterrupts;
class IntelGuC;
class IntelGuCSubmission;
class IntelRequestManager;
class IntelIOAccelerator;  // IOAccelerator service

class AppleIntelTGLController : public IOService {
    OSDeclareDefaultStructors(AppleIntelTGLController)
    
public:
    /* IOService overrides */
    virtual bool init(OSDictionary *dictionary = NULL) APPLE_KEXT_OVERRIDE;
virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
virtual void free(void) APPLE_KEXT_OVERRIDE;

    /* Power Management */
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal,
                                  IOService *whatDevice) APPLE_KEXT_OVERRIDE;

    /* Accessors */
    IOPCIDevice* getPCIDevice() const { return pciDevice; }
    IntelUncore* getUncore() const { return uncore; }
    void __iomem* getMMIOBase() const;  // Implemented in .cpp
    IntelDeviceInfo* getDeviceInfo() const { return deviceInfo; }
    IOWorkLoop* getWorkLoop() const override { return workLoop; }
    IntelGTT* getGTT() const { return gtt; }
    IntelGEM* getGEM() const { return gem; }
    IntelRingBuffer* getRenderRing() const { return renderRing; }
    IntelContext* getDefaultContext() const { return defaultContext; }
    IntelDisplay* getDisplay() const { return display; }
    IntelGTPowerManagement* getGTPower() const { return gtPower; }
    IntelRuntimePM* getRuntimePM() const { return runtimePM; }
    IntelDisplayInterrupts* getDisplayInterrupts() const { return displayInterrupts; }
    IntelGTInterrupts* getGTInterrupts() const { return gtInterrupts; }
    
    // GuC accessors
    IntelGuC* getGuC() const { return guc; }
    IntelGuCSubmission* getGuCSubmission() const { return gucSubmission; }
    IntelPowerManagement* getPowerManagement() const { return powerMgmt; }
    
    // IOAccelerator accessor
    IntelIOAccelerator* getAccelerator() const { return accelerator; }
    
    // Blitter accessor
    class IntelBlitter* getBlitter() const { return blitter; }
    
    // Framebuffer GGTT address
    uint64_t getFramebufferGGTT() const { return framebufferGGTT; }
    void setFramebufferGGTT(uint64_t addr) { framebufferGGTT = addr; }
    
    // Fence management
    class IntelFence* createFence();
    class IntelFence* findFence(uint32_t fenceId);
    void signalFence(uint32_t fenceId);
    void releaseFence(uint32_t fenceId);
    
    /* Register access helpers (delegate to uncore) - implemented in .cpp to avoid incomplete type */
    u32 readRegister32(u32 offset) const;
    void writeRegister32(u32 offset, u32 value);
    
    /* Ring buffer access */
    IntelRingBuffer* getRingBuffer(int ring_id) const;
    
    /* GEM object allocation */
    IntelGEMObject* allocateGEMObject(size_t size);
    
    /* Context management */
    void destroyContext(IntelContext* context);
    
    /* Request management */
    class IntelRequestManager* getRequestManager() const { return requestManager; }
    
    /* IOSurface Integration (Phase 1) */
    IOReturn mapSurfaceToGPU(IOMemoryDescriptor* mem, uint64_t* outGPUAddr);
    IOReturn unmapSurfaceFromGPU(uint64_t gpuAddress);
    IOReturn mapSurfaceToGGTT(IOMemoryDescriptor* mem, uint64_t* outGPUAddr);
    IOReturn unmapSurfaceFromGGTT(uint64_t gpuAddress);
    uint64_t allocateGPUVirtualAddress(uint64_t size);
    IOReturn bindGEMtoPPGTT(IntelGEMObject* gem, IntelContext* context);
    IOReturn unbindGEMfromPPGTT(IntelGEMObject* gem, IntelContext* context);

private:
    /* Hardware detection and initialization */
    bool initPowerManagement();  // Wake GPU (NEW - moved from framebuffer)
    bool detectHardware();
    bool initializeHardware();
    bool setupDeviceInfo();
    bool setupUncore();
    bool setupMemoryManagement();
    bool setupGTT();
    bool setupGEM();
    bool setupGuC();
    bool setupRenderRing();
    bool setupBlitter();
    bool setupDefaultContext();
    bool setupDisplay();  // NEW
    bool setupPowerManagement();  // NEW

    /* Cleanup */
    void cleanupHardware();
    void cleanupResources();

    /* Device components */
    IOPCIDevice         *pciDevice;         // PCI device
    IntelPCIDevice      *i915PciDev;        // Our PCI wrapper
    IntelDeviceInfo     *deviceInfo;        // Device information
    IntelUncore         *uncore;            // Register access
    IntelGTT            *gtt;
    IntelGEM            *gem;
    IntelRingBuffer     *renderRing;
    IntelContext        *defaultContext;
    IntelDisplay        *display;
    class IntelBlitter  *blitter;           // Blitter engine for 2D operations
    IntelGTPowerManagement *gtPower;  // NEW
    IntelRuntimePM      *runtimePM;  // NEW
    IntelDisplayInterrupts* displayInterrupts;
    IntelGTInterrupts* gtInterrupts;
    IntelGuC            *guc;  // GuC firmware interface
    IntelGuCSubmission  *gucSubmission;  // GuC submission
    IntelPowerManagement *powerMgmt;  // Power management
    IntelRequestManager  *requestManager;  // Request manager
    IntelIOAccelerator   *accelerator;  // IOAccelerator service for Metal/WindowServer

    /* Fence management */
    OSArray             *activeFences;      // Array of active fences
    IOLock              *fenceLock;         // Lock for fence operations
    uint32_t            nextFenceId;        // Next fence ID to allocate
    
    /* GEM object tracking (Phase 1: IOSurface) */
    OSArray             *gemObjects;        // Array of IntelGEMObject* (wrapped in OSNumber)

    /* Workloop and gates */
    IOWorkLoop          *workLoop;          // Driver work loop
    IOCommandGate       *commandGate;       // Command gate for serialization
    IOTimerEventSource  *watchdogTimer;     // Watchdog timer

    /* Memory regions */
    IOMemoryMap         *mmioMap;           // MMIO BAR mapping
    IOMemoryMap         *gttMap;            // GTT BAR mapping

    /* Driver state */
    bool                isInitialized;
    bool                isResuming;
    UInt16              deviceID;
    UInt16              vendorID;
    UInt8               revisionID;
    uint64_t            framebufferGGTT;  // GGTT address of the main framebuffer

    /* Statistics */
    UInt64              startTime;
    UInt32              resetCount;
};

#endif /* AppleIntelTGLController_h */
