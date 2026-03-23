/*
 * Intel3DPipeline.cpp - 3D Rendering Pipeline Implementation
 * Week 26 - Phase 6: 3D Hardware Acceleration
 */

#include "Intel3DPipeline.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(Intel3DPipeline, OSObject)

// Initialization
bool Intel3DPipeline::init() {
    if (!super::init()) {
        return false;
    }
    
    controller = nullptr;
    renderRing = nullptr;
    pipelineContext = nullptr;
    currentState = nullptr;
    lock = nullptr;
    initialized = false;
    pipelineActive = false;
    
    memset(&stats, 0, sizeof(stats));
    
    return true;
}

void Intel3DPipeline::free() {
    if (initialized) {
        stop();
    }
    
    if (currentState) {
        IOFree(currentState, sizeof(Intel3DState));
        currentState = nullptr;
    }
    
    if (lock) {
        IORecursiveLockFree(lock);
        lock = nullptr;
    }
    
    super::free();
}

bool Intel3DPipeline::initWithController(AppleIntelTGLController* ctrl) {
    if (!ctrl) {
        return false;
    }
    
    controller = ctrl;
    
    // Create lock
    lock = IORecursiveLockAlloc();
    if (!lock) {
        IOLog("Intel3DPipeline: Failed to allocate lock\n");
        return false;
    }
    
    // Allocate pipeline state
    currentState = (Intel3DState*)IOMalloc(sizeof(Intel3DState));
    if (!currentState) {
        IOLog("Intel3DPipeline: Failed to allocate pipeline state\n");
        return false;
    }
    memset(currentState, 0, sizeof(Intel3DState));
    
    // Initialize default state
    currentState->topology = TOPOLOGY_TRIANGLES;
    currentState->cullMode = CULL_BACK;
    currentState->fillMode = FILL_SOLID;
    currentState->depthFunc = DEPTH_OP_LESS;
    currentState->blendOp = BLEND_OP_ADD;
    currentState->srcBlend = BLEND_SRC_ALPHA;
    currentState->dstBlend = BLEND_INV_SRC_ALPHA;
    
    initialized = true;
    IOLog("Intel3DPipeline: Initialized successfully\n");
    return true;
}

// Lifecycle
bool Intel3DPipeline::start() {
    IORecursiveLockLock(lock);
    
    if (pipelineActive) {
        IORecursiveLockUnlock(lock);
        return true;
    }
    
    // Get render ring buffer
    renderRing = controller->getRenderRing();
    if (!renderRing) {
        IOLog("Intel3DPipeline: Failed to get render ring\n");
        IORecursiveLockUnlock(lock);
        return false;
    }
    
    // Create pipeline context
    pipelineContext = new IntelContext();
    if (!pipelineContext || !pipelineContext->initWithController(controller)) {
        IOLog("Intel3DPipeline: Failed to create pipeline context\n");
        IORecursiveLockUnlock(lock);
        return false;
    }
    
    // Initialize 3D pipeline
    Pipeline3DError error = createPipeline(PIPELINE_3D);
    if (error != PIPELINE_SUCCESS) {
        IOLog("Intel3DPipeline: Failed to create 3D pipeline: %d\n", error);
        pipelineContext->release();
        pipelineContext = nullptr;
        IORecursiveLockUnlock(lock);
        return false;
    }
    
    pipelineActive = true;
    IOLog("Intel3DPipeline: Started successfully\n");
    
    IORecursiveLockUnlock(lock);
    return true;
}

void Intel3DPipeline::stop() {
    IORecursiveLockLock(lock);
    
    if (!pipelineActive) {
        IORecursiveLockUnlock(lock);
        return;
    }
    
    // Wait for idle
    waitForIdle(5000);
    
    // Destroy pipeline
    destroyPipeline();
    
    // Release context
    if (pipelineContext) {
        pipelineContext->release();
        pipelineContext = nullptr;
    }
    
    pipelineActive = false;
    IOLog("Intel3DPipeline: Stopped\n");
    
    IORecursiveLockUnlock(lock);
}

// Pipeline Management
Pipeline3DError Intel3DPipeline::createPipeline(IntelPipelineType type) {
    uint32_t commands[32];
    uint32_t* cmd = commands;
    
    // Build pipeline select command
    cmd = buildPipelineSelectCommand(cmd, type);
    
    // Submit command
    IntelRequest* request = nullptr;
    Pipeline3DError error = submitPipelineCommand(commands, 
                                                  (uint32_t)(cmd - commands),
                                                  &request);
    if (error != PIPELINE_SUCCESS) {
        return error;
    }
    
    // Wait for completion
    error = waitForCompletion(request, 1000);
    if (request) {
        request->release();
    }
    
    return error;
}

Pipeline3DError Intel3DPipeline::destroyPipeline() {
    // Flush and wait
    return flush();
}

Pipeline3DError Intel3DPipeline::bindPipeline() {
    // Pipeline binding is implicit in state setup
    return PIPELINE_SUCCESS;
}

// Shader Management
IntelShaderProgram* Intel3DPipeline::createShaderProgram(IntelShaderType type,
                                                        const void* kernelCode,
                                                        uint32_t kernelSize) {
    if (!kernelCode || kernelSize == 0) {
        return nullptr;
    }
    
    IORecursiveLockLock(lock);
    
    // Allocate shader program structure
    IntelShaderProgram* program = (IntelShaderProgram*)IOMalloc(sizeof(IntelShaderProgram));
    if (!program) {
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    memset(program, 0, sizeof(IntelShaderProgram));
    
    // Create GEM object for kernel
    program->kernelObject = controller->allocateGEMObject(kernelSize);
    if (!program->kernelObject) {
        IOFree(program, sizeof(IntelShaderProgram));
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    
    // Copy kernel code
    void* mapped = nullptr;
    if (!program->kernelObject->mapCPU(&mapped) || !mapped) {
        program->kernelObject->release();
        IOFree(program, sizeof(IntelShaderProgram));
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    memcpy(mapped, kernelCode, kernelSize);
    program->kernelObject->unmapCPU();
    
    program->type = type;
    program->kernelSize = kernelSize;
    program->compiled = false;
    program->gpuAddress = program->kernelObject->getGPUAddress();
    
    IORecursiveLockUnlock(lock);
    return program;
}

void Intel3DPipeline::destroyShaderProgram(IntelShaderProgram* program) {
    if (!program) {
        return;
    }
    
    IORecursiveLockLock(lock);
    
    if (program->kernelObject) {
        program->kernelObject->release();
    }
    
    IOFree(program, sizeof(IntelShaderProgram));
    
    IORecursiveLockUnlock(lock);
}

Pipeline3DError Intel3DPipeline::compileShader(IntelShaderProgram* program) {
    if (!validateShader(program)) {
        return PIPELINE_SHADER_ERROR;
    }
    
    // In a real implementation, this would invoke the shader compiler
    // For now, we assume the kernel is pre-compiled
    program->compiled = true;
    
    return PIPELINE_SUCCESS;
}

Pipeline3DError Intel3DPipeline::bindShader(IntelShaderProgram* program) {
    if (!program || !program->compiled) {
        return PIPELINE_SHADER_ERROR;
    }
    
    IORecursiveLockLock(lock);
    
    // Update current state based on shader type
    switch (program->type) {
        case SHADER_VERTEX:
            currentState->vertexShader = program;
            break;
        case SHADER_HULL:
            currentState->hullShader = program;
            break;
        case SHADER_DOMAIN:
            currentState->domainShader = program;
            break;
        case SHADER_GEOMETRY:
            currentState->geometryShader = program;
            break;
        case SHADER_FRAGMENT:
            currentState->fragmentShader = program;
            break;
        default:
            IORecursiveLockUnlock(lock);
            return PIPELINE_SHADER_ERROR;
    }
    
    IORecursiveLockUnlock(lock);
    return PIPELINE_SUCCESS;
}

// State Management
Pipeline3DError Intel3DPipeline::setPipelineState(const Intel3DState* state) {
    if (!state) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    memcpy(currentState, state, sizeof(Intel3DState));
    IORecursiveLockUnlock(lock);
    
    return PIPELINE_SUCCESS;
}

Pipeline3DError Intel3DPipeline::getPipelineState(Intel3DState* state) {
    if (!state) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    memcpy(state, currentState, sizeof(Intel3DState));
    IORecursiveLockUnlock(lock);
    
    return PIPELINE_SUCCESS;
}

Pipeline3DError Intel3DPipeline::resetPipelineState() {
    IORecursiveLockLock(lock);
    
    memset(currentState, 0, sizeof(Intel3DState));
    
    // Restore defaults
    currentState->topology = TOPOLOGY_TRIANGLES;
    currentState->cullMode = CULL_BACK;
    currentState->fillMode = FILL_SOLID;
    currentState->depthFunc = DEPTH_OP_LESS;
    currentState->blendOp = BLEND_OP_ADD;
    currentState->srcBlend = BLEND_SRC_ALPHA;
    currentState->dstBlend = BLEND_INV_SRC_ALPHA;
    
    IORecursiveLockUnlock(lock);
    return PIPELINE_SUCCESS;
}

// Resource Management - Textures
IntelTexture* Intel3DPipeline::createTexture(uint32_t width, uint32_t height,
                                            IntelTextureFormat format,
                                            uint32_t mipLevels) {
    if (width == 0 || height == 0) {
        return nullptr;
    }
    
    IORecursiveLockLock(lock);
    
    IntelTexture* texture = (IntelTexture*)IOMalloc(sizeof(IntelTexture));
    if (!texture) {
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    memset(texture, 0, sizeof(IntelTexture));
    
    // Calculate texture size
    uint32_t bpp = getTextureBytesPerPixel(format);
    uint32_t pitch = calculateTexturePitch(width, format);
    uint64_t totalSize = pitch * height;
    
    // Add mipmap sizes
    if (mipLevels > 1) {
        uint32_t mipWidth = width;
        uint32_t mipHeight = height;
        for (uint32_t i = 1; i < mipLevels; i++) {
            mipWidth = (mipWidth > 1) ? (mipWidth / 2) : 1;
            mipHeight = (mipHeight > 1) ? (mipHeight / 2) : 1;
            uint32_t mipPitch = calculateTexturePitch(mipWidth, format);
            totalSize += mipPitch * mipHeight;
        }
    }
    
    // Create GEM object
    texture->object = controller->allocateGEMObject(totalSize);
    if (!texture->object) {
        IOFree(texture, sizeof(IntelTexture));
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    
    texture->width = width;
    texture->height = height;
    texture->depth = 1;
    texture->format = format;
    texture->mipLevels = mipLevels;
    texture->arraySize = 1;
    texture->gpuAddress = texture->object->getGPUAddress();
    texture->pitch = pitch;
    
    IORecursiveLockUnlock(lock);
    return texture;
}

void Intel3DPipeline::destroyTexture(IntelTexture* texture) {
    if (!texture) {
        return;
    }
    
    IORecursiveLockLock(lock);
    
    if (texture->object) {
        texture->object->release();
    }
    
    IOFree(texture, sizeof(IntelTexture));
    
    IORecursiveLockUnlock(lock);
}

Pipeline3DError Intel3DPipeline::bindTexture(IntelTexture* texture, uint32_t slot) {
    if (!texture || slot >= 32) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    if (!validateTexture(texture)) {
        return PIPELINE_RESOURCE_ERROR;
    }
    
    IORecursiveLockLock(lock);
    currentState->textures[slot] = texture;
    if (slot >= currentState->numTextures) {
        currentState->numTextures = slot + 1;
    }
    IORecursiveLockUnlock(lock);
    
    return PIPELINE_SUCCESS;
}

Pipeline3DError Intel3DPipeline::updateTexture(IntelTexture* texture,
                                              const void* data,
                                              uint32_t mipLevel) {
    if (!texture || !data || mipLevel >= texture->mipLevels) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    // Map texture memory
    void* mapped = nullptr;
    if (!texture->object->mapCPU(&mapped) || !mapped) {
        IORecursiveLockUnlock(lock);
        return PIPELINE_MEMORY_ERROR;
    }
    
    // Calculate mip level offset and size
    uint64_t offset = 0;
    uint32_t mipWidth = texture->width;
    uint32_t mipHeight = texture->height;
    
    for (uint32_t i = 0; i < mipLevel; i++) {
        uint32_t mipPitch = calculateTexturePitch(mipWidth, texture->format);
        offset += mipPitch * mipHeight;
        mipWidth = (mipWidth > 1) ? (mipWidth / 2) : 1;
        mipHeight = (mipHeight > 1) ? (mipHeight / 2) : 1;
    }
    
    uint32_t mipPitch = calculateTexturePitch(mipWidth, texture->format);
    uint64_t mipSize = mipPitch * mipHeight;
    
    // Copy data
    memcpy((uint8_t*)mapped + offset, data, mipSize);
    
    texture->object->unmapCPU();
    
    IORecursiveLockUnlock(lock);
    return PIPELINE_SUCCESS;
}

// Resource Management - Samplers
IntelSampler* Intel3DPipeline::createSampler(IntelTextureFilter filter,
                                            IntelTextureWrap wrap) {
    IntelSampler* sampler = (IntelSampler*)IOMalloc(sizeof(IntelSampler));
    if (!sampler) {
        return nullptr;
    }
    memset(sampler, 0, sizeof(IntelSampler));
    
    sampler->minFilter = filter;
    sampler->magFilter = filter;
    sampler->wrapU = wrap;
    sampler->wrapV = wrap;
    sampler->wrapW = wrap;
    sampler->minLOD = 0.0f;
    sampler->maxLOD = 1000.0f;
    sampler->anisotropyEnable = false;
    sampler->maxAnisotropy = 1;
    
    return sampler;
}

void Intel3DPipeline::destroySampler(IntelSampler* sampler) {
    if (sampler) {
        IOFree(sampler, sizeof(IntelSampler));
    }
}

Pipeline3DError Intel3DPipeline::bindSampler(IntelSampler* sampler, uint32_t slot) {
    if (!sampler || slot >= 16) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    currentState->samplers[slot] = sampler;
    if (slot >= currentState->numSamplers) {
        currentState->numSamplers = slot + 1;
    }
    IORecursiveLockUnlock(lock);
    
    return PIPELINE_SUCCESS;
}

// Resource Management - Buffers
IntelBuffer* Intel3DPipeline::createBuffer(uint64_t size, bool isVertexBuffer,
                                          bool isIndexBuffer) {
    if (size == 0) {
        return nullptr;
    }
    
    IORecursiveLockLock(lock);
    
    IntelBuffer* buffer = (IntelBuffer*)IOMalloc(sizeof(IntelBuffer));
    if (!buffer) {
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    memset(buffer, 0, sizeof(IntelBuffer));
    
    // Create GEM object
    buffer->object = controller->allocateGEMObject(size);
    if (!buffer->object) {
        IOFree(buffer, sizeof(IntelBuffer));
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    
    buffer->size = size;
    buffer->gpuAddress = buffer->object->getGPUAddress();
    buffer->isVertexBuffer = isVertexBuffer;
    buffer->isIndexBuffer = isIndexBuffer;
    buffer->isConstantBuffer = !isVertexBuffer && !isIndexBuffer;
    buffer->stride = 0;
    
    IORecursiveLockUnlock(lock);
    return buffer;
}

void Intel3DPipeline::destroyBuffer(IntelBuffer* buffer) {
    if (!buffer) {
        return;
    }
    
    IORecursiveLockLock(lock);
    
    if (buffer->object) {
        buffer->object->release();
    }
    
    IOFree(buffer, sizeof(IntelBuffer));
    
    IORecursiveLockUnlock(lock);
}

Pipeline3DError Intel3DPipeline::bindVertexBuffer(IntelBuffer* buffer, uint32_t slot) {
    if (!buffer || slot >= 16 || !buffer->isVertexBuffer) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    if (!validateBuffer(buffer)) {
        return PIPELINE_RESOURCE_ERROR;
    }
    
    IORecursiveLockLock(lock);
    currentState->vertexBuffers[slot] = buffer;
    if (slot >= currentState->numVertexBuffers) {
        currentState->numVertexBuffers = slot + 1;
    }
    IORecursiveLockUnlock(lock);
    
    return PIPELINE_SUCCESS;
}

Pipeline3DError Intel3DPipeline::bindIndexBuffer(IntelBuffer* buffer) {
    if (!buffer || !buffer->isIndexBuffer) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    if (!validateBuffer(buffer)) {
        return PIPELINE_RESOURCE_ERROR;
    }
    
    IORecursiveLockLock(lock);
    currentState->indexBuffer = buffer;
    IORecursiveLockUnlock(lock);
    
    return PIPELINE_SUCCESS;
}

Pipeline3DError Intel3DPipeline::bindConstantBuffer(IntelBuffer* buffer, uint32_t slot) {
    if (!buffer || slot >= 8 || !buffer->isConstantBuffer) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    if (!validateBuffer(buffer)) {
        return PIPELINE_RESOURCE_ERROR;
    }
    
    IORecursiveLockLock(lock);
    currentState->constantBuffers[slot] = buffer;
    if (slot >= currentState->numConstantBuffers) {
        currentState->numConstantBuffers = slot + 1;
    }
    IORecursiveLockUnlock(lock);
    
    return PIPELINE_SUCCESS;
}

Pipeline3DError Intel3DPipeline::updateBuffer(IntelBuffer* buffer,
                                             const void* data,
                                             uint64_t size) {
    if (!buffer || !data || size > buffer->size) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    void* mapped = nullptr;
    if (!buffer->object->mapCPU(&mapped) || !mapped) {
        IORecursiveLockUnlock(lock);
        return PIPELINE_MEMORY_ERROR;
    }
    
    memcpy(mapped, data, size);
    buffer->object->unmapCPU();
    
    IORecursiveLockUnlock(lock);
    return PIPELINE_SUCCESS;
}

// Resource Management - Render Targets
IntelRenderTarget* Intel3DPipeline::createRenderTarget(uint32_t width,
                                                       uint32_t height,
                                                       IntelTextureFormat colorFormat,
                                                       IntelTextureFormat depthFormat) {
    if (width == 0 || height == 0) {
        return nullptr;
    }
    
    IORecursiveLockLock(lock);
    
    IntelRenderTarget* target = (IntelRenderTarget*)IOMalloc(sizeof(IntelRenderTarget));
    if (!target) {
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    memset(target, 0, sizeof(IntelRenderTarget));
    
    // Create color buffer
    uint32_t colorPitch = calculateTexturePitch(width, colorFormat);
    uint64_t colorSize = colorPitch * height;
    
    target->colorObject = controller->allocateGEMObject(colorSize);
    if (!target->colorObject) {
        IOFree(target, sizeof(IntelRenderTarget));
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    
    // Create depth buffer
    uint32_t depthPitch = calculateTexturePitch(width, depthFormat);
    uint64_t depthSize = depthPitch * height;
    
    target->depthObject = controller->allocateGEMObject(depthSize);
    if (!target->depthObject) {
        target->colorObject->release();
        IOFree(target, sizeof(IntelRenderTarget));
        IORecursiveLockUnlock(lock);
        return nullptr;
    }
    
    target->width = width;
    target->height = height;
    target->colorFormat = colorFormat;
    target->depthFormat = depthFormat;
    target->colorAddress = target->colorObject->getGPUAddress();
    target->depthAddress = target->depthObject->getGPUAddress();
    target->colorPitch = colorPitch;
    target->depthPitch = depthPitch;
    
    IORecursiveLockUnlock(lock);
    return target;
}

void Intel3DPipeline::destroyRenderTarget(IntelRenderTarget* target) {
    if (!target) {
        return;
    }
    
    IORecursiveLockLock(lock);
    
    if (target->colorObject) target->colorObject->release();
    if (target->depthObject) target->depthObject->release();
    if (target->stencilObject) target->stencilObject->release();
    
    IOFree(target, sizeof(IntelRenderTarget));
    
    IORecursiveLockUnlock(lock);
}

Pipeline3DError Intel3DPipeline::bindRenderTarget(IntelRenderTarget* target) {
    if (!target) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    if (!validateRenderTarget(target)) {
        return PIPELINE_RESOURCE_ERROR;
    }
    
    IORecursiveLockLock(lock);
    currentState->renderTarget = target;
    IORecursiveLockUnlock(lock);
    
    return PIPELINE_SUCCESS;
}

// Drawing Operations
Pipeline3DError Intel3DPipeline::drawPrimitives(IntelPrimitiveTopology topology,
                                               uint32_t vertexStart,
                                               uint32_t vertexCount) {
    if (!validateDrawParams(vertexStart, vertexCount)) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    recordDrawStart();
    
    uint32_t commands[256];
    uint32_t* cmd = commands;
    
    // Build state commands
    if (currentState->vertexShader) {
        cmd = buildVertexShaderState(cmd, currentState->vertexShader);
    }
    if (currentState->fragmentShader) {
        cmd = buildFragmentShaderState(cmd, currentState->fragmentShader);
    }
    
    cmd = buildViewportState(cmd, currentState->viewport);
    cmd = buildScissorState(cmd, currentState->scissor);
    
    if (currentState->renderTarget) {
        cmd = buildDepthBufferState(cmd, currentState->renderTarget);
        cmd = buildColorBufferState(cmd, currentState->renderTarget);
    }
    
    // Build draw command
    cmd = buildDrawCommand(cmd, topology, vertexStart, vertexCount);
    
    // Build pipe control
    cmd = buildPipeControlCommand(cmd);
    
    // Submit
    IntelRequest* request = nullptr;
    Pipeline3DError error = submitPipelineCommand(commands,
                                                  (uint32_t)(cmd - commands),
                                                  &request);
    
    if (error == PIPELINE_SUCCESS && request) {
        waitForCompletion(request, 5000);
        request->release();
        
        uint32_t primitiveCount = getPrimitiveCount(topology, vertexCount);
        recordDrawComplete(primitiveCount, vertexCount);
    }
    
    IORecursiveLockUnlock(lock);
    return error;
}

Pipeline3DError Intel3DPipeline::drawIndexedPrimitives(IntelPrimitiveTopology topology,
                                                      uint32_t indexStart,
                                                      uint32_t indexCount) {
    if (!currentState->indexBuffer || indexCount == 0) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    // Similar to drawPrimitives but uses index buffer
    return drawPrimitives(topology, indexStart, indexCount);
}

Pipeline3DError Intel3DPipeline::drawInstanced(IntelPrimitiveTopology topology,
                                              uint32_t vertexCount,
                                              uint32_t instanceCount) {
    if (instanceCount == 0) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    // Draw multiple instances
    for (uint32_t i = 0; i < instanceCount; i++) {
        Pipeline3DError error = drawPrimitives(topology, 0, vertexCount);
        if (error != PIPELINE_SUCCESS) {
            return error;
        }
    }
    
    return PIPELINE_SUCCESS;
}

// Clear Operations
Pipeline3DError Intel3DPipeline::clearRenderTarget(float r, float g, float b, float a) {
    if (!currentState->renderTarget) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    IntelRenderTarget* target = currentState->renderTarget;
    void* mapped = nullptr;
    if (!target->colorObject->mapCPU(&mapped) || !mapped) {
        IORecursiveLockUnlock(lock);
        return PIPELINE_MEMORY_ERROR;
    }
    
    // Convert float color to format
    uint32_t color;
    switch (target->colorFormat) {
        case TEX_FORMAT_RGBA8_UNORM:
            color = ((uint32_t)(a * 255) << 24) |
                   ((uint32_t)(b * 255) << 16) |
                   ((uint32_t)(g * 255) << 8) |
                   ((uint32_t)(r * 255));
            break;
        case TEX_FORMAT_BGRA8_UNORM:
            color = ((uint32_t)(a * 255) << 24) |
                   ((uint32_t)(r * 255) << 16) |
                   ((uint32_t)(g * 255) << 8) |
                   ((uint32_t)(b * 255));
            break;
        default:
            color = 0;
            break;
    }
    
    // Clear buffer
    uint32_t* pixels = (uint32_t*)mapped;
    uint32_t pixelCount = (target->colorPitch / 4) * target->height;
    for (uint32_t i = 0; i < pixelCount; i++) {
        pixels[i] = color;
    }
    
    target->colorObject->unmapCPU();
    
    IORecursiveLockUnlock(lock);
    return PIPELINE_SUCCESS;
}

Pipeline3DError Intel3DPipeline::clearDepthStencil(float depth, uint8_t stencil) {
    if (!currentState->renderTarget) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    IntelRenderTarget* target = currentState->renderTarget;
    void* mapped = nullptr;
    if (!target->depthObject->mapCPU(&mapped) || !mapped) {
        IORecursiveLockUnlock(lock);
        return PIPELINE_MEMORY_ERROR;
    }
    
    // Clear depth buffer
    if (target->depthFormat == TEX_FORMAT_DEPTH24_STENCIL8) {
        uint32_t* pixels = (uint32_t*)mapped;
        uint32_t depthValue = (uint32_t)(depth * 0xFFFFFF);
        uint32_t value = (stencil << 24) | depthValue;
        uint32_t pixelCount = (target->depthPitch / 4) * target->height;
        for (uint32_t i = 0; i < pixelCount; i++) {
            pixels[i] = value;
        }
    }
    
    target->depthObject->unmapCPU();
    
    IORecursiveLockUnlock(lock);
    return PIPELINE_SUCCESS;
}

// Synchronization
Pipeline3DError Intel3DPipeline::flush() {
    if (!renderRing) {
        return PIPELINE_STATE_ERROR;
    }
    
    uint32_t commands[16];
    uint32_t* cmd = commands;
    
    cmd = buildPipeControlCommand(cmd);
    
    IntelRequest* request = nullptr;
    Pipeline3DError error = submitPipelineCommand(commands,
                                                  (uint32_t)(cmd - commands),
                                                  &request);
    if (error == PIPELINE_SUCCESS && request) {
        waitForCompletion(request, 1000);
        request->release();
    }
    
    return error;
}

Pipeline3DError Intel3DPipeline::waitForIdle(uint32_t timeoutMs) {
    flush();
    
    // Wait for all operations to complete
    AbsoluteTime deadline;
    clock_interval_to_deadline(timeoutMs, kMillisecondScale, (uint64_t*)&deadline);
    
    while (!isIdle()) {
        AbsoluteTime now;
        clock_get_uptime((uint64_t*)&now);
        if (now >= deadline) {
            return PIPELINE_TIMEOUT;
        }
        IOSleep(1);
    }
    
    return PIPELINE_SUCCESS;
}

bool Intel3DPipeline::isIdle() {
    return renderRing ? renderRing->isIdle() : true;
}

// Statistics
void Intel3DPipeline::getStatistics(Pipeline3DStats* outStats) {
    if (!outStats) {
        return;
    }
    
    IORecursiveLockLock(lock);
    memcpy(outStats, &stats, sizeof(Pipeline3DStats));
    IORecursiveLockUnlock(lock);
}

void Intel3DPipeline::resetStatistics() {
    IORecursiveLockLock(lock);
    memset(&stats, 0, sizeof(Pipeline3DStats));
    IORecursiveLockUnlock(lock);
}

void Intel3DPipeline::printStatistics() {
    IOLog("Draw calls:          %llu\n", stats.drawCalls);
    IOLog("Primitives sent:     %llu\n", stats.primitivesSent);
    IOLog("Vertices processed:  %llu\n", stats.verticesProcessed);
    IOLog("Fragments generated: %llu\n", stats.fragmentsGenerated);
    IOLog("Shader invocations:  %llu\n", stats.shaderInvocations);
    IOLog("Pipeline stalls:     %llu\n", stats.pipelineStalls);
    IOLog("Average draw time:   %u us\n", stats.averageDrawTimeUs);
    IOLog("Max draw time:       %u us\n", stats.maxDrawTimeUs);
    IOLog("GPU utilization:     %u%%\n", stats.gpuUtilization);
    IOLog("Errors:              %llu\n", stats.errors);
}

// Hardware Capabilities
uint32_t Intel3DPipeline::getMaxTextureSize() {
    return 16384;  // Gen12: 16K x 16K
}

uint32_t Intel3DPipeline::getMaxRenderTargetSize() {
    return 16384;  // Gen12: 16K x 16K
}

uint32_t Intel3DPipeline::getMaxVertexBuffers() {
    return 16;
}

bool Intel3DPipeline::supportsGeometryShaders() {
    return true;  // Gen12 supports geometry shaders
}

bool Intel3DPipeline::supportsTessellation() {
    return true;  // Gen12 supports tessellation
}

bool Intel3DPipeline::supportsComputeShaders() {
    return true;  // Gen12 supports compute shaders
}

// Command Generation
uint32_t* Intel3DPipeline::buildPipelineSelectCommand(uint32_t* cmd,
                                                      IntelPipelineType type) {
    *cmd++ = GEN12_3DSTATE_PIPELINESELECT;
    *cmd++ = type;
    return cmd;
}

uint32_t* Intel3DPipeline::buildVertexShaderState(uint32_t* cmd,
                                                  IntelShaderProgram* program) {
    *cmd++ = GEN12_3DSTATE_VS | (8 - 2);
    *cmd++ = (uint32_t)(program->gpuAddress & 0xFFFFFFFF);
    *cmd++ = (uint32_t)(program->gpuAddress >> 32);
    *cmd++ = program->numThreads << 16;
    *cmd++ = program->scratchSize;
    *cmd++ = program->bindingTableOffset;
    *cmd++ = program->samplerStateOffset;
    *cmd++ = 0;
    return cmd;
}

uint32_t* Intel3DPipeline::buildFragmentShaderState(uint32_t* cmd,
                                                    IntelShaderProgram* program) {
    *cmd++ = GEN12_3DSTATE_PS | (12 - 2);
    *cmd++ = (uint32_t)(program->gpuAddress & 0xFFFFFFFF);
    *cmd++ = (uint32_t)(program->gpuAddress >> 32);
    *cmd++ = program->numThreads << 16;
    *cmd++ = program->scratchSize;
    *cmd++ = program->bindingTableOffset;
    *cmd++ = program->samplerStateOffset;
    *cmd++ = 0;
    *cmd++ = 0;
    *cmd++ = 0;
    *cmd++ = 0;
    *cmd++ = 0;
    return cmd;
}

uint32_t* Intel3DPipeline::buildViewportState(uint32_t* cmd, const float* viewport) {
    *cmd++ = GEN12_3DSTATE_VIEWPORT_STATE_POINTERS | (2 - 2);
    *cmd++ = 0;  // Viewport state offset (placeholder)
    return cmd;
}

uint32_t* Intel3DPipeline::buildScissorState(uint32_t* cmd, const uint32_t* scissor) {
    *cmd++ = GEN12_3DSTATE_SCISSOR_STATE_POINTERS | (2 - 2);
    *cmd++ = 0;  // Scissor state offset (placeholder)
    return cmd;
}

uint32_t* Intel3DPipeline::buildDepthBufferState(uint32_t* cmd,
                                                 IntelRenderTarget* target) {
    *cmd++ = GEN12_3DSTATE_DEPTH_BUFFER | (8 - 2);
    *cmd++ = target->depthFormat;
    *cmd++ = (uint32_t)(target->depthAddress & 0xFFFFFFFF);
    *cmd++ = (uint32_t)(target->depthAddress >> 32);
    *cmd++ = target->depthPitch;
    *cmd++ = target->width;
    *cmd++ = target->height;
    *cmd++ = 0;
    return cmd;
}

uint32_t* Intel3DPipeline::buildColorBufferState(uint32_t* cmd,
                                                 IntelRenderTarget* target) {
    // Build render target state (simplified)
    *cmd++ = 0x78000000 | (8 - 2);  // RENDER_SURFACE_STATE
    *cmd++ = target->colorFormat;
    *cmd++ = (uint32_t)(target->colorAddress & 0xFFFFFFFF);
    *cmd++ = (uint32_t)(target->colorAddress >> 32);
    *cmd++ = target->colorPitch;
    *cmd++ = target->width;
    *cmd++ = target->height;
    *cmd++ = 0;
    return cmd;
}

uint32_t* Intel3DPipeline::buildDrawCommand(uint32_t* cmd,
                                            IntelPrimitiveTopology topology,
                                            uint32_t vertexStart,
                                            uint32_t vertexCount) {
    *cmd++ = GEN12_3DPRIMITIVE | (7 - 2);
    *cmd++ = topology;
    *cmd++ = vertexCount;
    *cmd++ = vertexStart;
    *cmd++ = 1;  // Instance count
    *cmd++ = 0;  // Start instance
    *cmd++ = 0;  // Base vertex
    return cmd;
}

uint32_t* Intel3DPipeline::buildPipeControlCommand(uint32_t* cmd) {
    *cmd++ = GEN12_PIPE_CONTROL | (6 - 2);
    *cmd++ = 0x00100000;  // CS stall
    *cmd++ = 0;
    *cmd++ = 0;
    *cmd++ = 0;
    *cmd++ = 0;
    return cmd;
}

// Command Submission
Pipeline3DError Intel3DPipeline::submitPipelineCommand(uint32_t* commands,
                                                      uint32_t numDwords,
                                                      IntelRequest** requestOut) {
    if (!renderRing || !pipelineContext) {
        return PIPELINE_STATE_ERROR;
    }
    
    IntelRequest* request = new IntelRequest();
    if (!request || !request->init()) {
        return PIPELINE_MEMORY_ERROR;
    }
    
    if (!request->initWithContext(pipelineContext)) {
        request->release();
        return PIPELINE_MEMORY_ERROR;
    }
    
    // Submit to ring buffer
    bool success = renderRing->submitCommand(commands, numDwords, request);
    if (!success) {
        request->release();
        return PIPELINE_STATE_ERROR;
    }
    
    if (requestOut) {
        *requestOut = request;
        request->retain();
    }
    
    request->release();
    return PIPELINE_SUCCESS;
}

Pipeline3DError Intel3DPipeline::waitForCompletion(IntelRequest* request,
                                                  uint32_t timeoutMs) {
    if (!request) {
        return PIPELINE_INVALID_PARAMS;
    }
    
    bool completed = request->waitForCompletion(timeoutMs);
    return completed ? PIPELINE_SUCCESS : PIPELINE_TIMEOUT;
}

// Validation
bool Intel3DPipeline::validateShader(IntelShaderProgram* program) {
    return program && program->kernelObject && program->kernelSize > 0;
}

bool Intel3DPipeline::validateTexture(IntelTexture* texture) {
    return texture && texture->object && texture->width > 0 && texture->height > 0;
}

bool Intel3DPipeline::validateBuffer(IntelBuffer* buffer) {
    return buffer && buffer->object && buffer->size > 0;
}

bool Intel3DPipeline::validateRenderTarget(IntelRenderTarget* target) {
    return target && target->colorObject && target->depthObject &&
           target->width > 0 && target->height > 0;
}

bool Intel3DPipeline::validateDrawParams(uint32_t vertexStart, uint32_t vertexCount) {
    return vertexCount > 0 && currentState->vertexShader && currentState->fragmentShader;
}

// Helper Functions
uint32_t Intel3DPipeline::getTextureBytesPerPixel(IntelTextureFormat format) {
    switch (format) {
        case TEX_FORMAT_R8_UNORM: return 1;
        case TEX_FORMAT_RG8_UNORM: return 2;
        case TEX_FORMAT_RGBA8_UNORM:
        case TEX_FORMAT_BGRA8_UNORM:
        case TEX_FORMAT_DEPTH24_STENCIL8: return 4;
        case TEX_FORMAT_R16_FLOAT: return 2;
        case TEX_FORMAT_RG16_FLOAT: return 4;
        case TEX_FORMAT_RGBA16_FLOAT: return 8;
        case TEX_FORMAT_R32_FLOAT: return 4;
        case TEX_FORMAT_RG32_FLOAT: return 8;
        case TEX_FORMAT_RGBA32_FLOAT: return 16;
        default: return 4;
    }
}

uint32_t Intel3DPipeline::calculateTexturePitch(uint32_t width,
                                               IntelTextureFormat format) {
    uint32_t bpp = getTextureBytesPerPixel(format);
    uint32_t pitch = width * bpp;
    // Align to 64 bytes
    return (pitch + 63) & ~63;
}

uint32_t Intel3DPipeline::getPrimitiveCount(IntelPrimitiveTopology topology,
                                           uint32_t vertexCount) {
    switch (topology) {
        case TOPOLOGY_POINTS: return vertexCount;
        case TOPOLOGY_LINES: return vertexCount / 2;
        case TOPOLOGY_LINE_STRIP: return vertexCount - 1;
        case TOPOLOGY_TRIANGLES: return vertexCount / 3;
        case TOPOLOGY_TRIANGLE_STRIP:
        case TOPOLOGY_TRIANGLE_FAN: return vertexCount - 2;
        case TOPOLOGY_QUADS: return vertexCount / 4;
        case TOPOLOGY_QUAD_STRIP: return (vertexCount - 2) / 2;
        default: return 0;
    }
}

// Statistics
void Intel3DPipeline::recordDrawStart() {
    // Record timestamp for draw timing
}

void Intel3DPipeline::recordDrawComplete(uint32_t primitiveCount,
                                        uint32_t vertexCount) {
    stats.drawCalls++;
    stats.primitivesSent += primitiveCount;
    stats.verticesProcessed += vertexCount;
    stats.fragmentsGenerated += primitiveCount * 3;  // Rough estimate
    stats.shaderInvocations += vertexCount;
}
