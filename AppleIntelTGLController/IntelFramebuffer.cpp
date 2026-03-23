/*
 * IntelFramebuffer.cpp
 */

#include "IntelFramebuffer.h"
#include "AppleIntelTGLController.h"
#include "IntelGEM.h"
#include "IntelGEMObject.h"
#include "IntelGTT.h"
#include "IntelUncore.h"
#include "IntelPipe.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelFramebuffer, OSObject)

bool IntelFramebuffer::init(AppleIntelTGLController *ctrl)
{
    if (!super::init()) {
        return false;
    }
    
    controller = ctrl;
    gtt = controller->getGTT();
    attachedPipe = NULL;
    
    memset(&info, 0, sizeof(info));
    
    IOLog("IntelFramebuffer: Initialized\n");
    return true;
}

void IntelFramebuffer::free()
{
    release();
    controller = NULL;
    gtt = NULL;
    
    super::free();
}

bool IntelFramebuffer::allocate(uint32_t width, uint32_t height, uint32_t bpp, enum fb_format format)
{
    // Release existing framebuffer
    if (info.obj) {
        release();
    }
    
    IOLog("IntelFramebuffer: Allocating %ux%u @%ubpp\n", width, height, bpp);
    
    // Calculate size
    uint32_t stride = calculateStride(width, bpp);
    size_t size = stride * height;
    
    // Align to 4KB
    size = (size + 4095) & ~4095;
    
    IOLog("IntelFramebuffer: Size: %zu bytes (stride: %u)\n", size, stride);
    
    // Create GEM object
    IntelGEM *gem = controller->getGEM();
    if (!gem) {
        IOLog("IntelFramebuffer: No GEM manager\n");
        return false;
    }
    
    IntelGEMObject *obj = gem->createObject(size, 0);
    if (!obj) {
        IOLog("IntelFramebuffer: Failed to create GEM object\n");
        return false;
    }
    
    // Allocate GTT space
    if (!gtt) {
        IOLog("IntelFramebuffer: No GTT manager\n");
        return false;
    }
    
    u64 gpuAddr = gtt->allocateSpace(size, 256 * 1024);  // 256KB alignment for scanout
    if (gpuAddr == 0) {
        IOLog("IntelFramebuffer: Failed to allocate GTT space\n");
        gem->destroyObject(obj);
        return false;
    }
    
    obj->setGTTAddress(gpuAddr);
    
    // Bind to GTT
    if (!gtt->bindObject(obj, 0)) {
        IOLog("IntelFramebuffer: Failed to bind framebuffer\n");
        gtt->freeSpace(gpuAddr, size);
        gem->destroyObject(obj);
        return false;
    }
    
    // Map for CPU access
    void *cpuAddr;
    if (!obj->mapCPU(&cpuAddr)) {
        IOLog("IntelFramebuffer: Failed to map framebuffer\n");
        gtt->unbindObject(obj);
        gtt->freeSpace(gpuAddr, size);
        gem->destroyObject(obj);
        return false;
    }
    
    // Fill info
    info.width = width;
    info.height = height;
    info.stride = stride;
    info.bpp = bpp;
    info.format = format;
    info.gpuAddress = gpuAddr;
    info.cpuAddress = cpuAddr;
    info.size = size;
    info.obj = obj;
    
    IOLog("IntelFramebuffer: Allocated at GPU 0x%llx, CPU %p\n", gpuAddr, cpuAddr);
    
    // Clear framebuffer to black
    clear(0x00000000);
    
    return true;
}

void IntelFramebuffer::release()
{
    if (!info.obj) {
        return;
    }
    
    IOLog("IntelFramebuffer: Releasing framebuffer\n");
    
    // Detach from pipe
    if (attachedPipe) {
        detachFromPipe();
    }
    
    // Unmap CPU address
    if (info.cpuAddress) {
        info.obj->unmapCPU();
        info.cpuAddress = NULL;
    }
    
    // Unbind from GTT
    if (gtt && info.gpuAddress) {
        gtt->unbindObject(info.obj);
        gtt->freeSpace(info.gpuAddress, info.size);
        info.gpuAddress = 0;
    }
    
    // Destroy GEM object (through GEM manager)
    IntelGEM *gem = controller->getGEM();
    if (gem) {
        gem->destroyObject(info.obj);
    }
    
    memset(&info, 0, sizeof(info));
    
    IOLog("IntelFramebuffer: Released\n");
}

bool IntelFramebuffer::clear(uint32_t color)
{
    if (!info.cpuAddress) {
        return false;
    }
    
    uint32_t bytesPerPixel = getBytesPerPixel();
    
    if (bytesPerPixel == 4) {
        // Fast path for 32-bit
        uint32_t *ptr = (uint32_t *)info.cpuAddress;
        size_t pixels = (info.stride / 4) * info.height;
        
        for (size_t i = 0; i < pixels; i++) {
            ptr[i] = color;
        }
    } else if (bytesPerPixel == 2) {
        // 16-bit
        uint16_t *ptr = (uint16_t *)info.cpuAddress;
        size_t pixels = (info.stride / 2) * info.height;
        uint16_t color16 = (uint16_t)color;
        
        for (size_t i = 0; i < pixels; i++) {
            ptr[i] = color16;
        }
    } else {
        // Generic byte-by-byte
        memset(info.cpuAddress, color & 0xFF, info.size);
    }
    
    return true;
}

bool IntelFramebuffer::fill(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    if (!info.cpuAddress) {
        return false;
    }
    
    // Clipping
    if (x >= info.width || y >= info.height) {
        return false;
    }
    
    if (x + width > info.width) {
        width = info.width - x;
    }
    
    if (y + height > info.height) {
        height = info.height - y;
    }
    
    uint32_t bytesPerPixel = getBytesPerPixel();
    
    for (uint32_t row = 0; row < height; row++) {
        uint8_t *rowPtr = (uint8_t *)info.cpuAddress +
                         ((y + row) * info.stride) +
                         (x * bytesPerPixel);
        
        if (bytesPerPixel == 4) {
            uint32_t *ptr = (uint32_t *)rowPtr;
            for (uint32_t col = 0; col < width; col++) {
                ptr[col] = color;
            }
        } else if (bytesPerPixel == 2) {
            uint16_t *ptr = (uint16_t *)rowPtr;
            uint16_t color16 = (uint16_t)color;
            for (uint32_t col = 0; col < width; col++) {
                ptr[col] = color16;
            }
        }
    }
    
    return true;
}

bool IntelFramebuffer::blit(const void *src, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!info.cpuAddress || !src) {
        return false;
    }
    
    // Clipping
    if (x >= info.width || y >= info.height) {
        return false;
    }
    
    if (x + width > info.width) {
        width = info.width - x;
    }
    
    if (y + height > info.height) {
        height = info.height - y;
    }
    
    uint32_t bytesPerPixel = getBytesPerPixel();
    uint32_t srcStride = width * bytesPerPixel;
    
    const uint8_t *srcPtr = (const uint8_t *)src;
    
    for (uint32_t row = 0; row < height; row++) {
        uint8_t *dstPtr = (uint8_t *)info.cpuAddress +
                         ((y + row) * info.stride) +
                         (x * bytesPerPixel);
        
        memcpy(dstPtr, srcPtr + (row * srcStride), srcStride);
    }
    
    return true;
}

bool IntelFramebuffer::attachToPipe(IntelPipe *pipe)
{
    if (!pipe || !info.obj) {
        return false;
    }
    
    IOLog("IntelFramebuffer: Attaching to pipe %u\n", pipe->getPipeIndex());
    
    // Program pipe registers with framebuffer address
    uint32_t pipeOffset = pipe->getRegisterOffset();
    IntelUncore *uncore = controller->getUncore();
    
    if (!uncore) {
        return false;
    }
    
    // Primary plane surface address
    uncore->writeRegister32(pipeOffset + 0x7001C, (uint32_t)info.gpuAddress);
    
    // Stride
    uncore->writeRegister32(pipeOffset + 0x70188, info.stride);
    
    // Size
    uint32_t sizeReg = ((info.height - 1) << 16) | (info.width - 1);
    uncore->writeRegister32(pipeOffset + 0x70190, sizeReg);
    
    // Format (assume XRGB8888 for now)
    uint32_t ctlReg = (1 << 31) |  // Enable
                      (6 << 26);    // 32bpp XRGB8888
    uncore->writeRegister32(pipeOffset + 0x70180, ctlReg);
    
    attachedPipe = pipe;
    
    IOLog("IntelFramebuffer: Attached to pipe\n");
    return true;
}

bool IntelFramebuffer::detachFromPipe()
{
    if (!attachedPipe) {
        return true;
    }
    
    IOLog("IntelFramebuffer: Detaching from pipe\n");
    
    // Disable primary plane
    uint32_t pipeOffset = attachedPipe->getRegisterOffset();
    IntelUncore *uncore = controller->getUncore();
    
    if (uncore) {
        uncore->writeRegister32(pipeOffset + 0x70180, 0);
    }
    
    attachedPipe = NULL;
    
    return true;
}

void IntelFramebuffer::getInfo(struct framebuffer_info *out)
{
    if (!out) {
        return;
    }
    
    memcpy(out, &info, sizeof(struct framebuffer_info));
}

void IntelFramebuffer::dumpInfo()
{
    IOLog("IntelFramebuffer Info:\n");
    IOLog("  Resolution: %ux%u\n", info.width, info.height);
    IOLog("  Stride: %u bytes\n", info.stride);
    IOLog("  BPP: %u\n", info.bpp);
    IOLog("  Format: %u\n", info.format);
    IOLog("  Size: %zu bytes (%.2f MB)\n",
          info.size, info.size / (1024.0 * 1024.0));
    IOLog("  GPU Address: 0x%llx\n", info.gpuAddress);
    IOLog("  CPU Address: %p\n", info.cpuAddress);
    IOLog("  Attached to pipe: %s\n", attachedPipe ? "Yes" : "No");
}

uint32_t IntelFramebuffer::getBytesPerPixel() const
{
    return (info.bpp + 7) / 8;
}

uint32_t IntelFramebuffer::calculateStride(uint32_t width, uint32_t bpp)
{
    uint32_t stride = (width * bpp + 7) / 8;
    
    // Align to 64 bytes (cache line)
    stride = (stride + 63) & ~63;
    
    return stride;
}
