/*
 * IntelVideoPostProcessing.cpp
 * Intel Graphics Driver
 *
 * Video post-processing engine (VPP Box) implementation.
 * Hardware-accelerated video enhancement and transformation.
 *
 * Week 32 - Phase 7: Video Engine (Final Week)
 */

#include "IntelVideoPostProcessing.h"
#include <IOKit/IOLib.h>

#define super OSObject

OSDefineMetaClassAndStructors(IntelVideoPostProcessing, OSObject)


// Initialization & Lifecycle


bool IntelVideoPostProcessing::init() {
    if (!super::init()) {
        return false;
    }
    
    m_controller = nullptr;
    m_ringBuffer = nullptr;
    m_running = false;
    m_lastError = VPP_SUCCESS;
    m_tempBuffer = nullptr;
    m_layerBuffer = nullptr;
    
    // Initialize statistics
    memset(&m_stats, 0, sizeof(VPPStatistics));
    m_lastProcessTime = 0;
    
    // Create locks
    m_statsLock = IOLockAlloc();
    if (!m_statsLock) {
        return false;
    }
    
    // Initialize capabilities
    memset(&m_capabilities, 0, sizeof(VPPCapabilities));
    
    IOLog("IntelVideoPostProcessing: Initialized\n");
    return true;
}

void IntelVideoPostProcessing::free() {
    if (m_running) {
        stop();
    }
    
    if (m_statsLock) {
        IOLockFree(m_statsLock);
        m_statsLock = nullptr;
    }
    
    super::free();
}

bool IntelVideoPostProcessing::initWithController(AppleIntelTGLController* controller) {
    if (!controller) {
        IOLog("IntelVideoPostProcessing: Invalid controller\n");
        return false;
    }
    
    m_controller = controller;
    m_ringBuffer = controller->getRingBuffer(RING_VECS);
    
    // Detect hardware capabilities
    detectCapabilities();
    
    IOLog("IntelVideoPostProcessing: Controller initialized\n");
    return true;
}

bool IntelVideoPostProcessing::start() {
    if (m_running) {
        return true;
    }
    
    // Allocate internal buffers
    if (allocateInternalBuffers() != VPP_SUCCESS) {
        IOLog("IntelVideoPostProcessing: Failed to allocate buffers\n");
        return false;
    }
    
    // Enable VPP hardware
    if (!enableHardware()) {
        IOLog("IntelVideoPostProcessing: Failed to enable hardware\n");
        freeInternalBuffers();
        return false;
    }
    
    m_running = true;
    IOLog("IntelVideoPostProcessing: Started (capabilities: scale=%d, color=%d, deint=%d, denoise=%d)\n",
          m_capabilities.scalingSupported, m_capabilities.colorConvertSupported,
          m_capabilities.deinterlaceSupported, m_capabilities.denoiseSupported);
    
    return true;
}

void IntelVideoPostProcessing::stop() {
    if (!m_running) {
        return;
    }
    
    // Wait for pending operations
    flush();
    
    // Disable VPP hardware
    disableHardware();
    
    // Free internal buffers
    freeInternalBuffers();
    
    m_running = false;
    IOLog("IntelVideoPostProcessing: Stopped\n");
}


// Processing Operations


VPPError IntelVideoPostProcessing::process(const VPPParams* params) {
    if (!params) {
        return VPP_ERROR_INVALID_PARAMS;
    }
    
    if (!m_running) {
        IOLog("IntelVideoPostProcessing: Not running\n");
        return VPP_ERROR_HARDWARE_FAULT;
    }
    
    // Validate parameters
    VPPError err = validateParams(params);
    if (err != VPP_SUCCESS) {
        return err;
    }
    
    uint64_t startTime = mach_absolute_time();
    
    // Dispatch to operation-specific handler
    switch (params->operation) {
        case VPP_OP_SCALE:
            err = processScale(params);
            break;
        case VPP_OP_COLOR_CONVERT:
            err = processColorConvert(params);
            break;
        case VPP_OP_DEINTERLACE:
            err = processDeinterlace(params);
            break;
        case VPP_OP_DENOISE:
            err = processDenoise(params);
            break;
        case VPP_OP_SHARPEN:
            err = processSharpen(params);
            break;
        case VPP_OP_COMPOSE:
            err = processCompose(params);
            break;
        case VPP_OP_ROTATE:
        case VPP_OP_MIRROR:
        case VPP_OP_CROP:
            err = processTransform(params);
            break;
        default:
            err = VPP_ERROR_UNSUPPORTED_OPERATION;
            break;
    }
    
    uint64_t endTime = mach_absolute_time();
    updateStatistics(endTime - startTime, err);
    
    m_lastError = err;
    return err;
}

VPPError IntelVideoPostProcessing::scale(IntelGEMObject* input,
                                        IntelGEMObject* output,
                                        const ScaleParams* params) {
    VPPParams vppParams;
    memset(&vppParams, 0, sizeof(VPPParams));
    
    vppParams.operation = VPP_OP_SCALE;
    vppParams.inputSurface = input;
    vppParams.outputSurface = output;
    vppParams.params.scale = *params;
    
    return process(&vppParams);
}

VPPError IntelVideoPostProcessing::colorConvert(IntelGEMObject* input,
                                               IntelGEMObject* output,
                                               const ColorConvertParams* params) {
    VPPParams vppParams;
    memset(&vppParams, 0, sizeof(VPPParams));
    
    vppParams.operation = VPP_OP_COLOR_CONVERT;
    vppParams.inputSurface = input;
    vppParams.outputSurface = output;
    vppParams.params.colorConvert = *params;
    
    return process(&vppParams);
}

VPPError IntelVideoPostProcessing::deinterlace(IntelGEMObject* input,
                                              IntelGEMObject* output,
                                              const DeinterlaceParams* params) {
    VPPParams vppParams;
    memset(&vppParams, 0, sizeof(VPPParams));
    
    vppParams.operation = VPP_OP_DEINTERLACE;
    vppParams.inputSurface = input;
    vppParams.outputSurface = output;
    vppParams.params.deinterlace = *params;
    
    return process(&vppParams);
}

VPPError IntelVideoPostProcessing::denoise(IntelGEMObject* input,
                                          IntelGEMObject* output,
                                          const DenoiseParams* params) {
    VPPParams vppParams;
    memset(&vppParams, 0, sizeof(VPPParams));
    
    vppParams.operation = VPP_OP_DENOISE;
    vppParams.inputSurface = input;
    vppParams.outputSurface = output;
    vppParams.params.denoise = *params;
    
    return process(&vppParams);
}

VPPError IntelVideoPostProcessing::sharpen(IntelGEMObject* input,
                                          IntelGEMObject* output,
                                          const SharpenParams* params) {
    VPPParams vppParams;
    memset(&vppParams, 0, sizeof(VPPParams));
    
    vppParams.operation = VPP_OP_SHARPEN;
    vppParams.inputSurface = input;
    vppParams.outputSurface = output;
    vppParams.params.sharpen = *params;
    
    return process(&vppParams);
}

VPPError IntelVideoPostProcessing::compose(const ComposeParams* params,
                                          IntelGEMObject* output) {
    VPPParams vppParams;
    memset(&vppParams, 0, sizeof(VPPParams));
    
    vppParams.operation = VPP_OP_COMPOSE;
    vppParams.outputSurface = output;
    vppParams.params.compose = *params;
    
    return process(&vppParams);
}

VPPError IntelVideoPostProcessing::transform(IntelGEMObject* input,
                                            IntelGEMObject* output,
                                            const TransformParams* params) {
    VPPParams vppParams;
    memset(&vppParams, 0, sizeof(VPPParams));
    
    if (params->rotation != ROTATE_NONE) {
        vppParams.operation = VPP_OP_ROTATE;
    } else if (params->mirrorHorizontal || params->mirrorVertical) {
        vppParams.operation = VPP_OP_MIRROR;
    } else {
        return VPP_ERROR_INVALID_PARAMS;
    }
    
    vppParams.inputSurface = input;
    vppParams.outputSurface = output;
    vppParams.params.transform = *params;
    
    return process(&vppParams);
}

void IntelVideoPostProcessing::waitForCompletion(uint32_t timeoutMs) {
    if (!m_running) {
        return;
    }
    
    uint64_t startTime = mach_absolute_time();
    uint64_t timeoutNs = timeoutMs * 1000000ULL;
    
    while (!isHardwareIdle()) {
        IOSleep(1);
        
        uint64_t elapsed = mach_absolute_time() - startTime;
        if (elapsed > timeoutNs) {
            IOLog("IntelVideoPostProcessing: Wait timeout\n");
            break;
        }
    }
}

void IntelVideoPostProcessing::flush() {
    if (!m_running) {
        return;
    }
    
    waitForCompletion(VPP_TIMEOUT_MS);
    resetHardware();
}


// Operation Implementation


VPPError IntelVideoPostProcessing::processScale(const VPPParams* params) {
    // Setup surfaces
    VPPError err = setupSurfaces(params->inputSurface, params->outputSurface,
                                 &params->inputDesc, &params->outputDesc);
    if (err != VPP_SUCCESS) {
        return err;
    }
    
    // Setup scaling parameters
    setupScaling(&params->params.scale);
    
    // Set operation mode
    setOperationMode(VPP_OP_SCALE);
    
    // Start operation
    writeRegister(VPP_COMMAND, 1);
    
    // Wait if not low latency
    if (!params->lowLatency) {
        waitForCompletion(VPP_TIMEOUT_MS);
    }
    
    IOLockLock(m_statsLock);
    m_stats.scaleOps++;
    IOLockUnlock(m_statsLock);
    
    return VPP_SUCCESS;
}

VPPError IntelVideoPostProcessing::processColorConvert(const VPPParams* params) {
    // Setup surfaces
    VPPError err = setupSurfaces(params->inputSurface, params->outputSurface,
                                 &params->inputDesc, &params->outputDesc);
    if (err != VPP_SUCCESS) {
        return err;
    }
    
    // Setup color conversion matrix
    setupColorMatrix(&params->params.colorConvert);
    
    // Set operation mode
    setOperationMode(VPP_OP_COLOR_CONVERT);
    
    // Start operation
    writeRegister(VPP_COMMAND, 1);
    
    if (!params->lowLatency) {
        waitForCompletion(VPP_TIMEOUT_MS);
    }
    
    IOLockLock(m_statsLock);
    m_stats.colorConvertOps++;
    IOLockUnlock(m_statsLock);
    
    return VPP_SUCCESS;
}

VPPError IntelVideoPostProcessing::processDeinterlace(const VPPParams* params) {
    // Setup surfaces
    VPPError err = setupSurfaces(params->inputSurface, params->outputSurface,
                                 &params->inputDesc, &params->outputDesc);
    if (err != VPP_SUCCESS) {
        return err;
    }
    
    // Setup deinterlace parameters
    setupDeinterlace(&params->params.deinterlace);
    
    // Set operation mode
    setOperationMode(VPP_OP_DEINTERLACE);
    
    // Start operation
    writeRegister(VPP_COMMAND, 1);
    
    if (!params->lowLatency) {
        waitForCompletion(VPP_TIMEOUT_MS);
    }
    
    IOLockLock(m_statsLock);
    m_stats.deinterlaceOps++;
    IOLockUnlock(m_statsLock);
    
    return VPP_SUCCESS;
}

VPPError IntelVideoPostProcessing::processDenoise(const VPPParams* params) {
    // Setup surfaces
    VPPError err = setupSurfaces(params->inputSurface, params->outputSurface,
                                 &params->inputDesc, &params->outputDesc);
    if (err != VPP_SUCCESS) {
        return err;
    }
    
    // Setup denoise strength
    float strength = params->params.denoise.strength;
    uint32_t strengthReg = (uint32_t)(strength * 255.0f);
    writeRegister(VPP_DENOISE_STRENGTH, strengthReg);
    
    // Set operation mode
    setOperationMode(VPP_OP_DENOISE);
    
    // Start operation
    writeRegister(VPP_COMMAND, 1);
    
    if (!params->lowLatency) {
        waitForCompletion(VPP_TIMEOUT_MS);
    }
    
    IOLockLock(m_statsLock);
    m_stats.denoiseOps++;
    IOLockUnlock(m_statsLock);
    
    return VPP_SUCCESS;
}

VPPError IntelVideoPostProcessing::processSharpen(const VPPParams* params) {
    // Setup surfaces
    VPPError err = setupSurfaces(params->inputSurface, params->outputSurface,
                                 &params->inputDesc, &params->outputDesc);
    if (err != VPP_SUCCESS) {
        return err;
    }
    
    // Setup sharpen strength
    float strength = params->params.sharpen.strength;
    uint32_t strengthReg = (uint32_t)(strength * 255.0f);
    writeRegister(VPP_SHARPEN_STRENGTH, strengthReg);
    
    // Set operation mode
    setOperationMode(VPP_OP_SHARPEN);
    
    // Start operation
    writeRegister(VPP_COMMAND, 1);
    
    if (!params->lowLatency) {
        waitForCompletion(VPP_TIMEOUT_MS);
    }
    
    IOLockLock(m_statsLock);
    m_stats.sharpenOps++;
    IOLockUnlock(m_statsLock);
    
    return VPP_SUCCESS;
}

VPPError IntelVideoPostProcessing::processCompose(const VPPParams* params) {
    const ComposeParams* compose = &params->params.compose;
    
    if (compose->numLayers == 0 || compose->numLayers > MAX_COMPOSITION_LAYERS) {
        return VPP_ERROR_INVALID_PARAMS;
    }
    
    // Setup composition layers
    setupLayers(compose->layers, compose->numLayers);
    
    // Setup output surface
    uint64_t outputAddr = params->outputSurface->getGPUAddress();
    writeRegister(VPP_OUTPUT_PTR, outputAddr & 0xFFFFFFFF);
    writeRegister(VPP_OUTPUT_PTR + 4, outputAddr >> 32);
    
    uint32_t outputSize = (params->outputDesc.height << 16) | params->outputDesc.width;
    writeRegister(VPP_OUTPUT_SIZE, outputSize);
    
    // Set operation mode
    setOperationMode(VPP_OP_COMPOSE);
    
    // Start operation
    writeRegister(VPP_COMMAND, 1);
    
    if (!params->lowLatency) {
        waitForCompletion(VPP_TIMEOUT_MS);
    }
    
    IOLockLock(m_statsLock);
    m_stats.composeOps++;
    IOLockUnlock(m_statsLock);
    
    return VPP_SUCCESS;
}

VPPError IntelVideoPostProcessing::processTransform(const VPPParams* params) {
    // Setup surfaces
    VPPError err = setupSurfaces(params->inputSurface, params->outputSurface,
                                 &params->inputDesc, &params->outputDesc);
    if (err != VPP_SUCCESS) {
        return err;
    }
    
    // Setup transform (rotation/mirror)
    const TransformParams* transform = &params->params.transform;
    uint32_t transformReg = 0;
    
    // Rotation bits
    transformReg |= (transform->rotation & 0x3) << 0;
    
    // Mirror bits
    if (transform->mirrorHorizontal) {
        transformReg |= (1 << 8);
    }
    if (transform->mirrorVertical) {
        transformReg |= (1 << 9);
    }
    
    writeRegister(VPP_CONTROL, readRegister(VPP_CONTROL) | transformReg);
    
    // Set operation mode
    setOperationMode(params->operation);
    
    // Start operation
    writeRegister(VPP_COMMAND, 1);
    
    if (!params->lowLatency) {
        waitForCompletion(VPP_TIMEOUT_MS);
    }
    
    IOLockLock(m_statsLock);
    m_stats.transformOps++;
    IOLockUnlock(m_statsLock);
    
    return VPP_SUCCESS;
}


// Helper Functions


void IntelVideoPostProcessing::setupScaling(const ScaleParams* params) {
    // Setup source rectangle
    uint32_t srcRect = (params->srcRect.y << 16) | params->srcRect.x;
    writeRegister(VPP_SRC_RECT, srcRect);
    srcRect = (params->srcRect.height << 16) | params->srcRect.width;
    writeRegister(VPP_SRC_RECT + 4, srcRect);
    
    // Setup destination rectangle
    uint32_t dstRect = (params->dstRect.y << 16) | params->dstRect.x;
    writeRegister(VPP_DST_RECT, dstRect);
    dstRect = (params->dstRect.height << 16) | params->dstRect.width;
    writeRegister(VPP_DST_RECT + 4, dstRect);
    
    // Calculate and setup scale factors
    float scaleX = (float)params->dstRect.width / (float)params->srcRect.width;
    float scaleY = (float)params->dstRect.height / (float)params->srcRect.height;
    
    uint32_t scaleFactor = ((uint32_t)(scaleY * 65536.0f) << 16) |
                          (uint32_t)(scaleX * 65536.0f);
    writeRegister(VPP_SCALE_FACTOR, scaleFactor);
    
    // Setup scaling mode
    uint32_t control = readRegister(VPP_CONTROL);
    control &= ~(0x3 << 16);  // Clear mode bits
    control |= (params->mode & 0x3) << 16;
    writeRegister(VPP_CONTROL, control);
}

void IntelVideoPostProcessing::setupColorMatrix(const ColorConvertParams* params) {
    // Color space conversion matrices
    // BT.601 to BT.709 example
    float matrix[3][3];
    
    if (params->srcColorSpace == COLOR_SPACE_BT601 &&
        params->dstColorSpace == COLOR_SPACE_BT709) {
        // BT.601 -> BT.709 conversion matrix
        matrix[0][0] = 1.0164f;  matrix[0][1] = -0.0458f; matrix[0][2] = 0.0000f;
        matrix[1][0] = 0.0000f;  matrix[1][1] = 1.0402f;  matrix[1][2] = -0.0719f;
        matrix[2][0] = 0.0000f;  matrix[2][1] = 0.0000f;  matrix[2][2] = 1.0546f;
    } else {
        // Identity matrix
        matrix[0][0] = 1.0f; matrix[0][1] = 0.0f; matrix[0][2] = 0.0f;
        matrix[1][0] = 0.0f; matrix[1][1] = 1.0f; matrix[1][2] = 0.0f;
        matrix[2][0] = 0.0f; matrix[2][1] = 0.0f; matrix[2][2] = 1.0f;
    }
    
    // Apply brightness/contrast/saturation adjustments
    float brightness = params->brightness;
    float contrast = params->contrast;
    float saturation = params->saturation;
    
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            matrix[i][j] *= contrast;
            if (i == j && i > 0) {  // Saturation on color channels
                matrix[i][j] *= saturation;
            }
        }
    }
    
    // Write matrix to hardware (fixed-point 1.15 format)
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            uint32_t value = (uint32_t)(matrix[i][j] * 32768.0f);
            writeRegister(VPP_COLOR_MATRIX + (i * 12 + j * 4), value);
        }
    }
}

void IntelVideoPostProcessing::setupDeinterlace(const DeinterlaceParams* params) {
    uint32_t control = readRegister(VPP_CONTROL);
    
    // Set deinterlace mode
    control &= ~(0x3 << 20);  // Clear mode bits
    control |= (params->mode & 0x3) << 20;
    
    // Set field order
    if (params->topFieldFirst) {
        control |= (1 << 22);
    } else {
        control &= ~(1 << 22);
    }
    
    writeRegister(VPP_CONTROL, control);
}

void IntelVideoPostProcessing::setupLayers(const CompositionLayer* layers,
                                          uint32_t numLayers) {
    // Sort layers by Z-order
    CompositionLayer sortedLayers[MAX_COMPOSITION_LAYERS];
    memcpy(sortedLayers, layers, numLayers * sizeof(CompositionLayer));
    
    // Simple bubble sort by z-order
    for (uint32_t i = 0; i < numLayers - 1; i++) {
        for (uint32_t j = 0; j < numLayers - i - 1; j++) {
            if (sortedLayers[j].zOrder > sortedLayers[j + 1].zOrder) {
                CompositionLayer temp = sortedLayers[j];
                sortedLayers[j] = sortedLayers[j + 1];
                sortedLayers[j + 1] = temp;
            }
        }
    }
    
    // Setup each layer
    for (uint32_t i = 0; i < numLayers; i++) {
        const CompositionLayer* layer = &sortedLayers[i];
        uint32_t layerBase = VPP_LAYER_BASE + (i * 0x40);
        
        // Surface address
        uint64_t surfAddr = layer->surface->getGPUAddress();
        writeRegister(layerBase + 0x00, surfAddr & 0xFFFFFFFF);
        writeRegister(layerBase + 0x04, surfAddr >> 32);
        
        // Source rectangle
        writeRegister(layerBase + 0x08, (layer->srcRect.y << 16) | layer->srcRect.x);
        writeRegister(layerBase + 0x0C, (layer->srcRect.height << 16) | layer->srcRect.width);
        
        // Destination rectangle
        writeRegister(layerBase + 0x10, (layer->dstRect.y << 16) | layer->dstRect.x);
        writeRegister(layerBase + 0x14, (layer->dstRect.height << 16) | layer->dstRect.width);
        
        // Alpha and blend mode
        uint32_t alphaReg = (uint32_t)(layer->alpha * 255.0f);
        alphaReg |= (layer->blendMode & 0xF) << 8;
        writeRegister(layerBase + 0x18, alphaReg);
        
        // Enable layer
        writeRegister(layerBase + 0x1C, 1);
    }
    
    // Set layer count
    writeRegister(VPP_CONTROL + 8, numLayers);
}

VPPError IntelVideoPostProcessing::setupSurfaces(IntelGEMObject* input,
                                                 IntelGEMObject* output,
                                                 const SurfaceDesc* inputDesc,
                                                 const SurfaceDesc* outputDesc) {
    if (!input || !output) {
        return VPP_ERROR_INVALID_PARAMS;
    }
    
    // Setup input surface
    uint64_t inputAddr = input->getGPUAddress();
    writeRegister(VPP_INPUT_PTR, inputAddr & 0xFFFFFFFF);
    writeRegister(VPP_INPUT_PTR + 4, inputAddr >> 32);
    
    uint32_t inputSize = (inputDesc->height << 16) | inputDesc->width;
    writeRegister(VPP_INPUT_SIZE, inputSize);
    
    // Setup output surface
    uint64_t outputAddr = output->getGPUAddress();
    writeRegister(VPP_OUTPUT_PTR, outputAddr & 0xFFFFFFFF);
    writeRegister(VPP_OUTPUT_PTR + 4, outputAddr >> 32);
    
    uint32_t outputSize = (outputDesc->height << 16) | outputDesc->width;
    writeRegister(VPP_OUTPUT_SIZE, outputSize);
    
    return VPP_SUCCESS;
}


// Format Support


bool IntelVideoPostProcessing::isOperationSupported(IntelVPPOperation op) const {
    switch (op) {
        case VPP_OP_SCALE:
            return m_capabilities.scalingSupported;
        case VPP_OP_COLOR_CONVERT:
            return m_capabilities.colorConvertSupported;
        case VPP_OP_DEINTERLACE:
            return m_capabilities.deinterlaceSupported;
        case VPP_OP_DENOISE:
            return m_capabilities.denoiseSupported;
        case VPP_OP_SHARPEN:
            return m_capabilities.sharpenSupported;
        case VPP_OP_COMPOSE:
            return m_capabilities.compositionSupported;
        case VPP_OP_ROTATE:
        case VPP_OP_MIRROR:
        case VPP_OP_CROP:
            return m_capabilities.rotationSupported;
        default:
            return false;
    }
}

bool IntelVideoPostProcessing::isFormatSupported(uint32_t format) const {
    // Common formats supported by VPP
    switch (format) {
        case 0x3231564E:  // NV12
        case 0x30323449:  // I420
        case 0x56595559:  // YUYV
        case 0x42475241:  // ARGB
        case 0x42475258:  // XRGB
        case 0x30315950:  // P010 (10-bit)
            return true;
        default:
            return false;
    }
}

VPPError IntelVideoPostProcessing::validateParams(const VPPParams* params) {
    if (!params) {
        return VPP_ERROR_INVALID_PARAMS;
    }
    
    // Check operation support
    if (!isOperationSupported(params->operation)) {
        IOLog("IntelVideoPostProcessing: Unsupported operation %d\n", params->operation);
        return VPP_ERROR_UNSUPPORTED_OPERATION;
    }
    
    // Check input/output surfaces (not needed for compose)
    if (params->operation != VPP_OP_COMPOSE) {
        if (!params->inputSurface || !params->outputSurface) {
            return VPP_ERROR_INVALID_PARAMS;
        }
        
        // Check format support
        if (!isFormatSupported(params->inputDesc.format) ||
            !isFormatSupported(params->outputDesc.format)) {
            return VPP_ERROR_UNSUPPORTED_FORMAT;
        }
        
        // Check size limits
        if (params->inputDesc.width > m_capabilities.maxInputWidth ||
            params->inputDesc.height > m_capabilities.maxInputHeight ||
            params->outputDesc.width > m_capabilities.maxOutputWidth ||
            params->outputDesc.height > m_capabilities.maxOutputHeight) {
            return VPP_ERROR_INVALID_SIZE;
        }
    }
    
    return VPP_SUCCESS;
}


// Statistics


void IntelVideoPostProcessing::getStatistics(VPPStatistics* stats) {
    if (!stats) {
        return;
    }
    
    IOLockLock(m_statsLock);
    memcpy(stats, &m_stats, sizeof(VPPStatistics));
    IOLockUnlock(m_statsLock);
}

void IntelVideoPostProcessing::resetStatistics() {
    IOLockLock(m_statsLock);
    memset(&m_stats, 0, sizeof(VPPStatistics));
    IOLockUnlock(m_statsLock);
}

void IntelVideoPostProcessing::printStatistics() {
    VPPStatistics stats;
    getStatistics(&stats);
    
    IOLog("Frames Processed: %llu\n", stats.framesProcessed);
    IOLog("FPS: %.2f\n", stats.framesPerSecond);
    IOLog("Average Time: %llu ns\n", stats.averageTime);
    IOLog("Operations:\n");
    IOLog("  Scaling: %llu\n", stats.scaleOps);
    IOLog("  Color Convert: %llu\n", stats.colorConvertOps);
    IOLog("  Deinterlace: %llu\n", stats.deinterlaceOps);
    IOLog("  Denoise: %llu\n", stats.denoiseOps);
    IOLog("  Sharpen: %llu\n", stats.sharpenOps);
    IOLog("  Compose: %llu\n", stats.composeOps);
    IOLog("  Transform: %llu\n", stats.transformOps);
    IOLog("Hardware Usage: %.1f%%\n", stats.hardwareUsage);
    IOLog("Errors: %u\n", stats.errors);
}

void IntelVideoPostProcessing::updateStatistics(uint64_t processTime, VPPError error) {
    IOLockLock(m_statsLock);
    
    m_stats.framesProcessed++;
    m_stats.totalTime += processTime;
    m_stats.averageTime = m_stats.totalTime / m_stats.framesProcessed;
    
    // Calculate FPS (assuming nanosecond timestamps)
    if (processTime > 0) {
        m_stats.framesPerSecond = 1000000000.0f / (float)processTime;
    }
    
    if (error != VPP_SUCCESS) {
        m_stats.errors++;
    }
    
    m_lastProcessTime = processTime;
    
    IOLockUnlock(m_statsLock);
}


// Capabilities


void IntelVideoPostProcessing::getCapabilities(VPPCapabilities* caps) {
    if (!caps) {
        return;
    }
    
    memcpy(caps, &m_capabilities, sizeof(VPPCapabilities));
}

uint32_t IntelVideoPostProcessing::getMaxInputWidth() const {
    return m_capabilities.maxInputWidth;
}

uint32_t IntelVideoPostProcessing::getMaxInputHeight() const {
    return m_capabilities.maxInputHeight;
}

float IntelVideoPostProcessing::getMaxScaleFactor() const {
    return m_capabilities.maxScaleFactor;
}

void IntelVideoPostProcessing::detectCapabilities() {
    // Tiger Lake (Gen12) capabilities
    m_capabilities.scalingSupported = true;
    m_capabilities.colorConvertSupported = true;
    m_capabilities.deinterlaceSupported = true;
    m_capabilities.denoiseSupported = true;
    m_capabilities.sharpenSupported = true;
    m_capabilities.compositionSupported = true;
    m_capabilities.rotationSupported = true;
    
    m_capabilities.maxInputWidth = 4096;
    m_capabilities.maxInputHeight = 4096;
    m_capabilities.maxOutputWidth = 4096;
    m_capabilities.maxOutputHeight = 4096;
    m_capabilities.maxScaleFactor = 8.0f;
    m_capabilities.maxLayers = MAX_COMPOSITION_LAYERS;
    
    m_capabilities.bit10Supported = true;
    m_capabilities.hdrSupported = true;
    m_capabilities.temporalDenoiseSupported = true;
    
    IOLog("IntelVideoPostProcessing: Capabilities detected (max: %ux%u)\n",
          m_capabilities.maxInputWidth, m_capabilities.maxInputHeight);
}


// Hardware Control


bool IntelVideoPostProcessing::enableHardware() {
    // Reset hardware
    writeRegister(VPP_CONTROL, VPP_CONTROL_RESET);
    IOSleep(1);
    
    // Enable VPP
    writeRegister(VPP_CONTROL, VPP_CONTROL_ENABLE);
    
    // Check if enabled
    uint32_t status = readRegister(VPP_STATUS);
    if (!(status & VPP_STATUS_IDLE)) {
        IOLog("IntelVideoPostProcessing: Hardware enable failed\n");
        return false;
    }
    
    IOLog("IntelVideoPostProcessing: Hardware enabled\n");
    return true;
}

void IntelVideoPostProcessing::disableHardware() {
    writeRegister(VPP_CONTROL, 0);
    IOLog("IntelVideoPostProcessing: Hardware disabled\n");
}

void IntelVideoPostProcessing::resetHardware() {
    writeRegister(VPP_CONTROL, VPP_CONTROL_RESET);
    IOSleep(1);
    writeRegister(VPP_CONTROL, VPP_CONTROL_ENABLE);
}

bool IntelVideoPostProcessing::isHardwareIdle() {
    uint32_t status = readRegister(VPP_STATUS);
    return (status & VPP_STATUS_IDLE) != 0;
}

void IntelVideoPostProcessing::writeRegister(uint32_t reg, uint32_t value) {
    if (m_controller) {
        m_controller->writeRegister32(reg, value);
    }
}

uint32_t IntelVideoPostProcessing::readRegister(uint32_t reg) {
    if (m_controller) {
        return m_controller->readRegister32(reg);
    }
    return 0;
}

void IntelVideoPostProcessing::setOperationMode(IntelVPPOperation op) {
    uint32_t control = readRegister(VPP_CONTROL);
    control &= ~VPP_CONTROL_OP_MASK;
    
    switch (op) {
        case VPP_OP_SCALE:
            control |= VPP_CONTROL_OP_SCALE;
            break;
        case VPP_OP_COLOR_CONVERT:
            control |= VPP_CONTROL_OP_COLOR;
            break;
        case VPP_OP_DEINTERLACE:
            control |= VPP_CONTROL_OP_DEINTERLACE;
            break;
        case VPP_OP_DENOISE:
            control |= VPP_CONTROL_OP_DENOISE;
            break;
        case VPP_OP_SHARPEN:
            control |= VPP_CONTROL_OP_SHARPEN;
            break;
        case VPP_OP_COMPOSE:
            control |= VPP_CONTROL_OP_COMPOSE;
            break;
        default:
            break;
    }
    
    writeRegister(VPP_CONTROL, control);
}


// Buffer Management


VPPError IntelVideoPostProcessing::allocateInternalBuffers() {
    // Allocate temporary processing buffer (4K, ARGB)
    m_tempBuffer = m_controller->allocateGEMObject(4096 * 4096 * 4);
    if (!m_tempBuffer) {
        return VPP_ERROR_NO_MEMORY;
    }
    
    // Allocate layer composition buffer
    m_layerBuffer = m_controller->allocateGEMObject(4096 * 4096 * 4);
    if (!m_layerBuffer) {
        m_tempBuffer->release();
        m_tempBuffer = nullptr;
        return VPP_ERROR_NO_MEMORY;
    }
    
    return VPP_SUCCESS;
}

void IntelVideoPostProcessing::freeInternalBuffers() {
    if (m_tempBuffer) {
        m_tempBuffer->release();
        m_tempBuffer = nullptr;
    }
    
    if (m_layerBuffer) {
        m_layerBuffer->release();
        m_layerBuffer = nullptr;
    }
}


// Error Handling


const char* IntelVideoPostProcessing::getErrorString(VPPError error) {
    switch (error) {
        case VPP_SUCCESS:
            return "Success";
        case VPP_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case VPP_ERROR_UNSUPPORTED_FORMAT:
            return "Unsupported format";
        case VPP_ERROR_UNSUPPORTED_OPERATION:
            return "Unsupported operation";
        case VPP_ERROR_NO_MEMORY:
            return "Out of memory";
        case VPP_ERROR_HARDWARE_FAULT:
            return "Hardware fault";
        case VPP_ERROR_TIMEOUT:
            return "Operation timeout";
        case VPP_ERROR_INVALID_SIZE:
            return "Invalid size";
        default:
            return "Unknown error";
    }
}
