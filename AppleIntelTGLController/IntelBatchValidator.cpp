//
//  IntelBatchValidator.cpp
// macOS Driver
//
//  Week 22: Batch Buffer Management - Command Validation
//

#include "IntelBatchValidator.h"
#include "AppleIntelTGLController.h"
#include "IntelBatchBuffer.h"
#include "IntelGEMObject.h"
#include "IntelContext.h"
#include <IOKit/IOLib.h>

#define super OSObject

OSDefineMetaClassAndStructors(IntelBatchValidator, OSObject)

bool IntelBatchValidator::init() {
    if (!super::init()) {
        return false;
    }
    
    controller = nullptr;
    started = false;
    
    maxRecursionDepth = MAX_RECURSION_DEPTH;
    privilegedAllowed = false;
    chainAllowed = true;
    
    commandTable = nullptr;
    commandTableSize = 0;
    
    whitelistedRegisters = nullptr;
    whitelistSize = 0;
    blacklistedRegisters = nullptr;
    blacklistSize = 0;
    readOnlyRegisters = nullptr;
    readOnlySize = 0;
    
    bzero(&stats, sizeof(stats));
    
    validatorLock = IORecursiveLockAlloc();
    if (!validatorLock) {
        return false;
    }
    
    return true;
}

void IntelBatchValidator::free() {
    stop();
    
    freeCommandTable();
    freeRegisterLists();
    
    if (validatorLock) {
        IORecursiveLockFree(validatorLock);
        validatorLock = nullptr;
    }
    
    super::free();
}

bool IntelBatchValidator::initWithController(AppleIntelTGLController* ctrl) {
    controller = ctrl;
    
    if (!buildCommandTable()) {
        return false;
    }
    
    if (!buildRegisterLists()) {
        return false;
    }
    
    return true;
}

bool IntelBatchValidator::start() {
    if (started) {
        return true;
    }
    
    started = true;
    IOLog("IntelBatchValidator: Started\n");
    return true;
}

void IntelBatchValidator::stop() {
    if (!started) {
        return;
    }
    
    started = false;
    IOLog("IntelBatchValidator: Stopped\n");
}

ValidationError IntelBatchValidator::validateBatch(IntelBatchBuffer* batch,
                                                    IntelContext* context,
                                                    uint32_t engineMask) {
    return validateBatchWithRelocations(batch, context, engineMask, nullptr, 0);
}

ValidationError IntelBatchValidator::validateBatchWithRelocations(
    IntelBatchBuffer* batch,
    IntelContext* context,
    uint32_t engineMask,
    RelocationEntry* relocations,
    uint32_t relocationCount) {
    
    if (!batch) {
        return VALIDATION_ERROR_NULL_BUFFER;
    }
    
    IORecursiveLockLock(validatorLock);
    
    uint64_t startTime = mach_absolute_time();
    stats.batchesValidated++;
    
    // Create validation context
    ValidationContext ctx;
    ctx.batch = batch;
    ctx.context = context;
    ctx.engineMask = engineMask;
    ctx.recursionDepth = 0;
    ctx.maxRecursion = maxRecursionDepth;
    ctx.privilegedAllowed = privilegedAllowed;
    ctx.allowChaining = chainAllowed;
    ctx.relocations = relocations;
    ctx.relocationCount = relocationCount;
    
    // Get batch buffer data
    IntelGEMObject* batchObj = batch->getBatchObject();
    if (!batchObj) {
        IORecursiveLockUnlock(validatorLock);
        stats.batchesFailed++;
        return VALIDATION_ERROR_NULL_BUFFER;
    }
    
    uint32_t batchLength = batch->getLength();
    if (batchLength == 0 || batchLength > MAX_BATCH_SIZE) {
        IORecursiveLockUnlock(validatorLock);
        stats.batchesFailed++;
        return VALIDATION_ERROR_INVALID_SIZE;
    }
    
    // Check alignment (must be DWord aligned)
    if ((batchLength & 3) != 0) {
        IORecursiveLockUnlock(validatorLock);
        stats.batchesFailed++;
        return VALIDATION_ERROR_INVALID_ALIGNMENT;
    }
    
    // Apply relocations first
    if (relocations && relocationCount > 0) {
        ValidationError error = applyRelocations(batch, relocations, relocationCount);
        if (error != VALIDATION_OK) {
            IORecursiveLockUnlock(validatorLock);
            stats.batchesFailed++;
            return error;
        }
    }
    
    // Validate each command
    uint32_t* commands = (uint32_t*)batchObj->getCPUAddress();
    uint32_t offset = 0;
    
    while (offset < batchLength) {
        ValidationError error = validateCommand(&commands[offset / 4], offset, &ctx);
        if (error != VALIDATION_OK) {
            IOLog("IntelBatchValidator: Command validation failed at offset 0x%x: %d\n",
                  offset, error);
            IORecursiveLockUnlock(validatorLock);
            stats.batchesFailed++;
            return error;
        }
        
        uint32_t cmdLength = getCommandLength(&commands[offset / 4]);
        offset += cmdLength * 4;
        stats.commandsValidated++;
        
        // Check for buffer overflow
        if (offset > batchLength) {
            IORecursiveLockUnlock(validatorLock);
            stats.batchesFailed++;
            return VALIDATION_ERROR_BUFFER_OVERFLOW;
        }
    }
    
    // Validate chaining if enabled
    if (chainAllowed) {
        ValidationError error = validateChain(batch, &ctx);
        if (error != VALIDATION_OK) {
            IORecursiveLockUnlock(validatorLock);
            stats.batchesFailed++;
            return error;
        }
    }
    
    uint64_t elapsed = mach_absolute_time() - startTime;
    stats.totalValidationTimeUs += elapsed / 1000;
    stats.batchesPassed++;
    
    IORecursiveLockUnlock(validatorLock);
    return VALIDATION_OK;
}

ValidationError IntelBatchValidator::validateCommand(uint32_t* command,
                                                      uint32_t offset,
                                                      ValidationContext* ctx) {
    if (!command || !ctx) {
        return VALIDATION_ERROR_NULL_BUFFER;
    }
    
    uint32_t opcode = *command;
    
    // Get command descriptor
    CommandDescriptor* desc = parseCommand(command);
    if (!desc) {
        return VALIDATION_ERROR_INVALID_COMMAND;
    }
    
    // Check if command is allowed
    if (!isCommandAllowed(opcode, ctx->privilegedAllowed)) {
        stats.privilegedBlocked++;
        return VALIDATION_ERROR_PRIVILEGED_COMMAND;
    }
    
    // Command-specific validation
    ValidationError error = validateCommandInternal(command, offset, ctx);
    if (error != VALIDATION_OK) {
        return error;
    }
    
    return VALIDATION_OK;
}

ValidationError IntelBatchValidator::validateCommandInternal(uint32_t* command,
                                                              uint32_t offset,
                                                              ValidationContext* ctx) {
    uint32_t opcode = *command & 0xFF000000;
    
    switch (opcode) {
        case MI_LOAD_REGISTER_IMM:
            return validateLoadRegisterImm(command, ctx);
            
        case MI_STORE_REGISTER_MEM:
            return validateStoreRegisterMem(command, ctx);
            
        case MI_LOAD_REGISTER_MEM:
            return validateLoadRegisterMem(command, ctx);
            
        case GFX_OP_PIPE_CONTROL:
            return validatePipeControl(command, ctx);
            
        case MI_BATCH_BUFFER_START:
            return validateBatchStart(command, ctx);
            
        case MI_NOOP:
        case MI_BATCH_BUFFER_END:
        case MI_USER_INTERRUPT:
            // These are always safe
            return VALIDATION_OK;
            
        default:
            // Unknown or unvalidated command
            // Allow by default but log it
            return VALIDATION_OK;
    }
}

ValidationError IntelBatchValidator::validateLoadRegisterImm(uint32_t* command,
                                                              ValidationContext* ctx) {
    uint32_t count = (*command >> 0) & 0xFF;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t reg = command[1 + i * 2];
        
        if (!isRegisterAllowed(reg, true)) {
            return VALIDATION_ERROR_INVALID_REGISTER;
        }
        
        if (isReadOnlyRegister(reg)) {
            return VALIDATION_ERROR_INVALID_REGISTER;
        }
    }
    
    return VALIDATION_OK;
}

ValidationError IntelBatchValidator::validateStoreRegisterMem(uint32_t* command,
                                                               ValidationContext* ctx) {
    uint32_t reg = command[1];
    uint64_t address = ((uint64_t)command[3] << 32) | command[2];
    
    if (!isRegisterAllowed(reg, false)) {
        return VALIDATION_ERROR_INVALID_REGISTER;
    }
    
    if (!isAddressAllowed(address, ctx->context)) {
        return VALIDATION_ERROR_INVALID_ADDRESS;
    }
    
    return VALIDATION_OK;
}

ValidationError IntelBatchValidator::validateLoadRegisterMem(uint32_t* command,
                                                              ValidationContext* ctx) {
    uint32_t reg = command[1];
    uint64_t address = ((uint64_t)command[3] << 32) | command[2];
    
    if (!isRegisterAllowed(reg, true)) {
        return VALIDATION_ERROR_INVALID_REGISTER;
    }
    
    if (isReadOnlyRegister(reg)) {
        return VALIDATION_ERROR_INVALID_REGISTER;
    }
    
    if (!isAddressAllowed(address, ctx->context)) {
        return VALIDATION_ERROR_INVALID_ADDRESS;
    }
    
    return VALIDATION_OK;
}

ValidationError IntelBatchValidator::validatePipeControl(uint32_t* command,
                                                          ValidationContext* ctx) {
    // PIPE_CONTROL is generally safe but can trigger flushes
    // Validate target address if present
    uint32_t flags = command[1];
    
    if (flags & (1 << 21)) {  // Post-sync operation
        uint64_t address = ((uint64_t)command[3] << 32) | command[2];
        if (!isAddressAllowed(address, ctx->context)) {
            return VALIDATION_ERROR_INVALID_ADDRESS;
        }
    }
    
    return VALIDATION_OK;
}

ValidationError IntelBatchValidator::validateBatchStart(uint32_t* command,
                                                         ValidationContext* ctx) {
    if (!ctx->allowChaining) {
        return VALIDATION_ERROR_UNSAFE_OPERATION;
    }
    
    if (!checkRecursionDepth(ctx)) {
        return VALIDATION_ERROR_RECURSION_DEPTH;
    }
    
    uint64_t targetAddress = getChainTarget(command);
    if (!isAddressAllowed(targetAddress, ctx->context)) {
        return VALIDATION_ERROR_INVALID_ADDRESS;
    }
    
    return VALIDATION_OK;
}

ValidationError IntelBatchValidator::validateStateBaseAddress(uint32_t* command,
                                                               ValidationContext* ctx) {
    // STATE_BASE_ADDRESS sets up base pointers
    // Validate all addresses are within bounds
    
    for (int i = 0; i < 8; i++) {
        if (command[0] & (1 << (i + 1))) {
            uint64_t address = ((uint64_t)command[i * 2 + 2] << 32) | command[i * 2 + 1];
            if (address != 0 && !isAddressAllowed(address, ctx->context)) {
                return VALIDATION_ERROR_INVALID_ADDRESS;
            }
        }
    }
    
    return VALIDATION_OK;
}

bool IntelBatchValidator::isCommandAllowed(uint32_t opcode, bool privileged) {
    uint32_t cmdType = opcode & 0xFF000000;
    
    // Always allowed commands
    switch (cmdType) {
        case MI_NOOP:
        case MI_BATCH_BUFFER_END:
        case MI_USER_INTERRUPT:
        case MI_ARB_CHECK:
            return true;
    }
    
    // Privileged commands
    switch (cmdType) {
        case MI_OVERLAY_FLIP:
        case MI_DISPLAY_FLIP:
        case MI_SET_CONTEXT:
        case MI_UPDATE_GTT:
            return privileged;
    }
    
    // Everything else is allowed by default
    return true;
}

bool IntelBatchValidator::isRegisterAllowed(uint32_t reg, bool write) {
    // Check blacklist first
    if (isBlacklistedRegister(reg)) {
        return false;
    }
    
    // Check write to read-only
    if (write && isReadOnlyRegister(reg)) {
        return false;
    }
    
    // Check whitelist if available
    if (whitelistSize > 0) {
        return isWhitelistedRegister(reg);
    }
    
    // Check if in safe range
    if (reg >= SAFE_REG_3D_START && reg <= SAFE_REG_3D_END) {
        return true;
    }
    
    if (reg >= SAFE_REG_VERTEX_START && reg <= SAFE_REG_VERTEX_END) {
        return true;
    }
    
    // Deny access to privileged ranges
    if (reg >= PRIV_REG_FORCEWAKE_START && reg <= PRIV_REG_FORCEWAKE_END) {
        return false;
    }
    
    return true;
}

bool IntelBatchValidator::isAddressAllowed(uint64_t address, IntelContext* context) {
    if (!context) {
        return false;
    }
    
    // Check if address is within context's address space
    // For now, accept any non-zero address
    // In production, validate against context's PPGTT
    
    if (address == 0) {
        return false;
    }
    
    if (address >= MMIO_BASE && address < (MMIO_BASE + MMIO_SIZE)) {
        // Deny MMIO access from batch buffers
        return false;
    }
    
    return true;
}

CommandDescriptor* IntelBatchValidator::parseCommand(uint32_t* command) {
    uint32_t opcode = *command & 0xFF000000;
    
    // Search command table
    for (uint32_t i = 0; i < commandTableSize; i++) {
        if (commandTable[i].opcode == opcode) {
            return &commandTable[i];
        }
    }
    
    return nullptr;
}

uint32_t IntelBatchValidator::getCommandLength(uint32_t* command) {
    uint32_t opcode = *command;
    uint32_t cmdType = opcode & 0xFF000000;
    
    // MI commands have length in bits [7:0]
    if ((cmdType >= 0x00000000 && cmdType <= 0x3F000000)) {
        uint32_t lengthField = (opcode >> 0) & 0xFF;
        return lengthField + 2;  // Length field + opcode + length itself
    }
    
    // 3D commands have length in bits [7:0]
    if (cmdType >= 0x60000000 && cmdType <= 0x7F000000) {
        uint32_t lengthField = (opcode >> 0) & 0xFF;
        return lengthField + 2;
    }
    
    // BLT commands
    if (cmdType >= 0x40000000 && cmdType <= 0x5F000000) {
        uint32_t lengthField = (opcode >> 0) & 0x1F;
        return lengthField + 2;
    }
    
    // Default to 1 DWord
    return 1;
}

CommandType IntelBatchValidator::getCommandType(uint32_t opcode) {
    uint32_t cmdType = opcode & 0xFF000000;
    
    switch (cmdType) {
        case MI_NOOP:
            return CMD_TYPE_NOOP;
        case MI_BATCH_BUFFER_START:
            return CMD_TYPE_BATCH_BUFFER_START;
        case MI_BATCH_BUFFER_END:
            return CMD_TYPE_BATCH_BUFFER_END;
        case MI_LOAD_REGISTER_IMM:
            return CMD_TYPE_MI_LOAD_REGISTER_IMM;
        case MI_STORE_REGISTER_MEM:
            return CMD_TYPE_MI_STORE_REGISTER_MEM;
        case MI_LOAD_REGISTER_MEM:
            return CMD_TYPE_MI_LOAD_REGISTER_MEM;
        case GFX_OP_PIPE_CONTROL:
            return CMD_TYPE_PIPE_CONTROL;
        default:
            return CMD_TYPE_UNKNOWN;
    }
}

ValidationError IntelBatchValidator::applyRelocations(IntelBatchBuffer* batch,
                                                       RelocationEntry* relocations,
                                                       uint32_t count) {
    if (!batch || !relocations || count == 0) {
        return VALIDATION_OK;
    }
    
    if (count > MAX_RELOCATIONS) {
        return VALIDATION_ERROR_BUFFER_OVERFLOW;
    }
    
    IntelGEMObject* batchObj = batch->getBatchObject();
    uint32_t* commands = (uint32_t*)batchObj->getCPUAddress();
    
    for (uint32_t i = 0; i < count; i++) {
        RelocationEntry* reloc = &relocations[i];
        
        if (reloc->offset >= batch->getLength()) {
            return VALIDATION_ERROR_INVALID_ADDRESS;
        }
        
        uint32_t* target = &commands[reloc->offset / 4];
        if (!relocateAddress(target, reloc)) {
            return VALIDATION_ERROR_INVALID_ADDRESS;
        }
        
        stats.commandsRelocated++;
    }
    
    return VALIDATION_OK;
}

bool IntelBatchValidator::relocateAddress(uint32_t* address, RelocationEntry* relocation) {
    if (!address || !relocation || !relocation->target) {
        return false;
    }
    
    // Get target GPU address
    uint64_t targetGPUAddr = relocation->target->getGPUAddress();
    uint64_t finalAddr = targetGPUAddr + relocation->targetOffset;
    
    // Write relocated address (assume 48-bit addresses)
    address[0] = (uint32_t)(finalAddr & 0xFFFFFFFF);
    if (address[1]) {
        address[1] = (uint32_t)(finalAddr >> 32);
    }
    
    return true;
}

ValidationError IntelBatchValidator::validateChain(IntelBatchBuffer* batch,
                                                    ValidationContext* ctx) {
    // Check for chain commands in the batch
    IntelGEMObject* batchObj = batch->getBatchObject();
    uint32_t* commands = (uint32_t*)batchObj->getCPUAddress();
    uint32_t length = batch->getLength();
    uint32_t offset = 0;
    
    while (offset < length) {
        uint32_t opcode = commands[offset / 4] & 0xFF000000;
        
        if (opcode == MI_BATCH_BUFFER_START) {
            if (!isChainCommandAllowed(&commands[offset / 4])) {
                return VALIDATION_ERROR_UNSAFE_OPERATION;
            }
        }
        
        uint32_t cmdLength = getCommandLength(&commands[offset / 4]);
        offset += cmdLength * 4;
    }
    
    return VALIDATION_OK;
}

bool IntelBatchValidator::isChainCommandAllowed(uint32_t* command) {
    if (!chainAllowed) {
        return false;
    }
    
    // Check for second-level batch buffer bit
    uint32_t flags = *command;
    bool secondLevel = (flags & (1 << 22)) != 0;
    
    // Second-level batches are safer
    return secondLevel;
}

uint64_t IntelBatchValidator::getChainTarget(uint32_t* command) {
    // Address is in DWords 1 and 2
    return ((uint64_t)command[2] << 32) | command[1];
}

bool IntelBatchValidator::checkPrivileges(uint32_t opcode, IntelContext* context) {
    if (privilegedAllowed) {
        return true;
    }
    
    // Check if context is privileged (kernel context)
    // For now, deny all privileged operations
    return false;
}

bool IntelBatchValidator::checkAddressSafety(uint64_t address, uint32_t length) {
    // Check for overflow
    if (address + length < address) {
        return false;
    }
    
    // Check bounds
    if (address >= MMIO_BASE && address < (MMIO_BASE + MMIO_SIZE)) {
        return false;
    }
    
    return true;
}

bool IntelBatchValidator::checkRecursionDepth(ValidationContext* ctx) {
    return ctx->recursionDepth < ctx->maxRecursion;
}

bool IntelBatchValidator::isWhitelistedRegister(uint32_t reg) {
    return isInRegisterList(reg, whitelistedRegisters, whitelistSize);
}

bool IntelBatchValidator::isBlacklistedRegister(uint32_t reg) {
    return isInRegisterList(reg, blacklistedRegisters, blacklistSize);
}

bool IntelBatchValidator::isReadOnlyRegister(uint32_t reg) {
    return isInRegisterList(reg, readOnlyRegisters, readOnlySize);
}

bool IntelBatchValidator::isInRegisterList(uint32_t reg, uint32_t* list, uint32_t size) {
    if (!list || size == 0) {
        return false;
    }
    
    for (uint32_t i = 0; i < size; i++) {
        if (list[i] == reg) {
            return true;
        }
    }
    
    return false;
}

void IntelBatchValidator::getStats(ValidationStats* outStats) {
    if (!outStats) {
        return;
    }
    
    IORecursiveLockLock(validatorLock);
    memcpy(outStats, &stats, sizeof(ValidationStats));
    IORecursiveLockUnlock(validatorLock);
}

void IntelBatchValidator::resetStats() {
    IORecursiveLockLock(validatorLock);
    bzero(&stats, sizeof(stats));
    IORecursiveLockUnlock(validatorLock);
}

void IntelBatchValidator::printStats() {
    ValidationStats localStats;
    getStats(&localStats);
    
    IOLog("Batches Validated: %llu\n", localStats.batchesValidated);
    IOLog("Batches Passed:    %llu\n", localStats.batchesPassed);
    IOLog("Batches Failed:    %llu\n", localStats.batchesFailed);
    IOLog("Commands Validated: %llu\n", localStats.commandsValidated);
    IOLog("Commands Relocated: %llu\n", localStats.commandsRelocated);
    IOLog("Privileged Blocked: %llu\n", localStats.privilegedBlocked);
    IOLog("Unsafe Blocked:     %llu\n", localStats.unsafeBlocked);
    
    if (localStats.batchesValidated > 0) {
        IOLog("Average Time:       %llu us\n",
              localStats.totalValidationTimeUs / localStats.batchesValidated);
    }
    
}

bool IntelBatchValidator::buildCommandTable() {
    // Build a simple command table
    // In production, this would be much more comprehensive
    
    commandTableSize = 20;
    commandTable = (CommandDescriptor*)IOMalloc(commandTableSize * sizeof(CommandDescriptor));
    if (!commandTable) {
        return false;
    }
    
    int idx = 0;
    
    commandTable[idx++] = {MI_NOOP, CMD_TYPE_NOOP, 1, 0, "MI_NOOP"};
    commandTable[idx++] = {MI_BATCH_BUFFER_END, CMD_TYPE_BATCH_BUFFER_END, 1, 0, "MI_BATCH_BUFFER_END"};
    commandTable[idx++] = {MI_BATCH_BUFFER_START, CMD_TYPE_BATCH_BUFFER_START, 3, CMD_FLAG_CHAINED, "MI_BATCH_BUFFER_START"};
    commandTable[idx++] = {MI_USER_INTERRUPT, CMD_TYPE_UNKNOWN, 1, 0, "MI_USER_INTERRUPT"};
    commandTable[idx++] = {MI_LOAD_REGISTER_IMM, CMD_TYPE_MI_LOAD_REGISTER_IMM, 0, CMD_FLAG_RELOCATABLE, "MI_LOAD_REGISTER_IMM"};
    commandTable[idx++] = {MI_STORE_REGISTER_MEM, CMD_TYPE_MI_STORE_REGISTER_MEM, 4, CMD_FLAG_RELOCATABLE, "MI_STORE_REGISTER_MEM"};
    commandTable[idx++] = {MI_LOAD_REGISTER_MEM, CMD_TYPE_MI_LOAD_REGISTER_MEM, 4, CMD_FLAG_RELOCATABLE, "MI_LOAD_REGISTER_MEM"};
    commandTable[idx++] = {GFX_OP_PIPE_CONTROL, CMD_TYPE_PIPE_CONTROL, 6, 0, "PIPE_CONTROL"};
    
    IOLog("IntelBatchValidator: Built command table with %d entries\n", idx);
    return true;
}

bool IntelBatchValidator::buildRegisterLists() {
    // Build register allow/deny lists
    // In production, these would be comprehensive
    
    // Blacklist: registers that should never be accessed
    blacklistSize = 10;
    blacklistedRegisters = (uint32_t*)IOMalloc(blacklistSize * sizeof(uint32_t));
    if (!blacklistedRegisters) {
        return false;
    }
    
    blacklistedRegisters[0] = 0x2000;  // GT forcewake
    blacklistedRegisters[1] = 0x2010;  // GT power control
    blacklistedRegisters[2] = 0x4000;  // Display control
    blacklistedRegisters[3] = 0xA000;  // Forcewake range
    
    // Read-only registers
    readOnlySize = 5;
    readOnlyRegisters = (uint32_t*)IOMalloc(readOnlySize * sizeof(uint32_t));
    if (!readOnlyRegisters) {
        return false;
    }
    
    readOnlyRegisters[0] = 0x2078;  // GT thread counts
    readOnlyRegisters[1] = 0x2084;  // GT slice info
    
    IOLog("IntelBatchValidator: Built register lists\n");
    return true;
}

void IntelBatchValidator::freeCommandTable() {
    if (commandTable) {
        IOFree(commandTable, commandTableSize * sizeof(CommandDescriptor));
        commandTable = nullptr;
    }
    commandTableSize = 0;
}

void IntelBatchValidator::freeRegisterLists() {
    if (whitelistedRegisters) {
        IOFree(whitelistedRegisters, whitelistSize * sizeof(uint32_t));
        whitelistedRegisters = nullptr;
    }
    
    if (blacklistedRegisters) {
        IOFree(blacklistedRegisters, blacklistSize * sizeof(uint32_t));
        blacklistedRegisters = nullptr;
    }
    
    if (readOnlyRegisters) {
        IOFree(readOnlyRegisters, readOnlySize * sizeof(uint32_t));
        readOnlyRegisters = nullptr;
    }
    
    whitelistSize = 0;
    blacklistSize = 0;
    readOnlySize = 0;
}
