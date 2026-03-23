/*
 * IntelMetalCommandBuffer.h - Metal Command Buffer Interface
 * Week 42: Metal Commands - Full Implementation
 * 
 * Complete MTLCommandBuffer implementation with full command encoding,
 * resource management, and GPU submission.
 */

#ifndef IntelMetalCommandBuffer_h
#define IntelMetalCommandBuffer_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

class IntelMetalCommandQueue;
class IntelMetalCommandTranslator;
class IntelIOAccelerator;


// MARK: - Command Buffer Status


typedef enum {
    kMetalCommandBufferStatusNotEnqueued = 0,  // Created but not enqueued
    kMetalCommandBufferStatusEnqueued    = 1,  // Enqueued but not committed
    kMetalCommandBufferStatusCommitted   = 2,  // Committed for execution
    kMetalCommandBufferStatusScheduled   = 3,  // Scheduled on GPU
    kMetalCommandBufferStatusCompleted   = 4,  // Execution completed
    kMetalCommandBufferStatusError       = 5,  // Execution failed
} MetalCommandBufferStatus;


// MARK: - Command Encoder Types


typedef enum {
    kMetalCommandEncoderTypeNone     = 0,
    kMetalCommandEncoderTypeRender   = 1,  // Render pass encoding
    kMetalCommandEncoderTypeCompute  = 2,  // Compute kernel encoding
    kMetalCommandEncoderTypeBlit     = 3,  // Blit/copy operations
    kMetalCommandEncoderTypeParallel = 4,  // Parallel render encoding
} MetalCommandEncoderType;


// MARK: - Command Types


typedef enum {
    // Render commands
    kMetalCommandTypeDraw                    = 0x1000,
    kMetalCommandTypeDrawIndexed             = 0x1001,
    kMetalCommandTypeDrawIndirect            = 0x1002,
    kMetalCommandTypeDrawIndexedIndirect     = 0x1003,
    kMetalCommandTypeSetVertexBuffer         = 0x1010,
    kMetalCommandTypeSetVertexTexture        = 0x1011,
    kMetalCommandTypeSetVertexSamplerState   = 0x1012,
    kMetalCommandTypeSetFragmentBuffer       = 0x1020,
    kMetalCommandTypeSetFragmentTexture      = 0x1021,
    kMetalCommandTypeSetFragmentSamplerState = 0x1022,
    kMetalCommandTypeSetRenderPipelineState  = 0x1030,
    kMetalCommandTypeSetViewport             = 0x1040,
    kMetalCommandTypeSetScissorRect          = 0x1041,
    kMetalCommandTypeSetDepthStencilState    = 0x1042,
    kMetalCommandTypeSetBlendColor           = 0x1043,
    
    // Compute commands
    kMetalCommandTypeDispatch                = 0x2000,
    kMetalCommandTypeDispatchIndirect        = 0x2001,
    kMetalCommandTypeSetComputePipelineState = 0x2010,
    kMetalCommandTypeSetComputeBuffer        = 0x2020,
    kMetalCommandTypeSetComputeTexture       = 0x2021,
    kMetalCommandTypeSetComputeSamplerState  = 0x2022,
    kMetalCommandTypeSetThreadgroupMemory    = 0x2030,
    
    // Blit commands
    kMetalCommandTypeCopyBufferToBuffer      = 0x3000,
    kMetalCommandTypeCopyBufferToTexture     = 0x3001,
    kMetalCommandTypeCopyTextureToBuffer     = 0x3002,
    kMetalCommandTypeCopyTextureToTexture    = 0x3003,
    kMetalCommandTypeFillBuffer              = 0x3010,
    kMetalCommandTypeClearTexture            = 0x3011,
    kMetalCommandTypeGenerateMipmaps         = 0x3020,
    kMetalCommandTypeSynchronize             = 0x3030,
    
    // Synchronization commands
    kMetalCommandTypeWaitForFence            = 0x4000,
    kMetalCommandTypeUpdateFence             = 0x4001,
    kMetalCommandTypeMemoryBarrier           = 0x4010,
} MetalCommandType;


// MARK: - Primitive Types


typedef enum {
    kMetalPrimitiveTypePoint        = 0,
    kMetalPrimitiveTypeLine         = 1,
    kMetalPrimitiveTypeLineStrip    = 2,
    kMetalPrimitiveTypeTriangle     = 3,
    kMetalPrimitiveTypeTriangleStrip = 4,
} MetalPrimitiveType;

typedef enum {
    kMetalIndexTypeUInt16 = 0,
    kMetalIndexTypeUInt32 = 1,
} MetalIndexType;


// MARK: - Command Structures


// Command header (present in all commands)
struct MetalCommandHeader {
    uint32_t commandType;       // MetalCommandType
    uint32_t commandSize;       // Size of command data (excluding header)
    uint32_t encoderType;       // MetalCommandEncoderType
    uint32_t sequenceNumber;    // For ordering
} __attribute__((packed));

// Draw command
struct MetalDrawCommand {
    uint32_t primitiveType;     // MetalPrimitiveType
    uint32_t vertexStart;       // First vertex
    uint32_t vertexCount;       // Number of vertices
    uint32_t instanceCount;     // Number of instances
    uint32_t baseInstance;      // First instance
} __attribute__((packed));

// Draw indexed command
struct MetalDrawIndexedCommand {
    uint32_t primitiveType;     // MetalPrimitiveType
    uint32_t indexCount;        // Number of indices
    uint32_t indexType;         // MetalIndexType
    uint64_t indexBufferOffset; // Offset in index buffer
    uint32_t instanceCount;     // Number of instances
    uint32_t baseVertex;        // Added to each index
    uint32_t baseInstance;      // First instance
} __attribute__((packed));

// Set buffer command
struct MetalSetBufferCommand {
    uint64_t bufferAddress;     // GPU address or handle
    uint64_t offset;            // Offset into buffer
    uint64_t length;            // Length of buffer
    uint32_t index;             // Binding index
    uint32_t shaderStage;       // Vertex/Fragment/Compute
} __attribute__((packed));

// Set texture command
struct MetalSetTextureCommand {
    uint64_t textureHandle;     // Texture handle
    uint32_t index;             // Binding index
    uint32_t shaderStage;       // Vertex/Fragment/Compute
    uint32_t mipLevel;          // Base mip level
    uint32_t arraySlice;        // Base array slice
} __attribute__((packed));

// Set sampler command
struct MetalSetSamplerCommand {
    uint64_t samplerHandle;     // Sampler state handle
    uint32_t index;             // Binding index
    uint32_t shaderStage;       // Vertex/Fragment/Compute
    uint32_t lodMinClamp;       // Min LOD clamp
    uint32_t lodMaxClamp;       // Max LOD clamp
} __attribute__((packed));

// Set pipeline state command
struct MetalSetPipelineStateCommand {
    uint64_t pipelineHandle;    // Pipeline state handle
    uint32_t pipelineType;      // Render/Compute
    uint32_t reserved;
} __attribute__((packed));

// Viewport command
struct MetalSetViewportCommand {
    float originX;
    float originY;
    float width;
    float height;
    float znear;
    float zfar;
} __attribute__((packed));

// Scissor rect command
struct MetalSetScissorRectCommand {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

// Dispatch command (compute)
struct MetalDispatchCommand {
    uint32_t threadgroupsX;     // Thread groups in X
    uint32_t threadgroupsY;     // Thread groups in Y
    uint32_t threadgroupsZ;     // Thread groups in Z
    uint32_t threadsPerGroupX;  // Threads per group in X
    uint32_t threadsPerGroupY;  // Threads per group in Y
    uint32_t threadsPerGroupZ;  // Threads per group in Z
} __attribute__((packed));

// Copy buffer command
struct MetalCopyBufferCommand {
    uint64_t sourceBuffer;      // Source buffer handle
    uint64_t sourceOffset;      // Source offset
    uint64_t destinationBuffer; // Dest buffer handle
    uint64_t destinationOffset; // Dest offset
    uint64_t size;              // Bytes to copy
} __attribute__((packed));

// Copy texture command
struct MetalCopyTextureCommand {
    uint64_t sourceTexture;     // Source texture handle
    uint32_t sourceSlice;       // Source array slice
    uint32_t sourceLevel;       // Source mip level
    uint32_t sourceX;           // Source origin X
    uint32_t sourceY;           // Source origin Y
    uint32_t sourceZ;           // Source origin Z
    uint64_t destinationTexture; // Dest texture handle
    uint32_t destinationSlice;  // Dest array slice
    uint32_t destinationLevel;  // Dest mip level
    uint32_t destinationX;      // Dest origin X
    uint32_t destinationY;      // Dest origin Y
    uint32_t destinationZ;      // Dest origin Z
    uint32_t width;             // Copy width
    uint32_t height;            // Copy height
    uint32_t depth;             // Copy depth
} __attribute__((packed));

// Fill buffer command
struct MetalFillBufferCommand {
    uint64_t buffer;            // Buffer handle
    uint64_t offset;            // Offset in buffer
    uint64_t size;              // Size to fill
    uint8_t pattern;            // Fill pattern byte
    uint8_t reserved[7];
} __attribute__((packed));


// MARK: - Resource Binding


#define kMaxVertexBuffers     31
#define kMaxFragmentBuffers   31
#define kMaxComputeBuffers    31
#define kMaxVertexTextures    31
#define kMaxFragmentTextures  31
#define kMaxComputeTextures   31
#define kMaxSamplerStates     16

struct MetalResourceBindings {
    // Vertex stage
    uint64_t vertexBuffers[kMaxVertexBuffers];
    uint64_t vertexTextures[kMaxVertexTextures];
    uint64_t vertexSamplers[kMaxSamplerStates];
    uint32_t vertexBufferCount;
    uint32_t vertexTextureCount;
    uint32_t vertexSamplerCount;
    
    // Fragment stage
    uint64_t fragmentBuffers[kMaxFragmentBuffers];
    uint64_t fragmentTextures[kMaxFragmentTextures];
    uint64_t fragmentSamplers[kMaxSamplerStates];
    uint32_t fragmentBufferCount;
    uint32_t fragmentTextureCount;
    uint32_t fragmentSamplerCount;
    
    // Compute stage
    uint64_t computeBuffers[kMaxComputeBuffers];
    uint64_t computeTextures[kMaxComputeTextures];
    uint64_t computeSamplers[kMaxSamplerStates];
    uint32_t computeBufferCount;
    uint32_t computeTextureCount;
    uint32_t computeSamplerCount;
};


// MARK: - Command Buffer Configuration


#define kCommandBufferInitialCapacity  (64 * 1024)   // 64KB
#define kCommandBufferMaxCapacity      (1024 * 1024) // 1MB
#define kCommandBufferGrowthFactor     2


// MARK: - IntelMetalCommandBuffer Class


class IntelMetalCommandBuffer : public OSObject {
    OSDeclareDefaultStructors(IntelMetalCommandBuffer)
    
public:

    // MARK: - Factory & Lifecycle

    
    static IntelMetalCommandBuffer* withCommandQueue(IntelMetalCommandQueue* queue);
    
    virtual bool initWithCommandQueue(IntelMetalCommandQueue* queue);
    virtual void free() override;
    

    // MARK: - Command Encoding - Render

    
    // Begin/end render encoding
    IOReturn beginRenderEncoder();
    IOReturn endRenderEncoder();
    
    // Draw commands
    IOReturn draw(uint32_t primitiveType,
                 uint32_t vertexStart,
                 uint32_t vertexCount,
                 uint32_t instanceCount);
    
    IOReturn drawIndexed(uint32_t primitiveType,
                        uint32_t indexCount,
                        uint32_t indexType,
                        uint64_t indexBufferOffset,
                        uint32_t instanceCount);
    
    // Resource binding - Vertex
    IOReturn setVertexBuffer(uint64_t buffer, uint64_t offset, uint32_t index);
    IOReturn setVertexTexture(uint64_t texture, uint32_t index);
    IOReturn setVertexSamplerState(uint64_t sampler, uint32_t index);
    
    // Resource binding - Fragment
    IOReturn setFragmentBuffer(uint64_t buffer, uint64_t offset, uint32_t index);
    IOReturn setFragmentTexture(uint64_t texture, uint32_t index);
    IOReturn setFragmentSamplerState(uint64_t sampler, uint32_t index);
    
    // Render state
    IOReturn setRenderPipelineState(uint64_t pipeline);
    IOReturn setViewport(float x, float y, float w, float h, float zn, float zf);
    IOReturn setScissorRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    

    // MARK: - Command Encoding - Compute

    
    // Begin/end compute encoding
    IOReturn beginComputeEncoder();
    IOReturn endComputeEncoder();
    
    // Dispatch commands
    IOReturn dispatch(uint32_t threadgroupsX,
                     uint32_t threadgroupsY,
                     uint32_t threadgroupsZ);
    
    IOReturn dispatchWithThreadsPerGroup(uint32_t threadsX,
                                        uint32_t threadsY,
                                        uint32_t threadsZ,
                                        uint32_t threadsPerGroupX,
                                        uint32_t threadsPerGroupY,
                                        uint32_t threadsPerGroupZ);
    
    // Resource binding - Compute
    IOReturn setComputeBuffer(uint64_t buffer, uint64_t offset, uint32_t index);
    IOReturn setComputeTexture(uint64_t texture, uint32_t index);
    IOReturn setComputeSamplerState(uint64_t sampler, uint32_t index);
    
    // Compute state
    IOReturn setComputePipelineState(uint64_t pipeline);
    IOReturn setThreadgroupMemoryLength(uint64_t length, uint32_t index);
    

    // MARK: - Command Encoding - Blit

    
    // Begin/end blit encoding
    IOReturn beginBlitEncoder();
    IOReturn endBlitEncoder();
    
    // Copy commands
    IOReturn copyBufferToBuffer(uint64_t srcBuffer, uint64_t srcOffset,
                               uint64_t dstBuffer, uint64_t dstOffset,
                               uint64_t size);
    
    IOReturn copyTextureToTexture(uint64_t srcTexture, uint32_t srcSlice, uint32_t srcLevel,
                                 uint64_t dstTexture, uint32_t dstSlice, uint32_t dstLevel,
                                 uint32_t width, uint32_t height, uint32_t depth);
    
    // Fill commands
    IOReturn fillBuffer(uint64_t buffer, uint64_t offset, uint64_t size, uint8_t pattern);
    

    // MARK: - Command Buffer Control

    
    // Enqueue for execution (renamed to avoid kernel macro conflict)
    IOReturn enqueueCommandBuffer();
    
    // Commit for execution
    IOReturn commit();
    
    // Submit to GPU (internal)
    IOReturn submitToGPU();
    
    // Wait for completion
    IOReturn waitUntilCompleted(uint64_t timeoutNs);
    
    // Completion handlers
    typedef void (*CompletionHandler)(void* context, IOReturn status);
    IOReturn addCompletedHandler(CompletionHandler handler, void* context);
    

    // MARK: - Status & Properties

    
    MetalCommandBufferStatus getStatus() const { return status; }
    bool isCompleted() const { return status >= kMetalCommandBufferStatusCompleted; }
    
    IntelMetalCommandQueue* getCommandQueue() const { return commandQueue; }
    IntelIOAccelerator* getAccelerator() const { return accelerator; }
    
    uint32_t getCommandCount() const { return commandCount; }
    uint64_t getGPUStartTime() const { return gpuStartTime; }
    uint64_t getGPUEndTime() const { return gpuEndTime; }
    

    // MARK: - Internal Accessors

    
    void* getCommandData() const;
    uint32_t getCommandDataSize() const { return commandDataOffset; }
    
    const MetalResourceBindings* getResourceBindings() const { return &resourceBindings; }
    
private:

    // MARK: - Internal Methods

    
    // Command buffer management
    IOReturn ensureCapacity(uint32_t additionalSize);
    IOReturn appendCommand(MetalCommandType type, const void* commandData, uint32_t size);
    
    // Completion handling
    void markCompleted(IOReturn completionStatus);
    void invokeCompletionHandlers();
    
    // Validation
    bool validateEncoderState(MetalCommandEncoderType requiredType);
    

    // MARK: - Member Variables

    
    // Core references
    IntelMetalCommandQueue* commandQueue;
    IntelMetalCommandTranslator* translator;
    IntelIOAccelerator* accelerator;
    
    // Command buffer storage
    IOBufferMemoryDescriptor* commandDataMemory;
    uint32_t commandDataCapacity;
    uint32_t commandDataOffset;
    uint32_t commandCount;
    uint32_t sequenceCounter;
    
    // Encoder state
    MetalCommandEncoderType currentEncoderType;
    uint32_t encoderCommandCount;
    
    // Resource bindings
    MetalResourceBindings resourceBindings;
    
    // Status
    MetalCommandBufferStatus status;
    IOReturn completionStatus;
    
    // Timing
    uint64_t createTime;
    uint64_t enqueueTime;
    uint64_t commitTime;
    uint64_t gpuStartTime;
    uint64_t gpuEndTime;
    
    // Completion handlers
    struct CompletionHandlerEntry {
        CompletionHandler handler;
        void* context;
    };
    OSArray* completionHandlers;
    IOLock* completionLock;
    
    // State
    bool initialized;
};

#endif /* IntelMetalCommandBuffer_h */
