/*
 * IntelMetalRenderTarget.h - Metal Render Target Management
 * Week 43: Rendering Pipeline
 * 
 * MTLRenderPassDescriptor and framebuffer management.
 * Handles color/depth/stencil attachments and MSAA.
 */

#ifndef IntelMetalRenderTarget_h
#define IntelMetalRenderTarget_h

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

class IntelIOAccelerator;
class IntelMetalTexture;


// MARK: - Render Target Types


// Load action
typedef enum {
    kMetalLoadActionDontCare = 0,
    kMetalLoadActionLoad     = 1,
    kMetalLoadActionClear    = 2,
} MetalLoadAction;

// Store action
typedef enum {
    kMetalStoreActionDontCare                   = 0,
    kMetalStoreActionStore                      = 1,
    kMetalStoreActionMultisampleResolve         = 2,
    kMetalStoreActionStoreAndMultisampleResolve = 3,
} MetalStoreAction;

// Attachment descriptor
struct MetalRenderAttachment {
    IntelMetalTexture*  texture;
    uint32_t            level;
    uint32_t            slice;
    uint32_t            depthPlane;
    
    IntelMetalTexture*  resolveTexture;
    uint32_t            resolveLevel;
    uint32_t            resolveSlice;
    uint32_t            resolveDepthPlane;
    
    MetalLoadAction     loadAction;
    MetalStoreAction    storeAction;
    
    // Clear values
    union {
        struct {
            float r, g, b, a;
        } clearColor;
        struct {
            float depth;
            uint32_t stencil;
        } clearDepthStencil;
    };
};

// Render pass descriptor
struct MetalRenderPassDescriptor {
    uint32_t               colorAttachmentCount;
    MetalRenderAttachment  colorAttachments[8];
    MetalRenderAttachment  depthAttachment;
    MetalRenderAttachment  stencilAttachment;
    
    uint32_t               renderTargetWidth;
    uint32_t               renderTargetHeight;
    uint32_t               renderTargetArrayLength;
};

// Framebuffer configuration
struct FramebufferConfiguration {
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t sampleCount;
    uint32_t colorAttachmentCount;
    uint32_t colorFormats[8];
    uint32_t depthFormat;
    uint32_t stencilFormat;
};

// Render target statistics
struct RenderTargetStatistics {
    uint32_t loadOperations;
    uint32_t storeOperations;
    uint32_t clearOperations;
    uint32_t resolveOperations;
    uint32_t pixelsRendered;
};


// MARK: - IntelMetalRenderTarget Class


class IntelMetalRenderTarget : public OSObject {
    OSDeclareDefaultStructors(IntelMetalRenderTarget)
    
public:
    // Factory & Lifecycle
    static IntelMetalRenderTarget* withDescriptor(
        IntelIOAccelerator* accel,
        const MetalRenderPassDescriptor* desc);
    
    virtual bool initWithDescriptor(
        IntelIOAccelerator* accel,
        const MetalRenderPassDescriptor* desc);
    
    virtual void free() override;
    
    // Render Target Information
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }
    uint32_t getLayerCount() const { return layerCount; }
    uint32_t getSampleCount() const { return sampleCount; }
    
    uint32_t getColorAttachmentCount() const { return colorAttachmentCount; }
    const MetalRenderAttachment* getColorAttachment(uint32_t index) const;
    const MetalRenderAttachment* getDepthAttachment() const { return hasDepth ? &depthAttachment : NULL; }
    const MetalRenderAttachment* getStencilAttachment() const { return hasStencil ? &stencilAttachment : NULL; }
    
    bool hasDepthAttachment() const { return hasDepth; }
    bool hasStencilAttachment() const { return hasStencil; }
    
    // GPU Framebuffer
    IOReturn generateFramebuffer();
    const void* getFramebufferState() const { return framebufferState ? framebufferState->getBytesNoCopy() : NULL; }
    uint32_t getFramebufferStateSize() const { return framebufferStateSize; }
    
    // Render Pass Operations
    IOReturn beginRenderPass();
    IOReturn endRenderPass();
    IOReturn clearAttachment(uint32_t index, bool isColor);
    IOReturn resolveAttachment(uint32_t index);
    
    // Statistics
    void getStatistics(RenderTargetStatistics* outStats);
    
private:
    IntelIOAccelerator*          accelerator;
    
    // Render target configuration
    uint32_t                     width;
    uint32_t                     height;
    uint32_t                     layerCount;
    uint32_t                     sampleCount;
    
    // Color attachments
    uint32_t                     colorAttachmentCount;
    MetalRenderAttachment        colorAttachments[8];
    
    // Depth/stencil attachments
    bool                         hasDepth;
    bool                         hasStencil;
    MetalRenderAttachment        depthAttachment;
    MetalRenderAttachment        stencilAttachment;
    
    // GPU framebuffer state
    IOBufferMemoryDescriptor*    framebufferState;
    uint32_t                     framebufferStateSize;
    
    // Statistics
    RenderTargetStatistics       stats;
    
    // State
    bool                         initialized;
    bool                         inRenderPass;
    
    // Internal methods
    IOReturn validateDescriptor(const MetalRenderPassDescriptor* desc);
    IOReturn allocateAttachments();
    IOReturn generateColorAttachmentState(uint32_t index);
    IOReturn generateDepthStencilState();
    IOReturn performLoadAction(const MetalRenderAttachment* attachment, bool isColor);
    IOReturn performStoreAction(const MetalRenderAttachment* attachment, bool isColor);
    IOReturn performClear(const MetalRenderAttachment* attachment, bool isColor);
    IOReturn performResolve(const MetalRenderAttachment* attachment);
};

#endif /* IntelMetalRenderTarget_h */
