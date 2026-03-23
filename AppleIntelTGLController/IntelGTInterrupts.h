/*
 * IntelGTInterrupts.h
 * Intel Graphics Technology (GT) Interrupt Handling
 *
 * Handles GPU/GT-related interrupts:
 * - Render complete (command buffer completion)
 * - User interrupts (seqno updates)
 * - GPU hang detection
 * - Page faults and errors
 * - Context switches
 * - Watchdog timeouts
 */

#ifndef IntelGTInterrupts_h
#define IntelGTInterrupts_h

#include <IOKit/IOService.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>

// Forward declarations
class AppleIntelTGLController;
class IntelRingBuffer;
class IntelContext;

/* GT interrupt types */
enum GTInterruptType {
    GT_INT_RENDER_COMPLETE  = (1 << 0),  // Render engine complete
    GT_INT_USER             = (1 << 1),  // User interrupt (seqno)
    GT_INT_CONTEXT_SWITCH   = (1 << 2),  // Context switch complete
    GT_INT_PAGE_FAULT       = (1 << 3),  // GPU page fault
    GT_INT_GPU_HANG         = (1 << 4),  // GPU hang detected
    GT_INT_ERROR            = (1 << 5),  // GPU error condition
    GT_INT_WATCHDOG         = (1 << 6),  // Watchdog timeout
    GT_INT_TLB_INVALIDATE   = (1 << 7),  // TLB invalidate complete
    GT_INT_ALL              = 0xFF
};

/* Render engine types */
enum GTEngine {
    GT_ENGINE_RCS   = 0,  // Render/Copy/Shader engine
    GT_ENGINE_BCS   = 1,  // Blitter engine
    GT_ENGINE_VCS0  = 2,  // Video Codec engine 0
    GT_ENGINE_VCS1  = 3,  // Video Codec engine 1
    GT_ENGINE_VECS  = 4,  // Video Enhancement engine
    GT_ENGINE_COUNT = 5
};

/* Command completion callback */
typedef void (*RenderCompleteCallback)(void* context, uint32_t engine, uint32_t seqno);

/* User interrupt callback */
typedef void (*UserInterruptCallback)(void* context, uint32_t engine);

/* GPU hang callback */
typedef void (*GPUHangCallback)(void* context, uint32_t engine, uint32_t acthd);

/* Page fault callback */
typedef void (*PageFaultCallback)(void* context, uint64_t faultAddr, uint32_t faultType);

/* Render complete handler */
struct RenderCompleteHandler {
    RenderCompleteCallback callback;
    void* context;
    uint32_t engine;
    uint32_t waitSeqno;         // Seqno to wait for
    bool enabled;
    RenderCompleteHandler* next;
};

/* User interrupt handler */
struct UserInterruptHandler {
    UserInterruptCallback callback;
    void* context;
    uint32_t engine;
    bool enabled;
    UserInterruptHandler* next;
};

/* GPU hang handler */
struct GPUHangHandler {
    GPUHangCallback callback;
    void* context;
    uint32_t engine;
    bool enabled;
    GPUHangHandler* next;
};

/* Page fault handler */
struct PageFaultHandler {
    PageFaultCallback callback;
    void* context;
    bool enabled;
    PageFaultHandler* next;
};

/* GT interrupt statistics */
struct GTInterruptStats {
    // Per-engine render complete
    uint64_t renderComplete[GT_ENGINE_COUNT];
    uint64_t renderLatency[GT_ENGINE_COUNT];  // Average uss
    
    // User interrupts
    uint64_t userInterrupt[GT_ENGINE_COUNT];
    
    // Context switches
    uint64_t contextSwitch[GT_ENGINE_COUNT];
    uint64_t contextSwitchLatency[GT_ENGINE_COUNT];  // Average uss
    
    // Errors
    uint64_t pageFault;
    uint64_t gpuHang[GT_ENGINE_COUNT];
    uint64_t errorInterrupt[GT_ENGINE_COUNT];
    uint64_t watchdogTimeout[GT_ENGINE_COUNT];
    
    // TLB
    uint64_t tlbInvalidate;
    
    // Overall
    uint64_t totalInterrupts;
    uint64_t spuriousInterrupts;
    uint64_t handlerTime;  // Total uss in handler
    
    // Hang detection
    uint32_t lastSeqno[GT_ENGINE_COUNT];
    uint64_t lastActivityTime[GT_ENGINE_COUNT];
    uint32_t hangCount;
};

/* GPU hang state */
struct GPUHangState {
    bool isHung;
    uint32_t engine;
    uint32_t acthd;           // Active head pointer
    uint32_t lastSeqno;
    uint32_t requestedSeqno;
    uint64_t hangTime;
    uint32_t hangCount;
};

class IntelGTInterrupts : public OSObject {
    OSDeclareDefaultStructors(IntelGTInterrupts)
    
public:
    /* Lifecycle */
    virtual bool init() override;
    virtual void free() override;
    
    bool initWithController(AppleIntelTGLController* controller);
    bool start();
    void stop();
    
    /* Interrupt registration */
    bool registerInterruptHandler();
    void unregisterInterruptHandler();
    
    /* Enable/disable interrupts */
    bool enableInterrupts(uint32_t types);
    void disableInterrupts(uint32_t types);
    bool isInterruptEnabled(uint32_t type);
    
    /* Per-engine control */
    bool enableEngineInterrupts(uint32_t engine);
    void disableEngineInterrupts(uint32_t engine);
    
    /* Render complete management */
    bool registerRenderCompleteHandler(uint32_t engine, uint32_t seqno,
                                      RenderCompleteCallback callback, void* context);
    void unregisterRenderCompleteHandler(uint32_t engine, RenderCompleteCallback callback);
    bool waitForRenderComplete(uint32_t engine, uint32_t seqno, uint32_t timeout_ms = 1000);
    uint32_t getCurrentSeqno(uint32_t engine);
    
    /* User interrupt management */
    bool registerUserInterruptHandler(uint32_t engine, UserInterruptCallback callback, void* context);
    void unregisterUserInterruptHandler(uint32_t engine, UserInterruptCallback callback);
    void triggerUserInterrupt(uint32_t engine);
    
    /* GPU hang detection */
    bool registerGPUHangHandler(uint32_t engine, GPUHangCallback callback, void* context);
    void unregisterGPUHangHandler(uint32_t engine, GPUHangCallback callback);
    bool isGPUHung(uint32_t engine);
    void clearGPUHang(uint32_t engine);
    void getHangState(uint32_t engine, GPUHangState* state);
    
    /* Page fault management */
    bool registerPageFaultHandler(PageFaultCallback callback, void* context);
    void unregisterPageFaultHandler(PageFaultCallback callback);
    
    /* Watchdog */
    void startWatchdog(uint32_t interval_ms = 1000);
    void stopWatchdog();
    void resetWatchdog();
    
    /* Statistics */
    void getStats(GTInterruptStats* stats);
    void resetStats();
    void printStats();
    
    /* Hardware interrupt handling - PUBLIC for display dispatch */
    void handleInterrupt();
    
private:
    static void interruptOccurred(OSObject* owner, IOInterruptEventSource* sender, int count);
    
    /* Specific interrupt handlers */
    void handleRenderComplete(uint32_t engine);
    void handleUserInterrupt(uint32_t engine);
    void handleContextSwitch(uint32_t engine);
    void handlePageFault(uint64_t faultAddr, uint32_t faultType);
    void handleGPUError(uint32_t engine);
    void handleWatchdogTimeout(uint32_t engine);
    void handleTLBInvalidate();
    
    /* Watchdog timer */
    static void watchdogTimerFired(OSObject* owner, IOTimerEventSource* sender);
    void checkForHangs();
    void detectGPUHang(uint32_t engine);
    
    /* Hardware register access */
    void writeGTInterruptMask(uint32_t mask);
    void writeGTInterruptEnable(uint32_t enable);
    uint32_t readGTInterruptStatus();
    void clearGTInterruptStatus(uint32_t status);
    
    uint32_t readEngineSeqno(uint32_t engine);
    uint32_t readEngineACTHD(uint32_t engine);
    uint32_t readEngineStatus(uint32_t engine);
    void writeEngineInterruptMask(uint32_t engine, uint32_t mask);
    void writeEngineInterruptEnable(uint32_t engine, uint32_t enable);
    
    uint64_t readPageFaultAddress();
    uint32_t readPageFaultType();
    void clearPageFault();
    
    /* Seqno tracking */
    void updateSeqno(uint32_t engine, uint32_t seqno);
    bool seqnoComplete(uint32_t engine, uint32_t seqno);
    
    /* Helpers */
    void invokeRenderCompleteHandlers(uint32_t engine, uint32_t seqno);
    void invokeUserInterruptHandlers(uint32_t engine);
    void invokeGPUHangHandlers(uint32_t engine, uint32_t acthd);
    void invokePageFaultHandlers(uint64_t faultAddr, uint32_t faultType);
    
    void updateRenderCompleteStats(uint32_t engine, uint64_t latency);
    void updateContextSwitchStats(uint32_t engine, uint64_t latency);
    void updateHangStats(uint32_t engine);
    
    const char* getEngineName(uint32_t engine);
    
    /* Member variables */
    AppleIntelTGLController* controller;
    IOWorkLoop* workLoop;
    IOInterruptEventSource* interruptSource;
    IOTimerEventSource* watchdogTimer;
    
    /* Handler lists */
    RenderCompleteHandler* renderCompleteHandlers[GT_ENGINE_COUNT];
    UserInterruptHandler* userInterruptHandlers[GT_ENGINE_COUNT];
    GPUHangHandler* gpuHangHandlers[GT_ENGINE_COUNT];
    PageFaultHandler* pageFaultHandlers;
    
    /* Interrupt state */
    uint32_t enabledInterrupts;     // Bitmask of enabled types
    uint32_t engineEnabled;         // Bitmask of enabled engines
    
    /* Seqno tracking */
    uint32_t currentSeqno[GT_ENGINE_COUNT];
    uint32_t lastSeqno[GT_ENGINE_COUNT];
    uint64_t lastSeqnoTime[GT_ENGINE_COUNT];
    bool seqnoPending[GT_ENGINE_COUNT];
    
    /* Hang detection */
    GPUHangState hangState[GT_ENGINE_COUNT];
    uint64_t lastActivityTime[GT_ENGINE_COUNT];
    uint32_t watchdogInterval;  // milliseconds
    bool watchdogRunning;
    
    /* Statistics */
    GTInterruptStats stats;
    uint64_t handlerStartTime;
    
    /* Synchronization */
    IOLock* interruptLock;
    IOLock* renderLock;
    IOLock* userLock;
    IOLock* hangLock;
    IOLock* faultLock;
    
    /* Status flags */
    bool isStarted;
    bool interruptsRegistered;
};

/* Helper macros for GT register offsets (Tiger Lake) */
#define GEN11_GT_INT_CTL                0x190000  // Master GT interrupt control

// Per-engine interrupt registers
#define GEN11_GT_ENGINE_IMR(e)          (0x190010 + (e) * 0x100)
#define GEN11_GT_ENGINE_IER(e)          (0x190014 + (e) * 0x100)
#define GEN11_GT_ENGINE_IIR(e)          (0x190018 + (e) * 0x100)
#define GEN11_GT_ENGINE_ISR(e)          (0x19001C + (e) * 0x100)

// Engine status registers
#define GEN11_GT_ENGINE_RING_HEAD(e)    (0x002040 + (e) * 0x1000)
#define GEN11_GT_ENGINE_RING_TAIL(e)    (0x002030 + (e) * 0x1000)
#define GEN11_GT_ENGINE_RING_CTL(e)     (0x002038 + (e) * 0x1000)
#define GEN11_GT_ENGINE_ACTHD(e)        (0x002074 + (e) * 0x1000)
#define GEN11_GT_ENGINE_SEQNO(e)        (0x002000 + (e) * 0x1000)

// Fault registers
#define GEN11_GT_FAULT_TLB_DATA0        0x004B10
#define GEN11_GT_FAULT_TLB_DATA1        0x004B14

/* GT interrupt bits */
#define GT_RENDER_COMPLETE_INT          (1 << 0)
#define GT_USER_INTERRUPT               (1 << 1)
#define GT_CONTEXT_SWITCH_INT           (1 << 2)
#define GT_PAGE_FAULT_INT               (1 << 3)
#define GT_ERROR_INT                    (1 << 4)
#define GT_WATCHDOG_INT                 (1 << 5)

/* Hang detection constants */
#define GPU_HANG_THRESHOLD_MS           5000  // 5 seconds
#define WATCHDOG_CHECK_INTERVAL_MS      1000  // 1 second

#endif /* IntelGTInterrupts_h */
