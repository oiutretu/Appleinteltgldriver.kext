/*
 * IntelMetalTexture.h - Metal Texture Management
 * Week 45: Resource Management (FINAL)
 * 
 * MTLTexture implementation for Intel GPU texture resources.
 */

#ifndef IntelMetalTexture_h
#define IntelMetalTexture_h

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

class IntelIOAccelerator;


// MARK: - Texture Types


// Texture type
typedef enum {
    kMetalTextureType1D              = 0,
    kMetalTextureType1DArray         = 1,
    kMetalTextureType2D              = 2,
    kMetalTextureType2DArray         = 3,
    kMetalTextureType2DMultisample   = 4,
    kMetalTextureTypeCube            = 5,
    kMetalTextureTypeCubeArray       = 6,
    kMetalTextureType3D              = 7,
} MetalTextureType;

// Texture usage
typedef enum {
    kMetalTextureUsageUnknown        = 0x0000,
    kMetalTextureUsageShaderRead     = 0x0001,
    kMetalTextureUsageShaderWrite    = 0x0002,
    kMetalTextureUsageRenderTarget   = 0x0004,
    kMetalTextureUsagePixelFormatView = 0x0010,
} MetalTextureUsage;

// Storage mode
typedef enum {
    kMetalStorageModeShared   = 0,
    kMetalStorageModeManaged  = 1,
    kMetalStorageModePrivate  = 2,
} MetalStorageMode;

// Pixel format (subset)
typedef enum {
    kMetalPixelFormatInvalid         = 0,
    kMetalPixelFormatRGBA8Unorm      = 70,
    kMetalPixelFormatRGBA8Snorm      = 72,
    kMetalPixelFormatRGBA16Float     = 115,
    kMetalPixelFormatRGBA32Float     = 125,
    kMetalPixelFormatBGRA8Unorm      = 80,
    kMetalPixelFormatDepth32Float    = 252,
    kMetalPixelFormatStencil8        = 253,
} MetalPixelFormat;

// Texture descriptor
struct MetalTextureDescriptor {
    MetalTextureType   textureType;
    MetalPixelFormat   pixelFormat;
    uint32_t           width;
    uint32_t           height;
    uint32_t           depth;
    uint32_t           mipmapLevelCount;
    uint32_t           sampleCount;
    uint32_t           arrayLength;
    MetalStorageMode   storageMode;
    MetalTextureUsage  usage;
};


// MARK: - IntelMetalTexture Class


class IntelMetalTexture : public OSObject {
    OSDeclareDefaultStructors(IntelMetalTexture)
    
public:
    // Factory & Lifecycle
    static IntelMetalTexture* withDescriptor(
        IntelIOAccelerator* accel,
        const MetalTextureDescriptor* desc);
    
    virtual bool initWithDescriptor(
        IntelIOAccelerator* accel,
        const MetalTextureDescriptor* desc);
    
    virtual void free() override;
    
    // Texture Information
    MetalTextureType getTextureType() const { return textureType; }
    MetalPixelFormat getPixelFormat() const { return pixelFormat; }
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }
    uint32_t getDepth() const { return depth; }
    uint32_t getMipmapLevelCount() const { return mipmapLevelCount; }
    uint32_t getSampleCount() const { return sampleCount; }
    uint32_t getArrayLength() const { return arrayLength; }
    MetalStorageMode getStorageMode() const { return storageMode; }
    MetalTextureUsage getUsage() const { return usage; }
    
    // Memory
    IOBufferMemoryDescriptor* getBuffer() const { return buffer; }
    size_t getAllocatedSize() const { return allocatedSize; }
    
    // GPU State
    IOReturn generateSurfaceState();
    const void* getSurfaceState() const { return surfaceState ? surfaceState->getBytesNoCopy() : NULL; }
    uint32_t getSurfaceStateSize() const { return surfaceStateSize; }
    
    // CPU Access
    void* getContents();
    IOReturn replaceRegion(uint32_t level, uint32_t slice,
                          uint32_t x, uint32_t y, uint32_t z,
                          uint32_t width, uint32_t height, uint32_t depth,
                          const void* data, uint32_t bytesPerRow, uint32_t bytesPerImage);
    
private:
    IntelIOAccelerator*           accelerator;
    
    // Texture properties
    MetalTextureType              textureType;
    MetalPixelFormat              pixelFormat;
    uint32_t                      width;
    uint32_t                      height;
    uint32_t                      depth;
    uint32_t                      mipmapLevelCount;
    uint32_t                      sampleCount;
    uint32_t                      arrayLength;
    MetalStorageMode              storageMode;
    MetalTextureUsage             usage;
    
    // Memory
    IOBufferMemoryDescriptor*     buffer;
    size_t                        allocatedSize;
    
    // GPU surface state
    IOBufferMemoryDescriptor*     surfaceState;
    uint32_t                      surfaceStateSize;
    
    // State
    bool                          initialized;
    
    // Internal methods
    IOReturn validateDescriptor(const MetalTextureDescriptor* desc);
    IOReturn allocateMemory();
    size_t calculateSize();
    uint32_t getBytesPerPixel();
};

#endif /* IntelMetalTexture_h */
