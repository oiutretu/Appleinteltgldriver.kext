/*
 * IntelFramebuffer.h
 *
 * Framebuffer management for display output
 * Manages scanout buffers and IOFramebuffer integration
 */

#ifndef INTEL_FRAMEBUFFER_H
#define INTEL_FRAMEBUFFER_H

#include <IOKit/graphics/IOFramebuffer.h>
#include "linux_compat.h"

class AppleIntelTGLController;
class IntelGEMObject;
class IntelGTT;
class IntelPipe;

/* Framebuffer formats */
enum fb_format {
    FB_FORMAT_XRGB8888 = 0,  // 32-bit RGB (8:8:8)
    FB_FORMAT_RGB565,         // 16-bit RGB (5:6:5)
    FB_FORMAT_XBGR8888,       // 32-bit BGR (8:8:8)
    FB_FORMAT_ARGB8888,       // 32-bit ARGB (8:8:8:8)
};

/* Framebuffer info */
struct framebuffer_info {
    uint32_t width;
    uint32_t height;
    uint32_t stride;          // Bytes per row
    uint32_t bpp;             // Bits per pixel
    enum fb_format format;
    
    u64 gpuAddress;           // GPU address
    void *cpuAddress;         // CPU-accessible pointer
    size_t size;              // Total size in bytes
    
    IntelGEMObject *obj;      // Backing GEM object
};

class IntelFramebuffer : public OSObject {
    OSDeclareDefaultStructors(IntelFramebuffer)
    
public:
    // Initialization
    bool init(AppleIntelTGLController *ctrl);
    virtual void free() override;
    
    // Framebuffer allocation
    bool allocate(uint32_t width, uint32_t height, uint32_t bpp, enum fb_format format);
    void release();
    
    // Access
    void* getCPUAddress() const { return info.cpuAddress; }
    u64 getGPUAddress() const { return info.gpuAddress; }
    uint32_t getWidth() const { return info.width; }
    uint32_t getHeight() const { return info.height; }
    uint32_t getStride() const { return info.stride; }
    uint32_t getBPP() const { return info.bpp; }
    size_t getSize() const { return info.size; }
    
    // Operations
    bool clear(uint32_t color);
    bool fill(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
    bool blit(const void *src, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    
    // Pipe attachment
    bool attachToPipe(IntelPipe *pipe);
    bool detachFromPipe();
    
    // Info
    void getInfo(struct framebuffer_info *out);
    void dumpInfo();
    
private:
    AppleIntelTGLController *controller;
    IntelGTT *gtt;
    
    struct framebuffer_info info;
    IntelPipe *attachedPipe;
    
    // Helper
    uint32_t getBytesPerPixel() const;
    uint32_t calculateStride(uint32_t width, uint32_t bpp);
};

#endif // INTEL_FRAMEBUFFER_H
