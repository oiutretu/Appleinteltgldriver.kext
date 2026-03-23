/*
 * IntelMetalComputeEncoder.h - Metal Compute Command Encoder
 * Week 44: Compute Pipeline
 * 
 * MTLComputeCommandEncoder for encoding compute dispatch operations.
 * Extracted from IntelMetalCommandBuffer (Week 42) for detailed implementation.
 */

#ifndef IntelMetalComputeEncoder_h
#define IntelMetalComputeEncoder_h

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "IntelMetalComputePipeline.h"

class IntelIOAccelerator;
class IntelMetalCommandBuffer;
class IntelMetalBuffer;
class IntelMetalTexture;
class IntelMetalSamplerState;


// MARK: - Compute Encoder Types


// Barrier scope
typedef enum {
    kMetalBarrierScopeBuffers  = 0x01,
    kMetalBarrierScopeTextures = 0x02,
    kMetalBarrierScopeAll      = 0xFF,
} MetalBarrierScope;

// Dispatch type
typedef enum {
    kMetalDispatchTypeConcurrent      = 0,
    kMetalDispatchTypeSerial          = 1,
    kMetalDispatchTypeIndirect        = 2,
} MetalDispatchType;

// Resource binding
struct ComputeResourceBinding {
    uint32_t index;
    uint32_t offset;
    uint32_t length;
    void*    resource;  // Buffer, Texture, or Sampler
};

// Dispatch configuration
struct ComputeDispatchConfig {
    uint32_t threadgroupsX;
    uint32_t threadgroupsY;
    uint32_t threadgroupsZ;
    
    uint32_t threadsPerGroupX;
    uint32_t threadsPerGroupY;
    uint32_t threadsPerGroupZ;
    
    MetalDispatchType dispatchType;
};

// Compute encoder statistics
struct ComputeEncoderStatistics {
    uint32_t dispatchCount;
    uint32_t barrierCount;
    uint32_t resourceBindings;
    uint32_t totalThreadgroups;
    uint32_t totalThreads;
};


// MARK: - IntelMetalComputeEncoder Class


class IntelMetalComputeEncoder : public OSObject {
    OSDeclareDefaultStructors(IntelMetalComputeEncoder)
    
public:
    // Factory & Lifecycle
    static IntelMetalComputeEncoder* withCommandBuffer(
        IntelMetalCommandBuffer* cmdBuffer);
    
    virtual bool initWithCommandBuffer(
        IntelMetalCommandBuffer* cmdBuffer);
    
    virtual void free() override;
    
    // Encoder State
    bool isActive() const { return active; }
    void endEncoding();
    
    // Pipeline State
    void setComputePipelineState(IntelMetalComputePipeline* pipeline);
    IntelMetalComputePipeline* getComputePipelineState() const { return currentPipeline; }
    
    // Resource Binding
    void setBuffer(IntelMetalBuffer* buffer, uint32_t offset, uint32_t index);
    void setBuffers(IntelMetalBuffer** buffers, uint32_t* offsets, uint32_t start, uint32_t count);
    
    void setTexture(IntelMetalTexture* texture, uint32_t index);
    void setTextures(IntelMetalTexture** textures, uint32_t start, uint32_t count);
    
    void setSamplerState(IntelMetalSamplerState* sampler, uint32_t index);
    void setSamplerStates(IntelMetalSamplerState** samplers, uint32_t start, uint32_t count);
    
    // Threadgroup Memory
    void setThreadgroupMemoryLength(uint32_t length, uint32_t index);
    
    // Dispatch Operations
    void dispatchThreadgroups(
        uint32_t threadgroupsX, uint32_t threadgroupsY, uint32_t threadgroupsZ,
        uint32_t threadsPerGroupX, uint32_t threadsPerGroupY, uint32_t threadsPerGroupZ);
    
    void dispatchThreadgroupsWithIndirectBuffer(
        IntelMetalBuffer* indirectBuffer,
        uint32_t indirectBufferOffset,
        uint32_t threadsPerGroupX, uint32_t threadsPerGroupY, uint32_t threadsPerGroupZ);
    
    void dispatchThreads(
        uint32_t threadsX, uint32_t threadsY, uint32_t threadsZ,
        uint32_t threadsPerGroupX, uint32_t threadsPerGroupY, uint32_t threadsPerGroupZ);
    
    // Synchronization
    void memoryBarrierWithScope(MetalBarrierScope scope);
    void memoryBarrierWithResources(IntelMetalBuffer** buffers, uint32_t bufferCount,
                                   IntelMetalTexture** textures, uint32_t textureCount);
    
    // Statistics
    void getStatistics(ComputeEncoderStatistics* outStats);
    
private:
    IntelIOAccelerator*          accelerator;
    IntelMetalCommandBuffer*     commandBuffer;
    
    // Current pipeline state
    IntelMetalComputePipeline*   currentPipeline;
    
    // Resource bindings
    IntelMetalBuffer*            boundBuffers[31];
    uint32_t                     boundBufferOffsets[31];
    IntelMetalTexture*           boundTextures[31];
    IntelMetalSamplerState*      boundSamplers[16];
    
    // Threadgroup memory
    uint32_t                     threadgroupMemoryLengths[8];
    
    // Statistics
    ComputeEncoderStatistics     stats;
    
    // State
    bool                         initialized;
    bool                         active;
    
    // Internal methods
    IOReturn validateDispatch(const ComputeDispatchConfig* config);
    IOReturn encodeDispatch(const ComputeDispatchConfig* config);
    IOReturn encodeGPGPUWalker(const ComputeDispatchConfig* config);
    IOReturn encodeBarrier(MetalBarrierScope scope);
    IOReturn bindResources();
    void updateStatistics(const ComputeDispatchConfig* config);
};

#endif /* IntelMetalComputeEncoder_h */
