/*
 * IntelMetalComputePipeline.h - Metal Compute Pipeline State
 * Week 44: Compute Pipeline
 * 
 * MTLComputePipelineState implementation for Intel GPU GPGPU execution.
 * Manages compute shader binding and thread group configuration.
 */

#ifndef IntelMetalComputePipeline_h
#define IntelMetalComputePipeline_h

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "IntelMetalShader.h"

class IntelIOAccelerator;


// MARK: - Compute Pipeline Types


// Thread execution width
typedef enum {
    kMetalThreadExecutionWidth8  = 8,
    kMetalThreadExecutionWidth16 = 16,
    kMetalThreadExecutionWidth32 = 32,
} MetalThreadExecutionWidth;

// Compute function descriptor
struct MetalComputeFunctionDescriptor {
    IntelMetalShader*  computeShader;
    const char*        functionName;
    
    // Thread group limits
    uint32_t maxThreadsPerThreadgroup;
    uint32_t threadgroupMemoryLength;
    
    // Execution configuration
    MetalThreadExecutionWidth threadExecutionWidth;
    bool supportIndirectDispatch;
};

// Thread group size
struct MetalThreadgroupSize {
    uint32_t width;   // X dimension
    uint32_t height;  // Y dimension
    uint32_t depth;   // Z dimension
};

// Compute pipeline descriptor
struct MetalComputePipelineDescriptor {
    IntelMetalShader*     computeShader;
    const char*           label;
    
    // Thread group configuration
    uint32_t              maxTotalThreadsPerThreadgroup;
    uint32_t              threadgroupMemoryLength;
    MetalThreadExecutionWidth threadExecutionWidth;
    
    // Feature support
    bool                  supportIndirectCommandBuffers;
    bool                  enableBarriers;
};

// Compute pipeline statistics
struct ComputePipelineStatistics {
    uint32_t dispatchCount;
    uint32_t threadgroupsDispatched;
    uint32_t threadsExecuted;
    uint32_t barrierCount;
    uint32_t localMemoryUsage;
};


// MARK: - IntelMetalComputePipeline Class


class IntelMetalComputePipeline : public OSObject {
    OSDeclareDefaultStructors(IntelMetalComputePipeline)
    
public:
    // Factory & Lifecycle
    static IntelMetalComputePipeline* withDescriptor(
        IntelIOAccelerator* accel,
        const MetalComputePipelineDescriptor* desc);
    
    static IntelMetalComputePipeline* withFunction(
        IntelIOAccelerator* accel,
        const MetalComputeFunctionDescriptor* desc);
    
    virtual bool initWithDescriptor(
        IntelIOAccelerator* accel,
        const MetalComputePipelineDescriptor* desc);
    
    virtual void free() override;
    
    // Pipeline Information
    IntelMetalShader* getComputeShader() const { return computeShader; }
    const char* getLabel() const { return label; }
    
    uint32_t getMaxTotalThreadsPerThreadgroup() const { return maxThreadsPerThreadgroup; }
    uint32_t getThreadgroupMemoryLength() const { return threadgroupMemoryLength; }
    MetalThreadExecutionWidth getThreadExecutionWidth() const { return threadExecutionWidth; }
    
    bool supportsIndirectCommandBuffers() const { return supportIndirect; }
    bool supportBarriers() const { return enableBarriers; }
    
    // GPU State
    IOReturn generateGPUState();
    const void* getGPUState() const { return gpuState ? gpuState->getBytesNoCopy() : NULL; }
    uint32_t getGPUStateSize() const { return gpuStateSize; }
    
    // Statistics
    void getStatistics(ComputePipelineStatistics* outStats);
    void recordDispatch(uint32_t threadgroupsX, uint32_t threadgroupsY, uint32_t threadgroupsZ,
                       uint32_t threadsPerGroupX, uint32_t threadsPerGroupY, uint32_t threadsPerGroupZ);
    void recordBarrier();
    
private:
    IntelIOAccelerator*          accelerator;
    
    // Compute shader
    IntelMetalShader*            computeShader;
    char                         label[64];
    
    // Thread group configuration
    uint32_t                     maxThreadsPerThreadgroup;
    uint32_t                     threadgroupMemoryLength;
    MetalThreadExecutionWidth    threadExecutionWidth;
    
    // Feature support
    bool                         supportIndirect;
    bool                         enableBarriers;
    
    // GPU state buffer
    IOBufferMemoryDescriptor*    gpuState;
    uint32_t                     gpuStateSize;
    
    // Statistics
    ComputePipelineStatistics    stats;
    
    // State
    bool                         initialized;
    bool                         compiled;
    
    // Internal methods
    IOReturn validateDescriptor(const MetalComputePipelineDescriptor* desc);
    IOReturn compileShader();
    IOReturn generateMediaVFEState();
    IOReturn generateInterfaceDescriptor();
    IOReturn generateThreadGroupConfig();
};

#endif /* IntelMetalComputePipeline_h */
