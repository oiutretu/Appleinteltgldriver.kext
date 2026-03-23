/*
 * IntelMetalRenderPipeline.h - Metal Render Pipeline State Object
 * Week 43: Rendering Pipeline
 * 
 * MTLRenderPipelineState implementation for Intel GPU.
 * Manages shader binding, rasterization, depth/stencil, and blend state.
 */

#ifndef IntelMetalRenderPipeline_h
#define IntelMetalRenderPipeline_h

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "IntelMetalShader.h"

class IntelIOAccelerator;


// MARK: - Pipeline State Types


// Blend operation
typedef enum {
    kMetalBlendOpAdd             = 0,
    kMetalBlendOpSubtract        = 1,
    kMetalBlendOpReverseSubtract = 2,
    kMetalBlendOpMin             = 3,
    kMetalBlendOpMax             = 4,
} MetalBlendOperation;

// Blend factor
typedef enum {
    kMetalBlendFactorZero                = 0,
    kMetalBlendFactorOne                 = 1,
    kMetalBlendFactorSourceColor         = 2,
    kMetalBlendFactorOneMinusSourceColor = 3,
    kMetalBlendFactorSourceAlpha         = 4,
    kMetalBlendFactorOneMinusSourceAlpha = 5,
    kMetalBlendFactorDestColor           = 6,
    kMetalBlendFactorOneMinusDestColor   = 7,
    kMetalBlendFactorDestAlpha           = 8,
    kMetalBlendFactorOneMinusDestAlpha   = 9,
} MetalBlendFactor;

// Compare function
typedef enum {
    kMetalCompareFuncNever        = 0,
    kMetalCompareFuncLess         = 1,
    kMetalCompareFuncEqual        = 2,
    kMetalCompareFuncLessEqual    = 3,
    kMetalCompareFuncGreater      = 4,
    kMetalCompareFuncNotEqual     = 5,
    kMetalCompareFuncGreaterEqual = 6,
    kMetalCompareFuncAlways       = 7,
} MetalCompareFunction;

// Stencil operation
typedef enum {
    kMetalStencilOpKeep           = 0,
    kMetalStencilOpZero           = 1,
    kMetalStencilOpReplace        = 2,
    kMetalStencilOpIncrementClamp = 3,
    kMetalStencilOpDecrementClamp = 4,
    kMetalStencilOpInvert         = 5,
    kMetalStencilOpIncrementWrap  = 6,
    kMetalStencilOpDecrementWrap  = 7,
} MetalStencilOperation;

// Cull mode
typedef enum {
    kMetalCullModeNone  = 0,
    kMetalCullModeFront = 1,
    kMetalCullModeBack  = 2,
} MetalCullMode;

// Winding order
typedef enum {
    kMetalWindingClockwise        = 0,
    kMetalWindingCounterClockwise = 1,
} MetalWinding;

// Triangle fill mode
typedef enum {
    kMetalTriangleFillModeFill  = 0,
    kMetalTriangleFillModeLines = 1,
} MetalTriangleFillMode;

// Depth clip mode
typedef enum {
    kMetalDepthClipModeClip  = 0,
    kMetalDepthClipModeClamp = 1,
} MetalDepthClipMode;

// Color write mask
typedef enum {
    kMetalColorWriteMaskNone  = 0x0,
    kMetalColorWriteMaskRed   = 0x1,
    kMetalColorWriteMaskGreen = 0x2,
    kMetalColorWriteMaskBlue  = 0x4,
    kMetalColorWriteMaskAlpha = 0x8,
    kMetalColorWriteMaskAll   = 0xF,
} MetalColorWriteMask;


// MARK: - Pipeline State Structures


// Stencil descriptor
struct MetalStencilDescriptor {
    MetalStencilOperation stencilFailOp;
    MetalStencilOperation depthFailOp;
    MetalStencilOperation depthStencilPassOp;
    MetalCompareFunction  stencilCompareFunc;
    uint32_t              readMask;
    uint32_t              writeMask;
};

// Depth/Stencil state
struct MetalDepthStencilState {
    bool                        depthTestEnabled;
    bool                        depthWriteEnabled;
    MetalCompareFunction        depthCompareFunc;
    bool                        stencilTestEnabled;
    MetalStencilDescriptor      frontFaceStencil;
    MetalStencilDescriptor      backFaceStencil;
};

// Blend state for color attachment
struct MetalBlendState {
    bool                  blendingEnabled;
    MetalBlendOperation   rgbBlendOp;
    MetalBlendOperation   alphaBlendOp;
    MetalBlendFactor      sourceRGBBlendFactor;
    MetalBlendFactor      sourceAlphaBlendFactor;
    MetalBlendFactor      destRGBBlendFactor;
    MetalBlendFactor      destAlphaBlendFactor;
    MetalColorWriteMask   writeMask;
};

// Rasterization state
struct MetalRasterizationState {
    MetalCullMode          cullMode;
    MetalWinding           frontFaceWinding;
    MetalTriangleFillMode  triangleFillMode;
    MetalDepthClipMode     depthClipMode;
    float                  depthBias;
    float                  depthSlopeScale;
    float                  depthBiasClamp;
};

// Vertex attribute
struct MetalVertexAttribute {
    uint32_t format;        // MTLVertexFormat
    uint32_t offset;
    uint32_t bufferIndex;
};

// Vertex layout
struct MetalVertexLayout {
    uint32_t              stride;
    uint32_t              stepRate;
    bool                  perInstance;
};

// Pipeline descriptor
struct MetalRenderPipelineDescriptor {
    IntelMetalShader*     vertexShader;
    IntelMetalShader*     fragmentShader;
    
    uint32_t              colorAttachmentCount;
    uint32_t              colorAttachmentFormats[8];  // MTLPixelFormat
    uint32_t              depthAttachmentFormat;
    uint32_t              stencilAttachmentFormat;
    
    uint32_t              sampleCount;
    bool                  alphaToCoverageEnabled;
    bool                  alphaToOneEnabled;
    
    MetalBlendState       colorAttachments[8];
    MetalDepthStencilState depthStencil;
    MetalRasterizationState rasterization;
    
    uint32_t              vertexAttributeCount;
    MetalVertexAttribute  vertexAttributes[32];
    uint32_t              vertexLayoutCount;
    MetalVertexLayout     vertexLayouts[32];
};

// Pipeline statistics
struct PipelineStatistics {
    uint32_t drawCallCount;
    uint32_t verticesProcessed;
    uint32_t primitivesGenerated;
    uint32_t fragmentsGenerated;
    uint32_t pipelineStateChanges;
};


// MARK: - IntelMetalRenderPipeline Class


class IntelMetalRenderPipeline : public OSObject {
    OSDeclareDefaultStructors(IntelMetalRenderPipeline)
    
public:
    // Factory & Lifecycle
    static IntelMetalRenderPipeline* withDescriptor(
        IntelIOAccelerator* accel,
        const MetalRenderPipelineDescriptor* desc);
    
    virtual bool initWithDescriptor(
        IntelIOAccelerator* accel,
        const MetalRenderPipelineDescriptor* desc);
    
    virtual void free() override;
    
    // Pipeline Information
    IntelMetalShader* getVertexShader() const { return vertexShader; }
    IntelMetalShader* getFragmentShader() const { return fragmentShader; }
    
    uint32_t getColorAttachmentCount() const { return colorAttachmentCount; }
    uint32_t getSampleCount() const { return sampleCount; }
    
    const MetalBlendState* getBlendState(uint32_t index) const;
    const MetalDepthStencilState* getDepthStencilState() const { return &depthStencilState; }
    const MetalRasterizationState* getRasterizationState() const { return &rasterizationState; }
    
    // GPU State
    IOReturn generateGPUState();
    const void* getGPUState() const { return gpuState ? gpuState->getBytesNoCopy() : NULL; }
    uint32_t getGPUStateSize() const { return gpuStateSize; }
    
    // Statistics
    void getStatistics(PipelineStatistics* outStats);
    void recordDrawCall(uint32_t vertexCount, uint32_t instanceCount);
    
private:
    IntelIOAccelerator*          accelerator;
    
    // Shaders
    IntelMetalShader*            vertexShader;
    IntelMetalShader*            fragmentShader;
    
    // Render target configuration
    uint32_t                     colorAttachmentCount;
    uint32_t                     colorAttachmentFormats[8];
    uint32_t                     depthAttachmentFormat;
    uint32_t                     stencilAttachmentFormat;
    uint32_t                     sampleCount;
    bool                         alphaToCoverageEnabled;
    bool                         alphaToOneEnabled;
    
    // Pipeline state
    MetalBlendState              blendStates[8];
    MetalDepthStencilState       depthStencilState;
    MetalRasterizationState      rasterizationState;
    
    // Vertex configuration
    uint32_t                     vertexAttributeCount;
    MetalVertexAttribute         vertexAttributes[32];
    uint32_t                     vertexLayoutCount;
    MetalVertexLayout            vertexLayouts[32];
    
    // GPU state buffer
    IOBufferMemoryDescriptor*    gpuState;
    uint32_t                     gpuStateSize;
    
    // Statistics
    PipelineStatistics           stats;
    
    // State
    bool                         initialized;
    bool                         compiled;
    
    // Internal methods
    IOReturn validateDescriptor(const MetalRenderPipelineDescriptor* desc);
    IOReturn compileShaders();
    IOReturn generateVertexFetchState();
    IOReturn generateFragmentState();
    IOReturn generateBlendState();
    IOReturn generateDepthStencilState();
    IOReturn generateRasterizationState();
};

#endif /* IntelMetalRenderPipeline_h */
