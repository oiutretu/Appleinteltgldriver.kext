/*
 * IntelDisplayInterrupts.h
 * Intel Display Interrupt Handling
 *
 * Handles display-related interrupts:
 * - Vblank (vertical blank) interrupts for frame synchronization
 * - Hot-plug detection for monitor connection/disconnection
 * - Pipe events (underruns, CRC, flip complete)
 * - Error conditions (FIFO underrun, etc.)
 */

#ifndef IntelDisplayInterrupts_h
#define IntelDisplayInterrupts_h

#include <IOKit/IOService.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include "intel_display_types.h"

// Forward declarations
class AppleIntelTGLController;
class IntelDisplay;
class IntelPipe;
class IntelPort;

/* Interrupt types */
enum DisplayInterruptType {
    DISP_INT_VBLANK       = (1 << 0),  // Vertical blank start
    DISP_INT_HOTPLUG      = (1 << 1),  // Display connected/disconnected
    DISP_INT_FLIP_DONE    = (1 << 2),  // Page flip completed
    DISP_INT_PIPE_ERROR   = (1 << 3),  // Pipe error (underrun, etc.)
    DISP_INT_CRC_DONE     = (1 << 4),  // CRC calculation done
    DISP_INT_FIFO_UNDERRUN = (1 << 5), // Display FIFO underrun
    DISP_INT_SPRITE_ERROR = (1 << 6),  // Sprite plane error
    DISP_INT_ALL          = 0xFF
};

/* Vblank callback type */
typedef void (*VblankCallback)(void* context, uint32_t frame);

/* Hotplug callback type */
typedef void (*HotplugCallback)(void* context, uint32_t port, bool connected);

/* Vblank handler registration */
struct VblankHandler {
    VblankCallback callback;
    void* context;
    uint32_t pipe;
    bool enabled;
    VblankHandler* next;
};

/* Hotplug handler registration */
struct HotplugHandler {
    HotplugCallback callback;
    void* context;
    uint32_t port;
    bool enabled;
    HotplugHandler* next;
};

/* Interrupt statistics */
struct DisplayInterruptStats {
    // Vblank counters (per pipe)
    uint64_t vblankCount[3];        // Pipe A, B, C
    uint64_t vblankMissed[3];       // Missed vblanks
    uint64_t vblankLatency[3];      // Average latency (microseconds)
    
    // Hotplug counters (per port)
    uint64_t hotplugCount[5];       // Port A-E
    uint64_t hotplugConnect[5];     // Connection events
    uint64_t hotplugDisconnect[5];  // Disconnection events
    
    // Error counters
    uint64_t fifoUnderrun[3];       // Per-pipe underruns
    uint64_t pipeErrors[3];         // Other pipe errors
    uint64_t spriteErrors[3];       // Sprite plane errors
    
    // Flip counters
    uint64_t flipComplete[3];       // Successful flips
    uint64_t flipTimeout[3];        // Timed out flips
    
    // Overall
    uint64_t totalInterrupts;       // Total interrupt count
    uint64_t spuriousInterrupts;    // Spurious/unexpected interrupts
    uint64_t handlerTime;           // Total time in handler (microseconds)
};

class IntelDisplayInterrupts : public OSObject {
    OSDeclareDefaultStructors(IntelDisplayInterrupts)
    
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
    
    /* Vblank management */
    bool enableVblank(uint32_t pipe);
    void disableVblank(uint32_t pipe);
    bool registerVblankHandler(uint32_t pipe, VblankCallback callback, void* context);
    void unregisterVblankHandler(uint32_t pipe, VblankCallback callback);
    uint32_t getVblankCounter(uint32_t pipe);
    bool waitForVblank(uint32_t pipe, uint32_t timeout_ms = 100);
    
    /* Hotplug management */
    bool enableHotplug(uint32_t port);
    void disableHotplug(uint32_t port);
    bool registerHotplugHandler(uint32_t port, HotplugCallback callback, void* context);
    void unregisterHotplugHandler(uint32_t port, HotplugCallback callback);
    void triggerHotplugDetection(uint32_t port);
    
    /* Page flip management */
    bool waitForFlipComplete(uint32_t pipe, uint32_t timeout_ms = 100);
    void notifyFlipPending(uint32_t pipe);
    
    /* Error handling */
    void clearPipeErrors(uint32_t pipe);
    bool hasPipeError(uint32_t pipe);
    void enableErrorReporting(uint32_t pipe);
    void disableErrorReporting(uint32_t pipe);
    
    /* Statistics */
    void getStats(DisplayInterruptStats* stats);
    void resetStats();
    void printStats();
    
private:
    /* Hardware interrupt handling */
    static void interruptOccurred(OSObject* owner, IOInterruptEventSource* sender, int count);
    void handleInterrupt();
    
    /* Specific interrupt handlers */
    void handleVblank(uint32_t pipe);
    void handleHotplug(uint32_t port);
    void handleFlipComplete(uint32_t pipe);
    void handlePipeError(uint32_t pipe);
    void handleFifoUnderrun(uint32_t pipe);
    
    /* Hardware register access */
    void writeInterruptMask(uint32_t mask);
    void writeInterruptEnable(uint32_t enable);
    uint32_t readInterruptStatus();
    void clearInterruptStatus(uint32_t status);
    
    uint32_t readVblankCounter(uint32_t pipe);
    uint32_t readHotplugStatus();
    void clearHotplugStatus(uint32_t port);
    
    /* Vblank helpers */
    void invokeVblankHandlers(uint32_t pipe);
    void updateVblankStats(uint32_t pipe, uint64_t latency);
    
    /* Hotplug helpers */
    void invokeHotplugHandlers(uint32_t port, bool connected);
    void updateHotplugStats(uint32_t port, bool connected);
    bool detectPortConnection(uint32_t port);
    
    /* Flip helpers */
    void updateFlipStats(uint32_t pipe, bool timeout);
    
    /* Member variables */
    AppleIntelTGLController* controller;
    IntelDisplay* display;
    IOWorkLoop* workLoop;
    IOInterruptEventSource* interruptSource;
    
    /* Handler lists */
    VblankHandler* vblankHandlers[3];   // Per-pipe handler lists
    HotplugHandler* hotplugHandlers[5]; // Per-port handler lists
    
    /* Interrupt state */
    uint32_t enabledInterrupts;         // Bitmask of enabled types
    uint32_t vblankEnabled;             // Bitmask of enabled pipes (bit 0-2)
    uint32_t hotplugEnabled;            // Bitmask of enabled ports (bit 0-4)
    
    /* Vblank state */
    uint32_t vblankCounter[3];          // Software vblank counters
    uint64_t lastVblankTime[3];         // Timestamp of last vblank
    bool vblankPending[3];              // Vblank waiting flag
    
    /* Flip state */
    bool flipPending[3];                // Page flip pending flag
    uint64_t flipStartTime[3];          // Flip request timestamp
    
    /* Statistics */
    DisplayInterruptStats stats;
    uint64_t handlerStartTime;          // For latency measurement
    
    /* Synchronization */
    IOLock* interruptLock;              // Protects interrupt state
    IOLock* vblankLock;                 // Protects vblank handlers
    IOLock* hotplugLock;                // Protects hotplug handlers
    
    /* Status flags */
    bool isStarted;
    bool interruptsRegistered;
};

/* Helper macros for register offsets (Tiger Lake) */
#define GEN11_DISPLAY_INT_CTL           0x44200  // Master interrupt control
#define GEN11_DE_PIPE_ISR(pipe)         (0x44400 + (pipe) * 0x10)
#define GEN11_DE_PIPE_IMR(pipe)         (0x44404 + (pipe) * 0x10)
#define GEN11_DE_PIPE_IIR(pipe)         (0x44408 + (pipe) * 0x10)
#define GEN11_DE_PIPE_IER(pipe)         (0x4440C + (pipe) * 0x10)

#define GEN11_DE_PORT_ISR               0x44440
#define GEN11_DE_PORT_IMR               0x44444
#define GEN11_DE_PORT_IIR               0x44448
#define GEN11_DE_PORT_IER               0x4444C

#define GEN11_DE_MISC_ISR               0x44460
#define GEN11_DE_MISC_IMR               0x44464
#define GEN11_DE_MISC_IIR               0x44468
#define GEN11_DE_MISC_IER               0x4446C

/* Pipe interrupt bits */
#define GEN8_PIPE_VBLANK                (1 << 0)
#define GEN8_PIPE_FLIP_DONE             (1 << 4)
#define GEN8_PIPE_FIFO_UNDERRUN         (1 << 31)
#define GEN8_PIPE_CRC_DONE              (1 << 5)
#define GEN8_PIPE_ODD_FIELD             (1 << 6)
#define GEN8_PIPE_EVEN_FIELD            (1 << 7)

/* Port interrupt bits */
#define GEN8_PORT_DP_A_HOTPLUG          (1 << 3)
#define GEN8_PORT_DP_B_HOTPLUG          (1 << 4)
#define GEN8_PORT_DP_C_HOTPLUG          (1 << 5)
#define GEN8_PORT_DP_D_HOTPLUG          (1 << 6)

/* Misc interrupt bits */
#define GEN8_DE_MISC_GSE                (1 << 27)

#endif /* IntelDisplayInterrupts_h */
