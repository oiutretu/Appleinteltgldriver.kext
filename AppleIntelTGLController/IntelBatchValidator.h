//
//  IntelBatchValidator.h
//  Graphics Driver
//
//  Week 22: Batch Buffer Management - Command Validation & Security
//  Validates batch buffers for security, correctness, and hardware compatibility
//

#ifndef IntelBatchValidator_h
#define IntelBatchValidator_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include "linux_compat.h"

class AppleIntelTGLController;
class IntelGEMObject;
class IntelContext;
class IntelBatchBuffer;

// Validation error codes
enum ValidationError {
    VALIDATION_OK = 0,
    VALIDATION_ERROR_NULL_BUFFER = 1,
    VALIDATION_ERROR_INVALID_SIZE = 2,
    VALIDATION_ERROR_INVALID_ALIGNMENT = 3,
    VALIDATION_ERROR_PRIVILEGED_COMMAND = 4,
    VALIDATION_ERROR_INVALID_REGISTER = 5,
    VALIDATION_ERROR_INVALID_ADDRESS = 6,
    VALIDATION_ERROR_BUFFER_OVERFLOW = 7,
    VALIDATION_ERROR_INVALID_ENGINE = 8,
    VALIDATION_ERROR_RECURSION_DEPTH = 9,
    VALIDATION_ERROR_INVALID_COMMAND = 10,
    VALIDATION_ERROR_UNSAFE_OPERATION = 11
};

// Command types
enum CommandType {
    CMD_TYPE_UNKNOWN = 0,
    CMD_TYPE_NOOP,
    CMD_TYPE_BATCH_BUFFER_START,
    CMD_TYPE_BATCH_BUFFER_END,
    CMD_TYPE_MI_LOAD_REGISTER_IMM,
    CMD_TYPE_MI_STORE_REGISTER_MEM,
    CMD_TYPE_MI_LOAD_REGISTER_MEM,
    CMD_TYPE_PIPE_CONTROL,
    CMD_TYPE_XY_BLT,
    CMD_TYPE_3D_PRIMITIVE,
    CMD_TYPE_STATE_BASE_ADDRESS,
    CMD_TYPE_MEDIA
};

// Command flags
#define CMD_FLAG_PRIVILEGED     (1 << 0)
#define CMD_FLAG_RELOCATABLE    (1 << 1)
#define CMD_FLAG_CHAINED        (1 << 2)
#define CMD_FLAG_VALIDATED      (1 << 3)
#define CMD_FLAG_UNSAFE         (1 << 4)

// Validation statistics
struct ValidationStats {
    uint64_t batchesValidated;
    uint64_t batchesPassed;
    uint64_t batchesFailed;
    uint64_t commandsValidated;
    uint64_t commandsRelocated;
    uint64_t privilegedBlocked;
    uint64_t unsafeBlocked;
    uint64_t totalValidationTimeUs;
};

// Command descriptor
struct CommandDescriptor {
    uint32_t opcode;
    CommandType type;
    uint32_t length;                    // In DWords
    uint32_t flags;
    const char* name;
};

// Relocation entry
struct RelocationEntry {
    uint32_t offset;                    // Offset in batch buffer
    uint64_t targetOffset;              // Offset in target object
    IntelGEMObject* target;             // Target object
    uint32_t readDomains;
    uint32_t writeDomain;
    RelocationEntry* next;
};

// Validation context
struct ValidationContext {
    IntelBatchBuffer* batch;
    IntelContext* context;
    uint32_t engineMask;
    uint32_t recursionDepth;
    uint32_t maxRecursion;
    bool privilegedAllowed;
    bool allowChaining;
    RelocationEntry* relocations;
    uint32_t relocationCount;
};

//
// IntelBatchValidator - Batch buffer validation and security
//
class IntelBatchValidator : public OSObject {
    OSDeclareDefaultStructors(IntelBatchValidator)
    
public:
    // Initialization
    bool init() override;
    void free() override;
    
    bool initWithController(AppleIntelTGLController* controller);
    bool start();
    void stop();
    
    // Validation
    ValidationError validateBatch(IntelBatchBuffer* batch, 
                                  IntelContext* context,
                                  uint32_t engineMask);
    
    ValidationError validateBatchWithRelocations(IntelBatchBuffer* batch,
                                                  IntelContext* context,
                                                  uint32_t engineMask,
                                                  RelocationEntry* relocations,
                                                  uint32_t relocationCount);
    
    // Command validation
    ValidationError validateCommand(uint32_t* command,
                                    uint32_t offset,
                                    ValidationContext* ctx);
    
    bool isCommandAllowed(uint32_t opcode, bool privileged);
    bool isRegisterAllowed(uint32_t reg, bool write);
    bool isAddressAllowed(uint64_t address, IntelContext* context);
    
    // Command parsing
    CommandDescriptor* parseCommand(uint32_t* command);
    uint32_t getCommandLength(uint32_t* command);
    CommandType getCommandType(uint32_t opcode);
    
    // Relocation
    ValidationError applyRelocations(IntelBatchBuffer* batch,
                                     RelocationEntry* relocations,
                                     uint32_t count);
    
    bool relocateAddress(uint32_t* address, RelocationEntry* relocation);
    
    // Chaining validation
    ValidationError validateChain(IntelBatchBuffer* batch,
                                  ValidationContext* ctx);
    
    bool isChainCommandAllowed(uint32_t* command);
    uint64_t getChainTarget(uint32_t* command);
    
    // Security checks
    bool checkPrivileges(uint32_t opcode, IntelContext* context);
    bool checkAddressSafety(uint64_t address, uint32_t length);
    bool checkRecursionDepth(ValidationContext* ctx);
    
    // Register validation
    bool isWhitelistedRegister(uint32_t reg);
    bool isBlacklistedRegister(uint32_t reg);
    bool isReadOnlyRegister(uint32_t reg);
    
    // Statistics
    void getStats(ValidationStats* stats);
    void resetStats();
    void printStats();
    
    // Configuration
    void setMaxRecursionDepth(uint32_t depth) { maxRecursionDepth = depth; }
    void setPrivilegedAllowed(bool allowed) { privilegedAllowed = allowed; }
    void setChainAllowed(bool allowed) { chainAllowed = allowed; }
    
    uint32_t getMaxRecursionDepth() const { return maxRecursionDepth; }
    bool isPrivilegedAllowed() const { return privilegedAllowed; }
    bool isChainAllowed() const { return chainAllowed; }
    
private:
    AppleIntelTGLController* controller;
    bool started;
    
    // Configuration
    uint32_t maxRecursionDepth;
    bool privilegedAllowed;
    bool chainAllowed;
    
    // Command tables
    CommandDescriptor* commandTable;
    uint32_t commandTableSize;
    
    // Register lists
    uint32_t* whitelistedRegisters;
    uint32_t whitelistSize;
    uint32_t* blacklistedRegisters;
    uint32_t blacklistSize;
    uint32_t* readOnlyRegisters;
    uint32_t readOnlySize;
    
    // Statistics
    ValidationStats stats;
    
    // Lock
    IORecursiveLock* validatorLock;
    
    // Internal methods
    bool buildCommandTable();
    bool buildRegisterLists();
    void freeCommandTable();
    void freeRegisterLists();
    
    ValidationError validateCommandInternal(uint32_t* command,
                                            uint32_t offset,
                                            ValidationContext* ctx);
    
    bool isInRegisterList(uint32_t reg, uint32_t* list, uint32_t size);
    
    // Command-specific validators
    ValidationError validateLoadRegisterImm(uint32_t* command, ValidationContext* ctx);
    ValidationError validateStoreRegisterMem(uint32_t* command, ValidationContext* ctx);
    ValidationError validateLoadRegisterMem(uint32_t* command, ValidationContext* ctx);
    ValidationError validatePipeControl(uint32_t* command, ValidationContext* ctx);
    ValidationError validateBatchStart(uint32_t* command, ValidationContext* ctx);
    ValidationError validateStateBaseAddress(uint32_t* command, ValidationContext* ctx);
};

//
// Command opcodes for Tiger Lake (Gen 12)
//

// MI (Memory Interface) Commands
#define MI_NOOP                         0x00000000
#define MI_USER_INTERRUPT               0x02000000
#define MI_WAIT_FOR_EVENT               0x03000000
#define MI_FLUSH                        0x04000000
#define MI_ARB_CHECK                    0x05000000
#define MI_REPORT_HEAD                  0x07000000
#define MI_ARB_ON_OFF                   0x08000000
#define MI_BATCH_BUFFER_END             0x0A000000
#define MI_SUSPEND_FLUSH                0x0B000000
#define MI_SET_APPID                    0x0E000000
#define MI_OVERLAY_FLIP                 0x11000000
#define MI_LOAD_SCAN_LINES_INCL         0x12000000
#define MI_DISPLAY_FLIP                 0x14000000
#define MI_SEMAPHORE_MBOX               0x16000000
#define MI_SET_CONTEXT                  0x18000000
#define MI_STORE_DATA_IMM               0x20000000
#define MI_STORE_DATA_INDEX             0x21000000
#define MI_LOAD_REGISTER_IMM            0x22000000
#define MI_UPDATE_GTT                   0x23000000
#define MI_STORE_REGISTER_MEM           0x24000000
#define MI_FLUSH_DW                     0x26000000
#define MI_LOAD_REGISTER_MEM            0x29000000
#define MI_LOAD_REGISTER_REG            0x2A000000
#define MI_BATCH_BUFFER_START           0x31000000
#define MI_PREDICATE                    0x0C000000
#define MI_TOPOLOGY_FILTER              0x0D000000
#define MI_LOAD_SCAN_LINES_EXCL         0x13000000
#define MI_ATOMIC                       0x2F000000
#define MI_MATH                         0x1A000000

// 3D Pipeline Commands
#define GFX_OP_3DSTATE_PIPELINED        0x78000000
#define GFX_OP_3DSTATE_NONPIPELINED     0x79000000
#define GFX_OP_PIPE_CONTROL             0x7A000000
#define GFX_OP_3DPRIMITIVE              0x7B000000

// BLT Commands
#define XY_SETUP_BLT_CMD                0x50000000
#define XY_SETUP_MONO_PATTERN_SL_BLT_CMD 0x51000000
#define XY_SETUP_CLIP_BLT_CMD           0x52000000
#define XY_SRC_COPY_BLT_CMD             0x53000000
#define XY_MONO_SRC_COPY_BLT_CMD        0x54000000
#define XY_FULL_BLT_CMD                 0x55000000
#define XY_FULL_MONO_SRC_BLT_CMD        0x56000000
#define XY_FULL_MONO_PATTERN_BLT_CMD    0x57000000
#define XY_FULL_MONO_PATTERN_MONO_SRC_BLT_CMD 0x58000000
#define XY_MONO_PAT_BLT_CMD             0x59000000
#define XY_PAT_BLT_CMD                  0x5A000000
#define XY_COLOR_BLT_CMD                0x5B000000
#define XY_FAST_COPY_BLT_CMD            0x5C000000

// Register ranges (for validation)
#define MMIO_BASE                       0x00000000
#define MMIO_SIZE                       0x00200000  // 2MB MMIO space

// Privileged register ranges
#define PRIV_REG_FORCEWAKE_START        0x0A000
#define PRIV_REG_FORCEWAKE_END          0x0AFFF
#define PRIV_REG_GT_START               0x02000
#define PRIV_REG_GT_END                 0x09FFF
#define PRIV_REG_DISPLAY_START          0x60000
#define PRIV_REG_DISPLAY_END            0x6FFFF

// Safe register ranges (rendering)
#define SAFE_REG_3D_START               0x02000
#define SAFE_REG_3D_END                 0x027FF
#define SAFE_REG_VERTEX_START           0x03000
#define SAFE_REG_VERTEX_END             0x037FF

// Maximum values
#define MAX_BATCH_SIZE                  (4 * 1024 * 1024)  // 4MB
#define MAX_RECURSION_DEPTH             8
#define MAX_RELOCATIONS                 4096

#endif /* IntelBatchValidator_h */
