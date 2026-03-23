/*
 * IntelUncore.h
 * 
 * Register access layer with forcewake support
 * Ported from Linux intel_uncore.c/h
 */

#ifndef IntelUncore_h
#define IntelUncore_h

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLocks.h>
#include "linux_compat.h"

// Forward declarations
class AppleIntelTGLController;

/* Forcewake domain IDs - matches Linux enum */
enum forcewake_domain_id {
    FW_DOMAIN_ID_RENDER = 0,
    FW_DOMAIN_ID_GT,            /* also includes blitter engine */
    FW_DOMAIN_ID_MEDIA,
    FW_DOMAIN_ID_MEDIA_VDBOX0,
    FW_DOMAIN_ID_MEDIA_VDBOX1,
    FW_DOMAIN_ID_MEDIA_VDBOX2,
    FW_DOMAIN_ID_MEDIA_VDBOX3,
    FW_DOMAIN_ID_MEDIA_VEBOX0,
    FW_DOMAIN_ID_MEDIA_VEBOX1,
    FW_DOMAIN_ID_GSC,
    
    FW_DOMAIN_ID_COUNT
};

/* Forcewake domain bitmasks */
enum forcewake_domains {
    FORCEWAKE_RENDER        = (1 << FW_DOMAIN_ID_RENDER),
    FORCEWAKE_GT            = (1 << FW_DOMAIN_ID_GT),
    FORCEWAKE_MEDIA         = (1 << FW_DOMAIN_ID_MEDIA),
    FORCEWAKE_MEDIA_VDBOX0  = (1 << FW_DOMAIN_ID_MEDIA_VDBOX0),
    FORCEWAKE_MEDIA_VDBOX1  = (1 << FW_DOMAIN_ID_MEDIA_VDBOX1),
    FORCEWAKE_MEDIA_VDBOX2  = (1 << FW_DOMAIN_ID_MEDIA_VDBOX2),
    FORCEWAKE_MEDIA_VDBOX3  = (1 << FW_DOMAIN_ID_MEDIA_VDBOX3),
    FORCEWAKE_MEDIA_VEBOX0  = (1 << FW_DOMAIN_ID_MEDIA_VEBOX0),
    FORCEWAKE_MEDIA_VEBOX1  = (1 << FW_DOMAIN_ID_MEDIA_VEBOX1),
    FORCEWAKE_GSC           = (1 << FW_DOMAIN_ID_GSC),
    
    FORCEWAKE_ALL = ((1 << FW_DOMAIN_ID_COUNT) - 1),
};

/* Forcewake domain structure */
struct forcewake_domain {
    u32 mask;                   /* Domain bitmask */
    u32 reg_set;                /* Forcewake set register offset */
    u32 reg_ack;                /* Forcewake acknowledge register offset */
    u32 val_set;                /* Value to write for set */
    u32 val_clear;              /* Value to write for clear */
    u32 val_reset;              /* Value indicating reset */
    bool active;                /* Domain is currently active */
};

class IntelUncore {
public:
    /* Initialization */
    bool init(AppleIntelTGLController *controller, void *mmioBase, size_t mmioSize);
    void cleanup();
    
    /* Register read operations */
    u8   readRegister8(u32 offset);
    u16  readRegister16(u32 offset);
    u32  readRegister32(u32 offset);
    u64  readRegister64(u32 offset);
    
    /* Register write operations */
    void writeRegister8(u32 offset, u8 value);
    void writeRegister16(u32 offset, u16 value);
    void writeRegister32(u32 offset, u32 value);
    void writeRegister64(u32 offset, u64 value);
    
    /* Forcewake operations */
    void forcewakeGet(enum forcewake_domains domains);
    void forcewakePut(enum forcewake_domains domains);
    bool forcewakeWaitAck(enum forcewake_domain_id domain_id, u32 timeout_ms);
    
    /* Posting read (ensure write completes) */
    void postingRead32(u32 offset);
    
    /* Register access with automatic forcewake */
    u32  readRegister32_fw(u32 offset);
    void writeRegister32_fw(u32 offset, u32 value);
    
    /* Accessors */
    void* getMMIOBase() const { return mmioBase; }
    bool isInitialized() const { return initialized; }
    
private:
    /* Initialization helpers */
    bool setupForcewakeDomains();
    void detectForcewakeConfig();
    
    /* Low-level MMIO access */
    inline u8   rawRead8(u32 offset);
    inline u16  rawRead16(u32 offset);
    inline u32  rawRead32(u32 offset);
    inline u64  rawRead64(u32 offset);
    inline void rawWrite8(u32 offset, u8 value);
    inline void rawWrite16(u32 offset, u16 value);
    inline void rawWrite32(u32 offset, u32 value);
    inline void rawWrite64(u32 offset, u64 value);
    
    /* Forcewake helpers */
    enum forcewake_domains getForcewakeDomains(u32 offset, bool is_write);
    void forcewakeSetDomain(enum forcewake_domain_id domain_id);
    void forcewakeClearDomain(enum forcewake_domain_id domain_id);
    bool checkForcewakeAck(enum forcewake_domain_id domain_id);
    
    /* Debug and error checking */
    bool checkForUnclaimedMMIO(u32 offset);
    void reportUnclaimedMMIO(u32 offset, bool is_write);
    
    /* Hardware detection */
    bool isTigerLake() const;
    bool isGen12() const;
    
    /* Member variables */
    AppleIntelTGLController     *controller;        // Parent controller
    void                    *mmioBase;          // MMIO virtual base address
    size_t                  mmioSize;           // MMIO region size
    
    /* Forcewake domains */
    struct forcewake_domain fw_domains[FW_DOMAIN_ID_COUNT];
    u32                     fw_domains_active;  // Bitmask of active domains
    
    /* Locks */
    IOLock                  *mmioLock;          // Serialize MMIO access
    IOLock                  *forcewakeLock;     // Serialize forcewake operations
    
    /* Flags */
    bool                    initialized;
    bool                    unclaimed_mmio_check;   // Check for unclaimed MMIO
    
    /* Statistics */
    UInt64                  read_count;
    UInt64                  write_count;
    UInt64                  forcewake_count;
    UInt64                  unclaimed_mmio_count;
};

/* Register offsets - Tiger Lake (Gen12) */
namespace TGL_REGS {
    /* Forcewake registers */
    constexpr u32 FORCEWAKE_RENDER_GEN9         = 0xa278;
    constexpr u32 FORCEWAKE_ACK_RENDER_GEN9     = 0xd84;
    constexpr u32 FORCEWAKE_GT_GEN9             = 0xa188;
    constexpr u32 FORCEWAKE_ACK_GT_GEN9         = 0xd50;
    constexpr u32 FORCEWAKE_MEDIA_GEN9          = 0xa270;
    constexpr u32 FORCEWAKE_ACK_MEDIA_GEN9      = 0xd88;
    
    /* Forcewake values */
    constexpr u32 FORCEWAKE_KERNEL              = 0x1;
    constexpr u32 FORCEWAKE_USER                = 0x2;
    constexpr u32 FORCEWAKE_KERNEL_FALLBACK     = 0x2;
    constexpr u32 FORCEWAKE_MT_ACK              = 0x10000;
    
    /* Other important registers */
    constexpr u32 GEN6_GT_THREAD_STATUS         = 0x13805c;
    constexpr u32 ECOBUS                        = 0xa180;
    constexpr u32 GEN11_EU_DISABLE              = 0x9134;
    
    /* Timeouts */
    constexpr u32 FORCEWAKE_ACK_TIMEOUT_MS      = 50;
    constexpr u32 GT_FIFO_TIMEOUT_MS            = 10;
};

#endif /* IntelUncore_h */
