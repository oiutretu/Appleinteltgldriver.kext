/*
 * IntelCompute.h - Compute Shader Support (GPGPU)
 * Week 27 - Phase 6: Compute/GPGPU Acceleration
 *
 * Implements compute shader support for Intel Tiger Lake (Gen12) GPU
 * - Compute pipeline management
 * - Work group scheduling and dispatch
 * - Shared local memory (SLM)
 * - Barrier synchronization
 * - Buffer and image binding
 * - Atomic operations
 */

#ifndef IntelCompute_h
#define IntelCompute_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include "AppleIntelTGLController.h"
#include "IntelRingBuffer.h"
#include "IntelContext.h"
#include "IntelRequest.h"
#include "IntelGEMObject.h"

class AppleIntelTGLController;
class IntelRingBuffer;
class IntelContext;
class IntelRequest;
class IntelGEMObject;

// Forward declarations
struct IntelComputeKernel;
struct IntelComputeBuffer;
struct IntelComputeImage;
struct ComputeDispatchParams;
struct ComputeStats;

// Compute Command Opcodes (Gen12)
#define GEN12_GPGPU_WALKER                   0x7A050000
#define GEN12_COMPUTE_WALKER                 0x7A270000
#define GEN12_STATE_COMPUTE_MODE             0x61020000
#define GEN12_MEDIA_STATE_FLUSH              0x7A040000
#define GEN12_MEDIA_INTERFACE_DESCRIPTOR_LOAD 0x7A020000
#define GEN12_PIPELINE_SELECT_GPGPU          0x69040001

// Work Group Dimensions
#define MAX_WORK_GROUP_SIZE_X    1024
#define MAX_WORK_GROUP_SIZE_Y    1024
#define MAX_WORK_GROUP_SIZE_Z    64
#define MAX_WORK_GROUP_INVOCATIONS 1024

// Shared Local Memory
#define MAX_SLM_SIZE_PER_GROUP   (64 * 1024)  // 64KB per work group
#define MAX_TOTAL_SLM_SIZE       (128 * 1024) // 128KB total

// Memory Types
enum IntelMemoryType {
    MEMORY_TYPE_GLOBAL    = 0,  // Global device memory
    MEMORY_TYPE_CONSTANT  = 1,  // Constant memory (read-only)
    MEMORY_TYPE_LOCAL     = 2,  // Shared local memory (SLM)
    MEMORY_TYPE_PRIVATE   = 3   // Private per-thread memory
};

// Buffer Access Flags
enum IntelBufferAccess {
    BUFFER_READ_ONLY   = 0x01,  // Read-only access
    BUFFER_WRITE_ONLY  = 0x02,  // Write-only access
    BUFFER_READ_WRITE  = 0x03   // Read-write access
};

// Image Formats
enum IntelImageFormat {
    IMAGE_FORMAT_R8_UINT       = 0,   // 8-bit unsigned integer
    IMAGE_FORMAT_R16_UINT      = 1,   // 16-bit unsigned integer
    IMAGE_FORMAT_R32_UINT      = 2,   // 32-bit unsigned integer
    IMAGE_FORMAT_R32_FLOAT     = 3,   // 32-bit float
    IMAGE_FORMAT_RG32_UINT     = 4,   // 2x 32-bit unsigned integer
    IMAGE_FORMAT_RG32_FLOAT    = 5,   // 2x 32-bit float
    IMAGE_FORMAT_RGBA8_UINT    = 6,   // 4x 8-bit unsigned integer
    IMAGE_FORMAT_RGBA16_UINT   = 7,   // 4x 16-bit unsigned integer
    IMAGE_FORMAT_RGBA32_UINT   = 8,   // 4x 32-bit unsigned integer
    IMAGE_FORMAT_RGBA32_FLOAT  = 9    // 4x 32-bit float
};

// Atomic Operations
enum IntelAtomicOp {
    ATOMIC_ADD      = 0,  // Atomic add
    ATOMIC_SUB      = 1,  // Atomic subtract
    ATOMIC_XCHG     = 2,  // Atomic exchange
    ATOMIC_CAS      = 3,  // Compare-and-swap
    ATOMIC_MIN      = 4,  // Atomic minimum
    ATOMIC_MAX      = 5,  // Atomic maximum
    ATOMIC_AND      = 6,  // Atomic bitwise AND
    ATOMIC_OR       = 7,  // Atomic bitwise OR
    ATOMIC_XOR      = 8   // Atomic bitwise XOR
};

// Barrier Types
enum IntelBarrierType {
    BARRIER_LOCAL     = 0x01,  // Work group local barrier
    BARRIER_GLOBAL    = 0x02,  // Global memory barrier
    BARRIER_IMAGE     = 0x04   // Image memory barrier
};

// Compute Error Codes
enum ComputeError {
    COMPUTE_SUCCESS           = 0,  // Success
    COMPUTE_INVALID_PARAMS    = 1,  // Invalid parameters
    COMPUTE_KERNEL_ERROR      = 2,  // Kernel compilation/validation error
    COMPUTE_RESOURCE_ERROR    = 3,  // Resource binding error
    COMPUTE_DISPATCH_ERROR    = 4,  // Dispatch error
    COMPUTE_MEMORY_ERROR      = 5,  // Memory allocation failure
    COMPUTE_TIMEOUT           = 6,  // Operation timeout
    COMPUTE_GPU_HANG          = 7,  // GPU hang detected
    COMPUTE_UNSUPPORTED       = 8   // Unsupported operation
};

// Compute Kernel Structure
struct IntelComputeKernel {
    IntelGEMObject* kernelObject;      // Kernel code GEM object
    uint64_t kernelOffset;             // Kernel offset in object
    uint32_t kernelSize;               // Kernel size in bytes
    
    // Work group configuration
    uint32_t workGroupSizeX;           // Work group size X
    uint32_t workGroupSizeY;           // Work group size Y
    uint32_t workGroupSizeZ;           // Work group size Z
    uint32_t workGroupCount;           // Total work groups
    
    // Memory requirements
    uint32_t slmSize;                  // Shared local memory size
    uint32_t privateMemSize;           // Private memory per thread
    uint32_t constantMemSize;          // Constant memory size
    
    // Resource bindings
    uint32_t numBuffers;               // Number of buffers
    uint32_t numImages;                // Number of images
    uint32_t bindingTableOffset;      // Binding table offset
    
    bool compiled;                     // Compilation status
    uint64_t gpuAddress;               // GPU address of kernel
};

// Compute Buffer Structure
struct IntelComputeBuffer {
    IntelGEMObject* object;            // Buffer data GEM object
    uint64_t size;                     // Buffer size in bytes
    uint64_t gpuAddress;               // GPU address
    IntelBufferAccess accessFlags;     // Access flags (read/write)
    IntelMemoryType memoryType;        // Memory type (global/constant/local)
    bool mapped;                       // CPU mapping status
    void* cpuAddress;                  // CPU-mapped address
};

// Compute Image Structure
struct IntelComputeImage {
    IntelGEMObject* object;            // Image data GEM object
    uint32_t width;                    // Image width
    uint32_t height;                   // Image height
    uint32_t depth;                    // Image depth (1 for 2D)
    IntelImageFormat format;           // Pixel format
    uint64_t gpuAddress;               // GPU address
    uint32_t pitch;                    // Row pitch in bytes
    IntelBufferAccess accessFlags;     // Access flags
};

// Dispatch Parameters
struct ComputeDispatchParams {
    IntelComputeKernel* kernel;        // Kernel to execute
    
    // Global work size (total threads)
    uint32_t globalSizeX;
    uint32_t globalSizeY;
    uint32_t globalSizeZ;
    
    // Local work size (threads per group)
    uint32_t localSizeX;
    uint32_t localSizeY;
    uint32_t localSizeZ;
    
    // Global offset
    uint32_t offsetX;
    uint32_t offsetY;
    uint32_t offsetZ;
    
    // Resource bindings
    IntelComputeBuffer* buffers[16];
    IntelComputeImage* images[8];
    uint32_t numBuffers;
    uint32_t numImages;
    
    // Constant arguments
    void* constantData;
    uint32_t constantDataSize;
};

// Compute Statistics
struct ComputeStats {
    uint64_t totalDispatches;          // Total kernel dispatches
    uint64_t totalWorkGroups;          // Total work groups executed
    uint64_t totalThreads;             // Total threads executed
    uint64_t atomicOperations;         // Atomic operations count
    uint64_t barrierSynchronizations;  // Barrier synchronizations
    uint64_t slmBytesUsed;             // SLM bytes used
    uint64_t globalMemoryReads;        // Global memory reads
    uint64_t globalMemoryWrites;       // Global memory writes
    uint32_t averageDispatchTimeUs;    // Average dispatch time (microseconds)
    uint32_t maxDispatchTimeUs;        // Maximum dispatch time
    uint32_t gpuUtilization;           // GPU utilization percentage
    uint64_t errors;                   // Error count
};

class IntelCompute : public OSObject {
    OSDeclareDefaultStructors(IntelCompute)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    bool initWithController(AppleIntelTGLController* controller);
    
    // Lifecycle
    bool start();
    void stop();
    
    // Kernel Management
    IntelComputeKernel* createKernel(const void* kernelCode, uint32_t kernelSize);
    void destroyKernel(IntelComputeKernel* kernel);
    ComputeError compileKernel(IntelComputeKernel* kernel);
    ComputeError setKernelWorkGroupSize(IntelComputeKernel* kernel,
                                       uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);
    ComputeError setKernelSLMSize(IntelComputeKernel* kernel, uint32_t slmSize);
    
    // Buffer Management
    IntelComputeBuffer* createBuffer(uint64_t size, IntelMemoryType type,
                                    IntelBufferAccess access);
    void destroyBuffer(IntelComputeBuffer* buffer);
    ComputeError writeBuffer(IntelComputeBuffer* buffer, const void* data,
                            uint64_t size, uint64_t offset);
    ComputeError readBuffer(IntelComputeBuffer* buffer, void* data,
                           uint64_t size, uint64_t offset);
    ComputeError mapBuffer(IntelComputeBuffer* buffer, void** cpuAddress);
    ComputeError unmapBuffer(IntelComputeBuffer* buffer);
    
    // Image Management
    IntelComputeImage* createImage(uint32_t width, uint32_t height,
                                  IntelImageFormat format, IntelBufferAccess access);
    void destroyImage(IntelComputeImage* image);
    ComputeError writeImage(IntelComputeImage* image, const void* data);
    ComputeError readImage(IntelComputeImage* image, void* data);
    
    // Kernel Execution
    ComputeError dispatch(const ComputeDispatchParams* params);
    ComputeError dispatchIndirect(IntelComputeKernel* kernel,
                                 IntelComputeBuffer* indirectBuffer,
                                 uint64_t offset);
    
    // Synchronization
    ComputeError memoryBarrier(IntelBarrierType type);
    ComputeError flush();
    ComputeError waitForIdle(uint32_t timeoutMs);
    bool isIdle();
    
    // Atomic Operations (for host-side atomics)
    ComputeError atomicOperation(IntelComputeBuffer* buffer, uint64_t offset,
                                IntelAtomicOp op, uint32_t value,
                                uint32_t* oldValue);
    
    // Statistics
    void getStatistics(ComputeStats* stats);
    void resetStatistics();
    void printStatistics();
    
    // Hardware Capabilities
    uint32_t getMaxWorkGroupSize();
    uint32_t getMaxWorkGroupSizeX();
    uint32_t getMaxWorkGroupSizeY();
    uint32_t getMaxWorkGroupSizeZ();
    uint32_t getMaxSLMSize();
    uint32_t getComputeUnits();
    uint32_t getMaxThreadsPerComputeUnit();
    bool supportsAtomics();
    bool supportsImages();
    bool supportsSubgroups();
    
private:
    AppleIntelTGLController* controller;
    IntelRingBuffer* computeRing;      // Compute/RCS ring buffer
    IntelContext* computeContext;      // Compute context
    ComputeStats stats;                // Statistics
    IORecursiveLock* lock;             // Thread safety
    
    bool initialized;
    bool computeActive;
    
    // Command Generation
    uint32_t* buildComputeWalkerCommand(uint32_t* cmd,
                                       const ComputeDispatchParams* params);
    uint32_t* buildMediaInterfaceDescriptor(uint32_t* cmd,
                                           IntelComputeKernel* kernel);
    uint32_t* buildMediaStateFlush(uint32_t* cmd);
    uint32_t* buildPipelineSelect(uint32_t* cmd);
    uint32_t* buildComputeModeCommand(uint32_t* cmd);
    uint32_t* buildBarrierCommand(uint32_t* cmd, IntelBarrierType type);
    
    // Command Submission
    ComputeError submitComputeCommand(uint32_t* commands, uint32_t numDwords,
                                     IntelRequest** requestOut);
    ComputeError waitForCompletion(IntelRequest* request, uint32_t timeoutMs);
    
    // Validation
    bool validateKernel(IntelComputeKernel* kernel);
    bool validateBuffer(IntelComputeBuffer* buffer);
    bool validateImage(IntelComputeImage* image);
    bool validateDispatchParams(const ComputeDispatchParams* params);
    bool validateWorkGroupSize(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);
    
    // Helper Functions
    uint32_t calculateWorkGroups(uint32_t globalSize, uint32_t localSize);
    uint32_t getImageBytesPerPixel(IntelImageFormat format);
    uint32_t calculateImagePitch(uint32_t width, IntelImageFormat format);
    uint32_t calculateTotalThreads(const ComputeDispatchParams* params);
    
    // Statistics
    void recordDispatchStart();
    void recordDispatchComplete(uint32_t workGroups, uint32_t threads);
};

#endif /* IntelCompute_h */
