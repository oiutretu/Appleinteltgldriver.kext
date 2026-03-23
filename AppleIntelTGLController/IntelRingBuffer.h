/*
 * IntelRingBuffer.h
 *
 * Ring buffer for GPU command submission
 * Ported from Linux intel_ring.c / intel_ring_submission.c
 */

#ifndef INTEL_RINGBUFFER_H
#define INTEL_RINGBUFFER_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "linux_compat.h"

class AppleIntelTGLController;
class IntelGEMObject;
class IntelGTT;
class IntelContext;
class IntelRequest;

/* Ring Buffer Sizes */
#define RING_SIZE_4K    (4 * 1024)
#define RING_SIZE_16K   (16 * 1024)
#define RING_SIZE_32K   (32 * 1024)

/* Ring IDs (Engine Types) */
enum intel_engine_id {
    RCS0 = 0,    // Render/Compute Engine 0
    BCS0,        // Blitter Engine 0
    VCS0,        // Video Decode Engine 0
    VCS1,        // Video Decode Engine 1
    VECS0,       // Video Enhancement Engine 0
    I915_NUM_ENGINES
};

/* Legacy names for compatibility */
#define RING_RCS    RCS0
#define RING_BCS    BCS0
#define RING_VCS0   VCS0
#define RING_VCS1   VCS1
#define RING_VECS   VECS0

/* Engine Classes */
enum intel_engine_class {
    RENDER_CLASS = 0,
    VIDEO_DECODE_CLASS,
    VIDEO_ENHANCE_CLASS,
    COPY_ENGINE_CLASS,
    OTHER_CLASS,
};

/* Ring Registers (Tiger Lake / Gen12) */
struct ring_registers {
    u32 tail;      // Ring tail (write pointer)
    u32 head;      // Ring head (read pointer)
    u32 start;     // Ring start address
    u32 ctl;       // Ring control
};

/* Ring Statistics */
struct ring_stats {
    u64 commands_submitted;
    u64 bytes_written;
    u64 waits;
    u64 wraps;
    u32 last_seqno;
};

class IntelRingBuffer {
public:
    IntelRingBuffer();
    virtual ~IntelRingBuffer();
    
    // Initialization
    bool init(AppleIntelTGLController *ctrl, enum intel_engine_id id, size_t size);
    void cleanup();
    
    // Command Submission
    bool begin(size_t numDwords);
    bool emit(u32 dword);
    bool emitDwords(const u32 *data, size_t count);
    bool advance();
    bool submitCommand(const u32 *commands, size_t numDwords, IntelRequest* request = nullptr);
    
    // Space Management
    size_t getAvailableSpace() const;
    bool waitForSpace(size_t numBytes, u32 timeoutMs);
    bool isSpaceAvailable(size_t numBytes) const;
    
    // Synchronization
    u32 getSeqno();
    bool sync();
    bool waitSeqno(u32 seqno, u32 timeoutMs);
    bool waitForIdle(uint32_t timeoutMs);
    void flush();
    void retireSeqno(u32 seqno);  // Called by interrupt handler
    
    // Control
    bool start();
    bool stop();
    bool reset();
    
    // Query
    u32 getHead() const;
    u32 getTail() const;
    bool isBusy() const;
    bool isIdle() const;
    
    // Statistics
    void getStats(struct ring_stats *stats);
    void printStats();
    
    // Access
    enum intel_engine_id getEngineId() const { return engineId; }
    IntelGEMObject* getRingObject() const { return ringObj; }
    u64 getRingAddress() const;
    size_t getSize() const { return ringSize; }
    
private:
    AppleIntelTGLController *controller;
    IntelGTT *gtt;
    
    // Ring Buffer
    IntelGEMObject *ringObj;
    void *ringVirtual;
    u64 ringGpuAddress;
    size_t ringSize;
    
    // Engine Info
    enum intel_engine_id engineId;
    enum intel_engine_class engineClass;
    const char *engineName;
    struct ring_registers regs;
    
    // Ring State
    u32 head;          // CPU-side head cache
    u32 tail;          // CPU-side tail
    u32 space;         // Available space cache
    bool active;
    bool gucManaged;   // True if GuC controls this ring
    
    // Sequence Numbers
    u32 currentSeqno;
    u32 lastRetiredSeqno;
    
    // Locks
    IOLock *ringLock;
    
    // Statistics
    struct ring_stats stats;
    IOLock *statsLock;
    
    // Private Methods
    bool allocateRing();
    bool setupRegisters();
    bool initializeHardware();
    
    void updateHead();
    void updateTail(u32 newTail);
    void updateSpace();
    
    u32 readRegister(u32 reg);
    void writeRegister(u32 reg, u32 value);
    
    size_t calculateSpace(u32 head, u32 tail) const;
    bool waitForIdleEngine(u32 timeoutMs);
    
    const char* getEngineName(enum intel_engine_id id);
    enum intel_engine_class getEngineClass(enum intel_engine_id id);
    void getRegisterOffsets(enum intel_engine_id id, struct ring_registers *regs);
};

/* Helper: Command MI (Memory Interface) Instructions */
namespace MI {
    static const u32 NOOP           = 0x00;
    static const u32 BATCH_BUFFER_END = 0x0A << 23;
    static const u32 FLUSH          = 0x04 << 23;
    static const u32 USER_INTERRUPT = 0x02 << 23;
    static const u32 STORE_DWORD    = 0x20 << 23;
    static const u32 LOAD_REGISTER  = 0x22 << 23;
    
    // Create MI_NOOP with padding
    static inline u32 noop(u32 count = 1) {
        return NOOP | ((count - 1) & 0x3FF);
    }
}

#endif // INTEL_RINGBUFFER_H
