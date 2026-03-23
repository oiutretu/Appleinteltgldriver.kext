/*
 * IntelMetalSamplerState.h - Metal Sampler State
 * Week 45: Resource Management (FINAL)
 * 
 * MTLSamplerState implementation for texture sampling configuration.
 */

#ifndef IntelMetalSamplerState_h
#define IntelMetalSamplerState_h

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

class IntelIOAccelerator;


// MARK: - Sampler Types


// Filter mode
typedef enum {
    kMetalSamplerMinMagFilterNearest = 0,
    kMetalSamplerMinMagFilterLinear  = 1,
} MetalSamplerMinMagFilter;

// Mip filter mode
typedef enum {
    kMetalSamplerMipFilterNotMipmapped = 0,
    kMetalSamplerMipFilterNearest      = 1,
    kMetalSamplerMipFilterLinear       = 2,
} MetalSamplerMipFilter;

// Address mode
typedef enum {
    kMetalSamplerAddressModeClampToEdge   = 0,
    kMetalSamplerAddressModeRepeat        = 2,
    kMetalSamplerAddressModeMirrorRepeat  = 3,
    kMetalSamplerAddressModeClampToZero   = 4,
} MetalSamplerAddressMode;

// Compare function
typedef enum {
    kMetalCompareFunctionNever        = 0,
    kMetalCompareFunctionLess         = 1,
    kMetalCompareFunctionEqual        = 2,
    kMetalCompareFunctionLessEqual    = 3,
    kMetalCompareFunctionGreater      = 4,
    kMetalCompareFunctionNotEqual     = 5,
    kMetalCompareFunctionGreaterEqual = 6,
    kMetalCompareFunctionAlways       = 7,
} MetalCompareFunction;

// Sampler descriptor
struct MetalSamplerDescriptor {
    MetalSamplerMinMagFilter minFilter;
    MetalSamplerMinMagFilter magFilter;
    MetalSamplerMipFilter    mipFilter;
    
    MetalSamplerAddressMode  sAddressMode;
    MetalSamplerAddressMode  tAddressMode;
    MetalSamplerAddressMode  rAddressMode;
    
    float                    lodMinClamp;
    float                    lodMaxClamp;
    uint32_t                 maxAnisotropy;
    
    bool                     compareEnabled;
    MetalCompareFunction     compareFunction;
};


// MARK: - IntelMetalSamplerState Class


class IntelMetalSamplerState : public OSObject {
    OSDeclareDefaultStructors(IntelMetalSamplerState)
    
public:
    // Factory & Lifecycle
    static IntelMetalSamplerState* withDescriptor(
        IntelIOAccelerator* accel,
        const MetalSamplerDescriptor* desc);
    
    virtual bool initWithDescriptor(
        IntelIOAccelerator* accel,
        const MetalSamplerDescriptor* desc);
    
    virtual void free() override;
    
    // Sampler Information
    MetalSamplerMinMagFilter getMinFilter() const { return minFilter; }
    MetalSamplerMinMagFilter getMagFilter() const { return magFilter; }
    MetalSamplerMipFilter getMipFilter() const { return mipFilter; }
    MetalSamplerAddressMode getSAddressMode() const { return sAddressMode; }
    MetalSamplerAddressMode getTAddressMode() const { return tAddressMode; }
    MetalSamplerAddressMode getRAddressMode() const { return rAddressMode; }
    
    // GPU State
    IOReturn generateSamplerState();
    const void* getSamplerState() const { return samplerState ? samplerState->getBytesNoCopy() : NULL; }
    uint32_t getSamplerStateSize() const { return samplerStateSize; }
    
private:
    IntelIOAccelerator*          accelerator;
    
    // Sampler configuration
    MetalSamplerMinMagFilter     minFilter;
    MetalSamplerMinMagFilter     magFilter;
    MetalSamplerMipFilter        mipFilter;
    
    MetalSamplerAddressMode      sAddressMode;
    MetalSamplerAddressMode      tAddressMode;
    MetalSamplerAddressMode      rAddressMode;
    
    float                        lodMinClamp;
    float                        lodMaxClamp;
    uint32_t                     maxAnisotropy;
    
    bool                         compareEnabled;
    MetalCompareFunction         compareFunction;
    
    // GPU sampler state
    IOBufferMemoryDescriptor*    samplerState;
    uint32_t                     samplerStateSize;
    
    // State
    bool                         initialized;
};

#endif /* IntelMetalSamplerState_h */
