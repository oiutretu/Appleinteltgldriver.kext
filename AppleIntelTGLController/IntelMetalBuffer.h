/*
 * IntelMetalBuffer.h - Metal Buffer Management
 * Week 45: Resource Management (FINAL)
 * 
 * MTLBuffer implementation for Intel GPU buffer resources.
 */

#ifndef IntelMetalBuffer_h
#define IntelMetalBuffer_h

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

class IntelIOAccelerator;


// MARK: - Buffer Types


// Storage mode (from texture)
typedef enum {
    kMetalBufferStorageModeShared   = 0,
    kMetalBufferStorageModeManaged  = 1,
    kMetalBufferStorageModePrivate  = 2,
} MetalBufferStorageMode;

// Resource options
typedef enum {
    kMetalResourceCPUCacheModeDefaultCache  = 0x0,
    kMetalResourceCPUCacheModeWriteCombined = 0x1,
    kMetalResourceStorageModeShared         = 0x0 << 4,
    kMetalResourceStorageModeManaged        = 0x1 << 4,
    kMetalResourceStorageModePrivate        = 0x2 << 4,
} MetalResourceOptions;


// MARK: - IntelMetalBuffer Class


class IntelMetalBuffer : public OSObject {
    OSDeclareDefaultStructors(IntelMetalBuffer)
    
public:
    // Factory & Lifecycle
    static IntelMetalBuffer* withLength(
        IntelIOAccelerator* accel,
        size_t length,
        MetalResourceOptions options);
    
    static IntelMetalBuffer* withBytes(
        IntelIOAccelerator* accel,
        const void* bytes,
        size_t length,
        MetalResourceOptions options);
    
    virtual bool initWithLength(
        IntelIOAccelerator* accel,
        size_t length,
        MetalResourceOptions options);
    
    virtual void free() override;
    
    // Buffer Information
    size_t getLength() const { return length; }
    MetalBufferStorageMode getStorageMode() const { return storageMode; }
    MetalResourceOptions getResourceOptions() const { return resourceOptions; }
    
    // CPU Access
    void* getContents();
    IOReturn didModifyRange(size_t offset, size_t length);
    
    // GPU State
    IOReturn generateSurfaceState();
    const void* getSurfaceState() const { return surfaceState ? surfaceState->getBytesNoCopy() : NULL; }
    uint32_t getSurfaceStateSize() const { return surfaceStateSize; }
    
    // Memory
    IOBufferMemoryDescriptor* getBuffer() const { return buffer; }
    
private:
    IntelIOAccelerator*           accelerator;
    
    // Buffer properties
    size_t                        length;
    MetalBufferStorageMode        storageMode;
    MetalResourceOptions          resourceOptions;
    
    // Memory
    IOBufferMemoryDescriptor*     buffer;
    
    // GPU surface state
    IOBufferMemoryDescriptor*     surfaceState;
    uint32_t                      surfaceStateSize;
    
    // State
    bool                          initialized;
    
    // Internal methods
    IOReturn allocateMemory();
};

#endif /* IntelMetalBuffer_h */
