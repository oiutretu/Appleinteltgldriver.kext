/*
 * IntelCompute.cpp - Compute Shader Implementation (GPGPU)
 * Week 27 - Phase 6: Compute/GPGPU Acceleration
 */

#include "IntelCompute.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelCompute, OSObject)

// Initialization
bool IntelCompute::init() {
    if (!super::init()) {
        return false;
    }
    
    controller = nullptr;
    computeRing = nullptr;
    computeContext = nullptr;
    lock = nullptr;
    initialized = false;
    computeActive = false;
    
    memset(&stats, 0, sizeof(stats));
    
    return true;
}

void IntelCompute::free() {
    if (initialized) {
        stop();
    }
    
    if (lock) {
        IORecursiveLockFree(lock);
        lock = nullptr;
    }
    
    super::free();
}

bool IntelCompute::initWithController(AppleIntelTGLController* ctrl) {
    if (!ctrl) {
        return false;
    }
    
    controller = ctrl;
    
    // Create lock
    lock = IORecursiveLockAlloc();
    if (!lock) {
        IOLog("IntelCompute: Failed to allocate lock\n");
        return false;
    }
    
    initialized = true;
    IOLog("IntelCompute: Initialized successfully\n");
    return true;
}

// Lifecycle
bool IntelCompute::start() {
    IORecursiveLockLock(lock);
    
    if (computeActive) {
        IORecursiveLockUnlock(lock);
        return true;
    }
    
    // Get render ring buffer (compute uses RCS on Gen12)
    computeRing = controller->getRenderRing();
    if (!computeRing) {
        IOLog("IntelCompute: Failed to get compute ring\n");
        IORecursiveLockUnlock(lock);
        return false;
    }
    
    // Create compute context
    computeContext = new IntelContext();
    if (!computeContext || !computeContext->initWithController(controller)) {
        if (computeContext) computeContext->release();
        IOLog("IntelCompute: Failed to create compute context\n");
        IORecursiveLockUnlock(lock);
        return false;
    }
    
    // Initialize GPGPU pipeline
    uint32_t commands[8];
    uint32_t* cmd = commands;
    
    cmd = buildPipelineSelect(cmd);
    cmd = buildComputeModeCommand(cmd);
    
    IntelRequest* request = nullptr;
    ComputeError error = submitComputeCommand(commands,
                                             (uint32_t)(cmd - commands),
                                             &request);
    if (error != COMPUTE_SUCCESS) {
        IOLog("IntelCompute: Failed to initialize GPGPU pipeline: %d\n", error);
        computeContext->release();
        computeContext = nullptr;
        IORecursiveLockUnlock(lock);
        return false;
    }
    
    if (request) {
        waitForCompletion(request, 1000);
        request->release();
    }
    
    computeActive = true;
    IOLog("IntelCompute: Started successfully\n");
    IOLog("IntelCompute: Max work group size: %u\n", getMaxWorkGroupSize());
    IOLog("IntelCompute: Compute units: %u\n", getComputeUnits());
    IOLog("IntelCompute: Max SLM size: %u KB\n", getMaxSLMSize() / 1024);
    
    IORecursiveLockUnlock(lock);
    return true;
}

void IntelCompute::stop() {
    IORecursiveLockLock(lock);
    
    if (!computeActive) {
        IORecursiveLockUnlock(lock);
        return;
    }
    
    // Wait for idle
    waitForIdle(5000);
    
    // Release context
    if (computeContext) {
        computeContext->release();
        computeContext = nullptr;
    }
    
    computeActive = false;
    IOLog("IntelCompute: Stopped\n");
    
    IORecursiveLockUnlock(lock);
}

// Kernel Management
IntelComputeKernel* IntelCompute::createKernel(const void* kernelCode,
                                              uint32_t kernelSize) {
    if (!kernelCode || kernelSize == 0) {
        return nullptr;
    }
    
    IORecursiveLockLock(lock);
    
    // Allocate kernel structure
    IntelComputeKernel* kernel = (IntelComputeKernel*)IOMalloc(sizeof(IntelComputeKernel));
    if (!kernel) {
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    memset(kernel, 0, sizeof(IntelComputeKernel));
    
    // Create GEM object for kernel
    kernel->kernelObject = controller->allocateGEMObject(kernelSize);
    if (!kernel->kernelObject) {
        IOFree(kernel, sizeof(IntelComputeKernel));
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    
    // Copy kernel code
    void* mapped = nullptr;
    if (!kernel->kernelObject->mapCPU(&mapped)) {
        kernel->kernelObject->release();
        IOFree(kernel, sizeof(IntelComputeKernel));
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    memcpy(mapped, kernelCode, kernelSize);
    kernel->kernelObject->unmapCPU();
    
    kernel->kernelSize = kernelSize;
    kernel->workGroupSizeX = 1;
    kernel->workGroupSizeY = 1;
    kernel->workGroupSizeZ = 1;
    kernel->compiled = false;
    kernel->gpuAddress = kernel->kernelObject->getGPUAddress();
    
    IORecursiveLockUnlock(lock);
    return kernel;
}

void IntelCompute::destroyKernel(IntelComputeKernel* kernel) {
    if (!kernel) {
        return;
    }
    
    IORecursiveLockLock(lock);
    
    if (kernel->kernelObject) {
        kernel->kernelObject->release();
    }
    
    IOFree(kernel, sizeof(IntelComputeKernel));
    
    IORecursiveLockUnlock(lock);
}

ComputeError IntelCompute::compileKernel(IntelComputeKernel* kernel) {
    if (!validateKernel(kernel)) {
        return COMPUTE_KERNEL_ERROR;
    }
    
    // In a real implementation, this would invoke the compute shader compiler
    // For now, we assume the kernel is pre-compiled
    kernel->compiled = true;
    
    return COMPUTE_SUCCESS;
}

ComputeError IntelCompute::setKernelWorkGroupSize(IntelComputeKernel* kernel,
                                                 uint32_t sizeX,
                                                 uint32_t sizeY,
                                                 uint32_t sizeZ) {
    if (!kernel) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    if (!validateWorkGroupSize(sizeX, sizeY, sizeZ)) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    kernel->workGroupSizeX = sizeX;
    kernel->workGroupSizeY = sizeY;
    kernel->workGroupSizeZ = sizeZ;
    IORecursiveLockUnlock(lock);
    
    return COMPUTE_SUCCESS;
}

ComputeError IntelCompute::setKernelSLMSize(IntelComputeKernel* kernel,
                                           uint32_t slmSize) {
    if (!kernel || slmSize > MAX_SLM_SIZE_PER_GROUP) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    kernel->slmSize = slmSize;
    IORecursiveLockUnlock(lock);
    
    return COMPUTE_SUCCESS;
}

// Buffer Management
IntelComputeBuffer* IntelCompute::createBuffer(uint64_t size,
                                              IntelMemoryType type,
                                              IntelBufferAccess access) {
    if (size == 0) {
        return nullptr;
    }
    
    IORecursiveLockLock(lock);
    
    IntelComputeBuffer* buffer = (IntelComputeBuffer*)IOMalloc(sizeof(IntelComputeBuffer));
    if (!buffer) {
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    memset(buffer, 0, sizeof(IntelComputeBuffer));
    
    // Create GEM object
    buffer->object = controller->allocateGEMObject(size);
    if (!buffer->object) {
        IOFree(buffer, sizeof(IntelComputeBuffer));
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    
    buffer->size = size;
    buffer->gpuAddress = buffer->object->getGPUAddress();
    buffer->accessFlags = access;
    buffer->memoryType = type;
    buffer->mapped = false;
    buffer->cpuAddress = nullptr;
    
    IORecursiveLockUnlock(lock);
    return buffer;
}

void IntelCompute::destroyBuffer(IntelComputeBuffer* buffer) {
    if (!buffer) {
        return;
    }
    
    IORecursiveLockLock(lock);
    
    if (buffer->mapped) {
        unmapBuffer(buffer);
    }
    
    if (buffer->object) {
        buffer->object->release();
    }
    
    IOFree(buffer, sizeof(IntelComputeBuffer));
    
    IORecursiveLockUnlock(lock);
}

ComputeError IntelCompute::writeBuffer(IntelComputeBuffer* buffer,
                                      const void* data,
                                      uint64_t size,
                                      uint64_t offset) {
    if (!buffer || !data || (offset + size) > buffer->size) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    void* mapped = nullptr;
    if (!buffer->object->mapCPU(&mapped)) {
        IORecursiveLockUnlock(lock);
        return COMPUTE_MEMORY_ERROR;
    }
    
    memcpy((uint8_t*)mapped + offset, data, size);
    buffer->object->unmapCPU();
    
    stats.globalMemoryWrites++;
    
    IORecursiveLockUnlock(lock);
    return COMPUTE_SUCCESS;
}

ComputeError IntelCompute::readBuffer(IntelComputeBuffer* buffer,
                                     void* data,
                                     uint64_t size,
                                     uint64_t offset) {
    if (!buffer || !data || (offset + size) > buffer->size) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    void* mapped = nullptr;
    if (!buffer->object->mapCPU(&mapped)) {
        IORecursiveLockUnlock(lock);
        return COMPUTE_MEMORY_ERROR;
    }
    
    memcpy(data, (uint8_t*)mapped + offset, size);
    buffer->object->unmapCPU();
    
    stats.globalMemoryReads++;
    
    IORecursiveLockUnlock(lock);
    return COMPUTE_SUCCESS;
}

ComputeError IntelCompute::mapBuffer(IntelComputeBuffer* buffer,
                                    void** cpuAddress) {
    if (!buffer || !cpuAddress) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    if (buffer->mapped) {
        *cpuAddress = buffer->cpuAddress;
        IORecursiveLockUnlock(lock);
        return COMPUTE_SUCCESS;
    }
    
    void* mapped = nullptr;
    if (!buffer->object->mapCPU(&mapped)) {
        IORecursiveLockUnlock(lock);
        return COMPUTE_MEMORY_ERROR;
    }
    
    buffer->mapped = true;
    buffer->cpuAddress = mapped;
    *cpuAddress = mapped;
    
    IORecursiveLockUnlock(lock);
    return COMPUTE_SUCCESS;
}

ComputeError IntelCompute::unmapBuffer(IntelComputeBuffer* buffer) {
    if (!buffer || !buffer->mapped) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    buffer->object->unmapCPU();
    buffer->mapped = false;
    buffer->cpuAddress = nullptr;
    
    IORecursiveLockUnlock(lock);
    return COMPUTE_SUCCESS;
}

// Image Management
IntelComputeImage* IntelCompute::createImage(uint32_t width,
                                            uint32_t height,
                                            IntelImageFormat format,
                                            IntelBufferAccess access) {
    if (width == 0 || height == 0) {
        return nullptr;
    }
    
    IORecursiveLockLock(lock);
    
    IntelComputeImage* image = (IntelComputeImage*)IOMalloc(sizeof(IntelComputeImage));
    if (!image) {
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    memset(image, 0, sizeof(IntelComputeImage));
    
    // Calculate image size
    uint32_t bpp = getImageBytesPerPixel(format);
    uint32_t pitch = calculateImagePitch(width, format);
    uint64_t imageSize = pitch * height;
    
    // Create GEM object
    image->object = controller->allocateGEMObject(imageSize);
    if (!image->object) {
        IOFree(image, sizeof(IntelComputeImage));
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    
    image->width = width;
    image->height = height;
    image->depth = 1;
    image->format = format;
    image->gpuAddress = image->object->getGPUAddress();
    image->pitch = pitch;
    image->accessFlags = access;
    
    IORecursiveLockUnlock(lock);
    return image;
}

void IntelCompute::destroyImage(IntelComputeImage* image) {
    if (!image) {
        return;
    }
    
    IORecursiveLockLock(lock);
    
    if (image->object) {
        image->object->release();
    }
    
    IOFree(image, sizeof(IntelComputeImage));
    
    IORecursiveLockUnlock(lock);
}

ComputeError IntelCompute::writeImage(IntelComputeImage* image,
                                     const void* data) {
    if (!image || !data) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    void* mapped = nullptr;
    if (!image->object->mapCPU(&mapped)) {
        IORecursiveLockUnlock(lock);
        return COMPUTE_MEMORY_ERROR;
    }
    
    uint64_t imageSize = image->pitch * image->height;
    memcpy(mapped, data, imageSize);
    
    image->object->unmapCPU();
    
    IORecursiveLockUnlock(lock);
    return COMPUTE_SUCCESS;
}

ComputeError IntelCompute::readImage(IntelComputeImage* image, void* data) {
    if (!image || !data) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    void* mapped = nullptr;
    if (!image->object->mapCPU(&mapped)) {
        IORecursiveLockUnlock(lock);
        return COMPUTE_MEMORY_ERROR;
    }
    
    uint64_t imageSize = image->pitch * image->height;
    memcpy(data, mapped, imageSize);
    
    image->object->unmapCPU();
    
    IORecursiveLockUnlock(lock);
    return COMPUTE_SUCCESS;
}

// Kernel Execution
ComputeError IntelCompute::dispatch(const ComputeDispatchParams* params) {
    if (!validateDispatchParams(params)) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    recordDispatchStart();
    
    uint32_t commands[256];
    uint32_t* cmd = commands;
    
    // Build media interface descriptor
    cmd = buildMediaInterfaceDescriptor(cmd, params->kernel);
    
    // Build compute walker command
    cmd = buildComputeWalkerCommand(cmd, params);
    
    // Build media state flush
    cmd = buildMediaStateFlush(cmd);
    
    // Submit command
    IntelRequest* request = nullptr;
    ComputeError error = submitComputeCommand(commands,
                                             (uint32_t)(cmd - commands),
                                             &request);
    
    if (error == COMPUTE_SUCCESS && request) {
        waitForCompletion(request, 10000);  // 10 second timeout for compute
        request->release();
        
        uint32_t workGroups = calculateWorkGroups(params->globalSizeX, params->localSizeX) *
                             calculateWorkGroups(params->globalSizeY, params->localSizeY) *
                             calculateWorkGroups(params->globalSizeZ, params->localSizeZ);
        uint32_t threads = calculateTotalThreads(params);
        recordDispatchComplete(workGroups, threads);
    }
    
    IORecursiveLockUnlock(lock);
    return error;
}

ComputeError IntelCompute::dispatchIndirect(IntelComputeKernel* kernel,
                                           IntelComputeBuffer* indirectBuffer,
                                           uint64_t offset) {
    if (!kernel || !indirectBuffer) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    // Read dispatch parameters from indirect buffer
    struct {
        uint32_t x, y, z;
    } indirectParams;
    
    ComputeError error = readBuffer(indirectBuffer, &indirectParams,
                                   sizeof(indirectParams), offset);
    if (error != COMPUTE_SUCCESS) {
        return error;
    }
    
    // Build dispatch parameters
    ComputeDispatchParams params = {};
    params.kernel = kernel;
    params.globalSizeX = indirectParams.x;
    params.globalSizeY = indirectParams.y;
    params.globalSizeZ = indirectParams.z;
    params.localSizeX = kernel->workGroupSizeX;
    params.localSizeY = kernel->workGroupSizeY;
    params.localSizeZ = kernel->workGroupSizeZ;
    
    return dispatch(&params);
}

// Synchronization
ComputeError IntelCompute::memoryBarrier(IntelBarrierType type) {
    uint32_t commands[16];
    uint32_t* cmd = commands;
    
    cmd = buildBarrierCommand(cmd, type);
    
    IntelRequest* request = nullptr;
    ComputeError error = submitComputeCommand(commands,
                                             (uint32_t)(cmd - commands),
                                             &request);
    if (error == COMPUTE_SUCCESS && request) {
        waitForCompletion(request, 1000);
        request->release();
        stats.barrierSynchronizations++;
    }
    
    return error;
}

ComputeError IntelCompute::flush() {
    if (!computeRing) {
        return COMPUTE_DISPATCH_ERROR;
    }
    
    uint32_t commands[8];
    uint32_t* cmd = commands;
    
    cmd = buildMediaStateFlush(cmd);
    
    IntelRequest* request = nullptr;
    ComputeError error = submitComputeCommand(commands,
                                             (uint32_t)(cmd - commands),
                                             &request);
    if (error == COMPUTE_SUCCESS && request) {
        waitForCompletion(request, 1000);
        request->release();
    }
    
    return error;
}

ComputeError IntelCompute::waitForIdle(uint32_t timeoutMs) {
    flush();
    
    // Wait for all operations to complete
    AbsoluteTime deadline;
    clock_interval_to_deadline(timeoutMs, kMillisecondScale, (uint64_t*)&deadline);
    
    while (!isIdle()) {
        AbsoluteTime now;
        clock_get_uptime((uint64_t*)&now);
        if (now >= deadline) {
            return COMPUTE_TIMEOUT;
        }
        IOSleep(1);
    }
    
    return COMPUTE_SUCCESS;
}

bool IntelCompute::isIdle() {
    return computeRing ? computeRing->isIdle() : true;
}

// Atomic Operations
ComputeError IntelCompute::atomicOperation(IntelComputeBuffer* buffer,
                                          uint64_t offset,
                                          IntelAtomicOp op,
                                          uint32_t value,
                                          uint32_t* oldValue) {
    if (!buffer || offset >= buffer->size) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    void* mapped = nullptr;
    if (!buffer->object->mapCPU(&mapped)) {
        IORecursiveLockUnlock(lock);
        return COMPUTE_MEMORY_ERROR;
    }
    
    volatile uint32_t* ptr = (volatile uint32_t*)((uint8_t*)mapped + offset);
    uint32_t old = *ptr;
    
    switch (op) {
        case ATOMIC_ADD:
            *ptr = old + value;
            break;
        case ATOMIC_SUB:
            *ptr = old - value;
            break;
        case ATOMIC_XCHG:
            *ptr = value;
            break;
        case ATOMIC_MIN:
            *ptr = (old < value) ? old : value;
            break;
        case ATOMIC_MAX:
            *ptr = (old > value) ? old : value;
            break;
        case ATOMIC_AND:
            *ptr = old & value;
            break;
        case ATOMIC_OR:
            *ptr = old | value;
            break;
        case ATOMIC_XOR:
            *ptr = old ^ value;
            break;
        default:
            buffer->object->unmapCPU();
            IORecursiveLockUnlock(lock);
            return COMPUTE_INVALID_PARAMS;
    }
    
    if (oldValue) {
        *oldValue = old;
    }
    
    buffer->object->unmapCPU();
    stats.atomicOperations++;
    
    IORecursiveLockUnlock(lock);
    return COMPUTE_SUCCESS;
}

// Statistics
void IntelCompute::getStatistics(ComputeStats* outStats) {
    if (!outStats) {
        return;
    }
    
    IORecursiveLockLock(lock);
    memcpy(outStats, &stats, sizeof(ComputeStats));
    IORecursiveLockUnlock(lock);
}

void IntelCompute::resetStatistics() {
    IORecursiveLockLock(lock);
    memset(&stats, 0, sizeof(ComputeStats));
    IORecursiveLockUnlock(lock);
}

void IntelCompute::printStatistics() {
    IOLog("Total dispatches:      %llu\n", stats.totalDispatches);
    IOLog("Total work groups:     %llu\n", stats.totalWorkGroups);
    IOLog("Total threads:         %llu\n", stats.totalThreads);
    IOLog("Atomic operations:     %llu\n", stats.atomicOperations);
    IOLog("Barrier syncs:         %llu\n", stats.barrierSynchronizations);
    IOLog("SLM bytes used:        %llu\n", stats.slmBytesUsed);
    IOLog("Global mem reads:      %llu\n", stats.globalMemoryReads);
    IOLog("Global mem writes:     %llu\n", stats.globalMemoryWrites);
    IOLog("Avg dispatch time:     %u us\n", stats.averageDispatchTimeUs);
    IOLog("Max dispatch time:     %u us\n", stats.maxDispatchTimeUs);
    IOLog("GPU utilization:       %u%%\n", stats.gpuUtilization);
    IOLog("Errors:                %llu\n", stats.errors);
}

// Hardware Capabilities
uint32_t IntelCompute::getMaxWorkGroupSize() {
    return MAX_WORK_GROUP_INVOCATIONS;
}

uint32_t IntelCompute::getMaxWorkGroupSizeX() {
    return MAX_WORK_GROUP_SIZE_X;
}

uint32_t IntelCompute::getMaxWorkGroupSizeY() {
    return MAX_WORK_GROUP_SIZE_Y;
}

uint32_t IntelCompute::getMaxWorkGroupSizeZ() {
    return MAX_WORK_GROUP_SIZE_Z;
}

uint32_t IntelCompute::getMaxSLMSize() {
    return MAX_SLM_SIZE_PER_GROUP;
}

uint32_t IntelCompute::getComputeUnits() {
    return 96;  // Gen12 Tiger Lake: 96 EUs
}

uint32_t IntelCompute::getMaxThreadsPerComputeUnit() {
    return 7;   // Gen12: 7 threads per EU
}

bool IntelCompute::supportsAtomics() {
    return true;  // Gen12 supports atomics
}

bool IntelCompute::supportsImages() {
    return true;  // Gen12 supports images
}

bool IntelCompute::supportsSubgroups() {
    return true;  // Gen12 supports subgroups (SIMD8/16/32)
}

// Command Generation
uint32_t* IntelCompute::buildComputeWalkerCommand(uint32_t* cmd,
                                                 const ComputeDispatchParams* params) {
    uint32_t numGroupsX = calculateWorkGroups(params->globalSizeX, params->localSizeX);
    uint32_t numGroupsY = calculateWorkGroups(params->globalSizeY, params->localSizeY);
    uint32_t numGroupsZ = calculateWorkGroups(params->globalSizeZ, params->localSizeZ);
    
    *cmd++ = GEN12_COMPUTE_WALKER | (15 - 2);
    *cmd++ = 0;  // Indirect data length
    *cmd++ = 0;  // Indirect data start address
    *cmd++ = params->kernel->bindingTableOffset;
    *cmd++ = params->localSizeX | (params->localSizeY << 16);
    *cmd++ = params->localSizeZ;
    *cmd++ = numGroupsX;
    *cmd++ = numGroupsY;
    *cmd++ = numGroupsZ;
    *cmd++ = params->offsetX;
    *cmd++ = params->offsetY;
    *cmd++ = params->offsetZ;
    *cmd++ = 0;  // Right execution mask
    *cmd++ = 0;  // Bottom execution mask
    *cmd++ = 0;  // Reserved
    
    return cmd;
}

uint32_t* IntelCompute::buildMediaInterfaceDescriptor(uint32_t* cmd,
                                                     IntelComputeKernel* kernel) {
    *cmd++ = GEN12_MEDIA_INTERFACE_DESCRIPTOR_LOAD | (4 - 2);
    *cmd++ = 0;  // Reserved
    *cmd++ = sizeof(uint32_t) * 8;  // Interface descriptor length
    *cmd++ = 0;  // Interface descriptor offset
    
    // Interface descriptor data
    *cmd++ = (uint32_t)(kernel->gpuAddress & 0xFFFFFFFF);
    *cmd++ = (uint32_t)(kernel->gpuAddress >> 32);
    *cmd++ = kernel->slmSize / 1024;  // SLM size in KB
    *cmd++ = (kernel->workGroupSizeX * kernel->workGroupSizeY * kernel->workGroupSizeZ);
    *cmd++ = kernel->bindingTableOffset;
    *cmd++ = 0;  // Sampler state offset
    *cmd++ = 0;  // Barrier enable
    *cmd++ = 0;  // Reserved
    
    return cmd;
}

uint32_t* IntelCompute::buildMediaStateFlush(uint32_t* cmd) {
    *cmd++ = GEN12_MEDIA_STATE_FLUSH | (2 - 2);
    *cmd++ = 0;
    return cmd;
}

uint32_t* IntelCompute::buildPipelineSelect(uint32_t* cmd) {
    *cmd++ = GEN12_PIPELINE_SELECT_GPGPU;
    *cmd++ = 0;
    return cmd;
}

uint32_t* IntelCompute::buildComputeModeCommand(uint32_t* cmd) {
    *cmd++ = GEN12_STATE_COMPUTE_MODE | (2 - 2);
    *cmd++ = 0;  // Default compute mode
    return cmd;
}

uint32_t* IntelCompute::buildBarrierCommand(uint32_t* cmd, IntelBarrierType type) {
    // Build memory barrier using PIPE_CONTROL
    *cmd++ = 0x7A000000 | (6 - 2);  // PIPE_CONTROL
    *cmd++ = 0x00100000;  // CS stall
    if (type & BARRIER_GLOBAL) {
        *cmd++ = 0x00000001;  // Flush TLB
    } else {
        *cmd++ = 0;
    }
    *cmd++ = 0;
    *cmd++ = 0;
    *cmd++ = 0;
    return cmd;
}

// Command Submission
ComputeError IntelCompute::submitComputeCommand(uint32_t* commands,
                                               uint32_t numDwords,
                                               IntelRequest** requestOut) {
    if (!computeRing || !computeContext) {
        return COMPUTE_DISPATCH_ERROR;
    }
    
    IntelRequest* request = new IntelRequest();
    if (!request || !request->init()) {
        return COMPUTE_MEMORY_ERROR;
    }
    
    if (!request->initWithContext(computeContext)) {
        request->release();
        return COMPUTE_MEMORY_ERROR;
    }
    
    // Submit to ring buffer
    bool success = computeRing->submitCommand(commands, numDwords, request);
    if (!success) {
        request->release();
        return COMPUTE_DISPATCH_ERROR;
    }
    
    if (requestOut) {
        *requestOut = request;
        request->retain();
    }
    
    request->release();
    return COMPUTE_SUCCESS;
}

ComputeError IntelCompute::waitForCompletion(IntelRequest* request,
                                            uint32_t timeoutMs) {
    if (!request) {
        return COMPUTE_INVALID_PARAMS;
    }
    
    bool completed = request->waitForCompletion(timeoutMs);
    return completed ? COMPUTE_SUCCESS : COMPUTE_TIMEOUT;
}

// Validation
bool IntelCompute::validateKernel(IntelComputeKernel* kernel) {
    return kernel && kernel->kernelObject && kernel->kernelSize > 0;
}

bool IntelCompute::validateBuffer(IntelComputeBuffer* buffer) {
    return buffer && buffer->object && buffer->size > 0;
}

bool IntelCompute::validateImage(IntelComputeImage* image) {
    return image && image->object && image->width > 0 && image->height > 0;
}

bool IntelCompute::validateDispatchParams(const ComputeDispatchParams* params) {
    if (!params || !params->kernel || !params->kernel->compiled) {
        return false;
    }
    
    if (params->globalSizeX == 0 || params->globalSizeY == 0 ||
        params->globalSizeZ == 0) {
        return false;
    }
    
    if (params->localSizeX == 0 || params->localSizeY == 0 ||
        params->localSizeZ == 0) {
        return false;
    }
    
    return validateWorkGroupSize(params->localSizeX, params->localSizeY,
                                params->localSizeZ);
}

bool IntelCompute::validateWorkGroupSize(uint32_t sizeX, uint32_t sizeY,
                                        uint32_t sizeZ) {
    if (sizeX > MAX_WORK_GROUP_SIZE_X ||
        sizeY > MAX_WORK_GROUP_SIZE_Y ||
        sizeZ > MAX_WORK_GROUP_SIZE_Z) {
        return false;
    }
    
    uint32_t totalSize = sizeX * sizeY * sizeZ;
    return totalSize <= MAX_WORK_GROUP_INVOCATIONS;
}

// Helper Functions
uint32_t IntelCompute::calculateWorkGroups(uint32_t globalSize,
                                          uint32_t localSize) {
    return (globalSize + localSize - 1) / localSize;
}

uint32_t IntelCompute::getImageBytesPerPixel(IntelImageFormat format) {
    switch (format) {
        case IMAGE_FORMAT_R8_UINT: return 1;
        case IMAGE_FORMAT_R16_UINT: return 2;
        case IMAGE_FORMAT_R32_UINT:
        case IMAGE_FORMAT_R32_FLOAT: return 4;
        case IMAGE_FORMAT_RG32_UINT:
        case IMAGE_FORMAT_RG32_FLOAT: return 8;
        case IMAGE_FORMAT_RGBA8_UINT: return 4;
        case IMAGE_FORMAT_RGBA16_UINT: return 8;
        case IMAGE_FORMAT_RGBA32_UINT:
        case IMAGE_FORMAT_RGBA32_FLOAT: return 16;
        default: return 4;
    }
}

uint32_t IntelCompute::calculateImagePitch(uint32_t width,
                                          IntelImageFormat format) {
    uint32_t bpp = getImageBytesPerPixel(format);
    uint32_t pitch = width * bpp;
    // Align to 64 bytes
    return (pitch + 63) & ~63;
}

uint32_t IntelCompute::calculateTotalThreads(const ComputeDispatchParams* params) {
    return params->globalSizeX * params->globalSizeY * params->globalSizeZ;
}

// Statistics
void IntelCompute::recordDispatchStart() {
    // Record timestamp for dispatch timing
}

void IntelCompute::recordDispatchComplete(uint32_t workGroups, uint32_t threads) {
    stats.totalDispatches++;
    stats.totalWorkGroups += workGroups;
    stats.totalThreads += threads;
}
