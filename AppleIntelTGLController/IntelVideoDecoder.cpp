/*
 * IntelVideoDecoder.cpp
 * Intel Graphics Driver
 *
 * Video decode engine (VD Box) implementation.
 * Week 30 - Phase 7: Video Engine
 */

#include "IntelVideoDecoder.h"
#include "IntelUncore.h"
#include "IntelGEM.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelVideoDecoder, OSObject)

// Initialization
bool IntelVideoDecoder::init() {
    if (!super::init()) {
        return false;
    }
    
    m_controller = nullptr;
    m_ringBuffer = nullptr;
    m_running = false;
    m_numRefFrames = 0;
    m_lastError = DECODE_SUCCESS;
    m_tempBuffer = nullptr;
    m_sliceDataBuffer = nullptr;
    m_lastDecodeTime = 0;
    
    // Initialize reference frames
    for (uint32_t i = 0; i < MAX_REFERENCE_FRAMES; i++) {
        m_refFrames[i].buffer = nullptr;
        m_refFrames[i].type = REF_FRAME_NONE;
        m_refFrames[i].frameNum = 0;
        m_refFrames[i].pocLsb = 0;
        m_refFrames[i].used = false;
        m_refFrames[i].timestamp = 0;
    }
    
    // Initialize statistics
    memset(&m_stats, 0, sizeof(DecodeStatistics));
    memset(&m_capabilities, 0, sizeof(DecodeCapabilities));
    
    // Create locks
    m_refFrameLock = IOLockAlloc();
    m_statsLock = IOLockAlloc();
    if (!m_refFrameLock || !m_statsLock) {
        return false;
    }
    
    IOLog("IntelVideoDecoder::init() - Initialized\n");
    return true;
}

void IntelVideoDecoder::free() {
    stop();
    
    if (m_refFrameLock) {
        IOLockFree(m_refFrameLock);
        m_refFrameLock = nullptr;
    }
    
    if (m_statsLock) {
        IOLockFree(m_statsLock);
        m_statsLock = nullptr;
    }
    
    super::free();
}

bool IntelVideoDecoder::initWithController(AppleIntelTGLController* controller) {
    if (!controller) {
        return false;
    }
    
    m_controller = controller;
    
    // Get VCS ring buffer (video command streamer)
    m_ringBuffer = controller->getRingBuffer(RING_VCS0);
    if (!m_ringBuffer) {
        IOLog("IntelVideoDecoder: Failed to get VCS ring buffer\n");
        return false;
    }
    
    // Detect hardware capabilities
    detectCapabilities();
    
    IOLog("IntelVideoDecoder::initWithController() - Ready\n");
    return true;
}

// Lifecycle
bool IntelVideoDecoder::start() {
    if (m_running) {
        return true;
    }
    
    // Allocate internal buffers
    DecodeError error = allocateInternalBuffers();
    if (error != DECODE_SUCCESS) {
        IOLog("IntelVideoDecoder: Failed to allocate buffers: %s\n",
              getErrorString(error));
        return false;
    }
    
    // Enable hardware
    if (!enableHardware()) {
        IOLog("IntelVideoDecoder: Failed to enable hardware\n");
        freeInternalBuffers();
        return false;
    }
    
    m_running = true;
    IOLog("IntelVideoDecoder::start() - Video decoder started\n");
    return true;
}

void IntelVideoDecoder::stop() {
    if (!m_running) {
        return;
    }
    
    // Wait for completion
    waitForCompletion(DECODE_TIMEOUT_MS);
    
    // Disable hardware
    disableHardware();
    
    // Clear reference frames
    clearReferenceFrames();
    
    // Free buffers
    freeInternalBuffers();
    
    m_running = false;
    IOLog("IntelVideoDecoder::stop() - Video decoder stopped\n");
}

// Decode operations
DecodeError IntelVideoDecoder::decode(const DecodeParams* params) {
    if (!m_running) {
        m_lastError = DECODE_ERROR_INVALID_PARAMS;
        return m_lastError;
    }
    
    if (!params) {
        m_lastError = DECODE_ERROR_INVALID_PARAMS;
        return m_lastError;
    }
    
    // Validate parameters
    DecodeError error = validateParams(params);
    if (error != DECODE_SUCCESS) {
        m_lastError = error;
        return error;
    }
    
    uint64_t startTime = mach_absolute_time();
    
    // Setup reference frames
    error = setupReferenceFrames(params);
    if (error != DECODE_SUCCESS) {
        m_lastError = error;
        return error;
    }
    
    // Decode based on codec
    switch (params->codec) {
        case VIDEO_CODEC_H264:
            error = decodeH264(params);
            break;
        case VIDEO_CODEC_H265:
            error = decodeH265(params);
            break;
        case VIDEO_CODEC_VP9:
            error = decodeVP9(params);
            break;
        case VIDEO_CODEC_JPEG:
            error = decodeJPEG(params);
            break;
        default:
            error = DECODE_ERROR_UNSUPPORTED_CODEC;
            break;
    }
    
    // Update statistics
    uint64_t endTime = mach_absolute_time();
    uint64_t decodeTime = endTime - startTime;
    updateStatistics(decodeTime, error);
    
    m_lastError = error;
    return error;
}

DecodeError IntelVideoDecoder::decodeFrame(IntelVideoCodec codec,
                                          IntelGEMObject* input,
                                          IntelGEMObject* output,
                                          const VideoFormat* format) {
    if (!input || !output || !format) {
        return DECODE_ERROR_INVALID_PARAMS;
    }
    
    // Setup decode parameters for frame decode
    DecodeParams params;
    memset(&params, 0, sizeof(params));
    
    params.codec = codec;
    params.operation = DECODE_OP_FRAME;
    params.format = *format;
    params.bitstreamBuffer = input;
    params.bitstreamOffset = 0;
    params.bitstreamSize = input->getSize();
    params.outputBuffer = output;
    params.outputOffset = 0;
    params.numRefFrames = 0;
    params.deblocking = true;
    params.errorConcealment = true;
    
    if (format) {
        params.format = *format;
    }
    
    return decode(&params);
}

DecodeError IntelVideoDecoder::decodeSlice(const DecodeParams* params,
                                          uint32_t sliceIndex) {
    if (!params || sliceIndex >= MAX_SLICES_PER_FRAME) {
        return DECODE_ERROR_INVALID_PARAMS;
    }
    
    // Setup slice-specific parameters
    DecodeError error = setupSliceParams(params, sliceIndex);
    if (error != DECODE_SUCCESS) {
        return error;
    }
    
    // Process slice
    return processSlice(sliceIndex);
}

void IntelVideoDecoder::waitForCompletion(uint32_t timeoutMs) {
    if (!m_running) {
        return;
    }
    
    uint64_t startTime = mach_absolute_time();
    uint64_t timeoutNs = timeoutMs * 1000000ULL;
    
    while (!isHardwareIdle()) {
        uint64_t currentTime = mach_absolute_time();
        if ((currentTime - startTime) > timeoutNs) {
            IOLog("IntelVideoDecoder: Decode timeout\n");
            m_lastError = DECODE_ERROR_TIMEOUT;
            resetHardware();
            break;
        }
        IOSleep(1);
    }
}

void IntelVideoDecoder::flush() {
    waitForCompletion(DECODE_TIMEOUT_MS);
    clearReferenceFrames();
}

// Reference frame management
DecodeError IntelVideoDecoder::addReferenceFrame(IntelGEMObject* buffer,
                                                IntelRefFrameType type,
                                                uint32_t frameNum) {
    if (!buffer || type == REF_FRAME_NONE) {
        return DECODE_ERROR_INVALID_PARAMS;
    }
    
    IOLockLock(m_refFrameLock);
    
    if (m_numRefFrames >= MAX_REFERENCE_FRAMES) {
        IOLockUnlock(m_refFrameLock);
        return DECODE_ERROR_TOO_MANY_REFS;
    }
    
    // Find free slot
    for (uint32_t i = 0; i < MAX_REFERENCE_FRAMES; i++) {
        if (!m_refFrames[i].used) {
            m_refFrames[i].buffer = buffer;
            m_refFrames[i].type = type;
            m_refFrames[i].frameNum = frameNum;
            m_refFrames[i].used = true;
            m_refFrames[i].timestamp = mach_absolute_time();
            m_numRefFrames++;
            IOLockUnlock(m_refFrameLock);
            return DECODE_SUCCESS;
        }
    }
    
    IOLockUnlock(m_refFrameLock);
    return DECODE_ERROR_TOO_MANY_REFS;
}

DecodeError IntelVideoDecoder::removeReferenceFrame(uint32_t frameNum) {
    IOLockLock(m_refFrameLock);
    
    ReferenceFrame* ref = findReferenceFrame(frameNum);
    if (ref) {
        ref->buffer = nullptr;
        ref->type = REF_FRAME_NONE;
        ref->used = false;
        m_numRefFrames--;
        IOLockUnlock(m_refFrameLock);
        return DECODE_SUCCESS;
    }
    
    IOLockUnlock(m_refFrameLock);
    return DECODE_ERROR_INVALID_PARAMS;
}

DecodeError IntelVideoDecoder::clearReferenceFrames() {
    IOLockLock(m_refFrameLock);
    
    for (uint32_t i = 0; i < MAX_REFERENCE_FRAMES; i++) {
        m_refFrames[i].buffer = nullptr;
        m_refFrames[i].type = REF_FRAME_NONE;
        m_refFrames[i].used = false;
    }
    m_numRefFrames = 0;
    
    IOLockUnlock(m_refFrameLock);
    return DECODE_SUCCESS;
}

ReferenceFrame* IntelVideoDecoder::getReferenceFrame(uint32_t index) {
    if (index >= MAX_REFERENCE_FRAMES) {
        return nullptr;
    }
    return &m_refFrames[index];
}

// Format support
bool IntelVideoDecoder::isCodecSupported(IntelVideoCodec codec) const {
    switch (codec) {
        case VIDEO_CODEC_H264:
            return m_capabilities.h264Supported;
        case VIDEO_CODEC_H265:
            return m_capabilities.h265Supported;
        case VIDEO_CODEC_VP9:
            return m_capabilities.vp9Supported;
        case VIDEO_CODEC_JPEG:
            return m_capabilities.jpegSupported;
        case VIDEO_CODEC_AV1:
            return m_capabilities.av1Supported;
        default:
            return false;
    }
}

bool IntelVideoDecoder::isProfileSupported(IntelVideoProfile profile) const {
    // Gen12+ supports all common profiles
    return true;
}

bool IntelVideoDecoder::isFormatSupported(const VideoFormat* format) const {
    if (!format) {
        return false;
    }
    
    // Check dimensions
    if (format->width > m_capabilities.maxWidth ||
        format->height > m_capabilities.maxHeight) {
        return false;
    }
    
    // Check bit depth
    if (format->bitDepth == 10 && !m_capabilities.bit10Supported) {
        return false;
    }
    if (format->bitDepth == 12 && !m_capabilities.bit12Supported) {
        return false;
    }
    
    // Check interlaced
    if (format->interlaced && !m_capabilities.interlacedSupported) {
        return false;
    }
    
    return true;
}

DecodeError IntelVideoDecoder::validateParams(const DecodeParams* params) {
    if (!params) {
        return DECODE_ERROR_INVALID_PARAMS;
    }
    
    // Check codec support
    if (!isCodecSupported(params->codec)) {
        return DECODE_ERROR_UNSUPPORTED_CODEC;
    }
    
    // Check profile support
    if (!isProfileSupported(params->profile)) {
        return DECODE_ERROR_UNSUPPORTED_PROFILE;
    }
    
    // Check format
    if (!isFormatSupported(&params->format)) {
        return DECODE_ERROR_INVALID_PARAMS;
    }
    
    // Check buffers
    if (!params->bitstreamBuffer || !params->outputBuffer) {
        return DECODE_ERROR_INVALID_PARAMS;
    }
    
    // Check reference frames
    if (params->numRefFrames > MAX_REFERENCE_FRAMES) {
        return DECODE_ERROR_TOO_MANY_REFS;
    }
    
    return DECODE_SUCCESS;
}

// Statistics
void IntelVideoDecoder::getStatistics(DecodeStatistics* stats) {
    if (!stats) {
        return;
    }
    
    IOLockLock(m_statsLock);
    *stats = m_stats;
    IOLockUnlock(m_statsLock);
}

void IntelVideoDecoder::resetStatistics() {
    IOLockLock(m_statsLock);
    memset(&m_stats, 0, sizeof(DecodeStatistics));
    IOLockUnlock(m_statsLock);
}

void IntelVideoDecoder::printStatistics() {
    DecodeStatistics stats;
    getStatistics(&stats);
    
    IOLog("IntelVideoDecoder Statistics:\n");
    IOLog("  Performance:\n");
    IOLog("    Frames decoded: %llu\n", stats.framesDecoded);
    IOLog("    Slices decoded: %llu\n", stats.slicesDecoded);
    IOLog("    Macroblocks: %llu\n", stats.macroblocks);
    IOLog("    Average time: %llu ns\n", stats.averageTime);
    IOLog("    FPS: %.2f\n", stats.framesPerSecond);
    IOLog("  Quality:\n");
    IOLog("    Errors: %u\n", stats.errors);
    IOLog("    Errors corrected: %u\n", stats.errorsCorrected);
    IOLog("    Errors concealed: %u\n", stats.errorConcealed);
    IOLog("  Hardware:\n");
    IOLog("    VD box usage: %.1f%%\n", stats.hardwareUsage);
    IOLog("    Bandwidth usage: %.1f%%\n", stats.bandwidthUsage);
    IOLog("  Codecs:\n");
    IOLog("    H.264: %llu frames\n", stats.h264Frames);
    IOLog("    H.265: %llu frames\n", stats.h265Frames);
    IOLog("    VP9: %llu frames\n", stats.vp9Frames);
    IOLog("    JPEG: %llu frames\n", stats.jpegFrames);
}

// Capabilities
void IntelVideoDecoder::getCapabilities(DecodeCapabilities* caps) {
    if (!caps) {
        return;
    }
    *caps = m_capabilities;
}

uint32_t IntelVideoDecoder::getMaxWidth() const {
    return m_capabilities.maxWidth;
}

uint32_t IntelVideoDecoder::getMaxHeight() const {
    return m_capabilities.maxHeight;
}

// Hardware control
bool IntelVideoDecoder::enableHardware() {
    // Enable VD box
    uint32_t control = readRegister(VD_CONTROL);
    control |= VD_CONTROL_ENABLE;
    writeRegister(VD_CONTROL, control);
    
    // Wait for ready
    for (int i = 0; i < 100; i++) {
        uint32_t status = readRegister(VD_STATUS);
        if (status & VD_STATUS_IDLE) {
            IOLog("IntelVideoDecoder: Hardware enabled\n");
            return true;
        }
        IOSleep(1);
    }
    
    IOLog("IntelVideoDecoder: Hardware enable timeout\n");
    return false;
}

void IntelVideoDecoder::disableHardware() {
    // Disable VD box
    uint32_t control = readRegister(VD_CONTROL);
    control &= ~VD_CONTROL_ENABLE;
    writeRegister(VD_CONTROL, control);
    
    IOLog("IntelVideoDecoder: Hardware disabled\n");
}

void IntelVideoDecoder::resetHardware() {
    // Reset VD box
    uint32_t control = readRegister(VD_CONTROL);
    control |= VD_CONTROL_RESET;
    writeRegister(VD_CONTROL, control);
    
    IOSleep(1);
    
    control &= ~VD_CONTROL_RESET;
    writeRegister(VD_CONTROL, control);
    
    // Wait for idle
    for (int i = 0; i < 100; i++) {
        uint32_t status = readRegister(VD_STATUS);
        if (status & VD_STATUS_IDLE) {
            IOLog("IntelVideoDecoder: Hardware reset complete\n");
            return;
        }
        IOSleep(1);
    }
    
    IOLog("IntelVideoDecoder: Hardware reset timeout\n");
}

bool IntelVideoDecoder::isHardwareIdle() {
    uint32_t status = readRegister(VD_STATUS);
    return (status & VD_STATUS_IDLE) != 0;
}

// Error handling
const char* IntelVideoDecoder::getErrorString(DecodeError error) {
    switch (error) {
        case DECODE_SUCCESS:
            return "Success";
        case DECODE_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case DECODE_ERROR_UNSUPPORTED_CODEC:
            return "Unsupported codec";
        case DECODE_ERROR_UNSUPPORTED_PROFILE:
            return "Unsupported profile";
        case DECODE_ERROR_INVALID_BITSTREAM:
            return "Invalid bitstream";
        case DECODE_ERROR_NO_MEMORY:
            return "Out of memory";
        case DECODE_ERROR_HARDWARE_FAULT:
            return "Hardware fault";
        case DECODE_ERROR_TIMEOUT:
            return "Timeout";
        case DECODE_ERROR_TOO_MANY_REFS:
            return "Too many reference frames";
        case DECODE_ERROR_BUFFER_TOO_SMALL:
            return "Buffer too small";
        default:
            return "Unknown error";
    }
}

// Private methods

void IntelVideoDecoder::writeRegister(uint32_t reg, uint32_t value) {
    if (m_controller && m_controller->getUncore()) {
        m_controller->getUncore()->writeRegister32(reg, value);
    }
}

uint32_t IntelVideoDecoder::readRegister(uint32_t reg) {
    if (m_controller && m_controller->getUncore()) {
        return m_controller->getUncore()->readRegister32(reg);
    }
    return 0;
}

void IntelVideoDecoder::setCodecMode(IntelVideoCodec codec,
                                    IntelVideoProfile profile) {
    uint32_t control = readRegister(VD_CONTROL);
    
    // Clear codec and profile fields
    control &= ~(VD_CONTROL_CODEC_MASK | VD_CONTROL_PROFILE_MASK);
    
    // Set codec
    switch (codec) {
        case VIDEO_CODEC_H264:
            control |= VD_CONTROL_CODEC_H264;
            break;
        case VIDEO_CODEC_H265:
            control |= VD_CONTROL_CODEC_H265;
            break;
        case VIDEO_CODEC_VP9:
            control |= VD_CONTROL_CODEC_VP9;
            break;
        case VIDEO_CODEC_JPEG:
            control |= VD_CONTROL_CODEC_JPEG;
            break;
        default:
            break;
    }
    
    // Set profile
    control |= (profile << 8);
    
    writeRegister(VD_CONTROL, control);
}

// Decode implementation
DecodeError IntelVideoDecoder::decodeH264(const DecodeParams* params) {
    // Set H.264 mode
    setCodecMode(VIDEO_CODEC_H264, params->profile);
    
    // Setup bitstream pointer
    uint64_t bitstreamAddr = params->bitstreamBuffer->getGPUAddress() +
                            params->bitstreamOffset;
    writeRegister(VD_BITSTREAM_PTR, (uint32_t)bitstreamAddr);
    writeRegister(VD_BITSTREAM_SIZE, params->bitstreamSize);
    
    // Setup output buffer
    uint64_t outputAddr = params->outputBuffer->getGPUAddress() +
                         params->outputOffset;
    writeRegister(VD_OUTPUT_PTR, (uint32_t)outputAddr);
    writeRegister(VD_OUTPUT_STRIDE, params->format.stride);
    
    // Start decode
    writeRegister(VD_COMMAND, 1);
    
    // Wait for completion
    waitForCompletion(DECODE_TIMEOUT_MS);
    
    // Check status
    uint32_t status = readRegister(VD_STATUS);
    if (status & VD_STATUS_ERROR) {
        return DECODE_ERROR_HARDWARE_FAULT;
    }
    if (status & VD_STATUS_TIMEOUT) {
        return DECODE_ERROR_TIMEOUT;
    }
    
    IOLockLock(m_statsLock);
    m_stats.h264Frames++;
    IOLockUnlock(m_statsLock);
    
    return DECODE_SUCCESS;
}

DecodeError IntelVideoDecoder::decodeH265(const DecodeParams* params) {
    // Set H.265 mode
    setCodecMode(VIDEO_CODEC_H265, params->profile);
    
    // Setup buffers (similar to H.264)
    uint64_t bitstreamAddr = params->bitstreamBuffer->getGPUAddress() +
                            params->bitstreamOffset;
    writeRegister(VD_BITSTREAM_PTR, (uint32_t)bitstreamAddr);
    writeRegister(VD_BITSTREAM_SIZE, params->bitstreamSize);
    
    uint64_t outputAddr = params->outputBuffer->getGPUAddress() +
                         params->outputOffset;
    writeRegister(VD_OUTPUT_PTR, (uint32_t)outputAddr);
    writeRegister(VD_OUTPUT_STRIDE, params->format.stride);
    
    // Start decode
    writeRegister(VD_COMMAND, 1);
    
    // Wait and check
    waitForCompletion(DECODE_TIMEOUT_MS);
    
    uint32_t status = readRegister(VD_STATUS);
    if (status & VD_STATUS_ERROR) {
        return DECODE_ERROR_HARDWARE_FAULT;
    }
    
    IOLockLock(m_statsLock);
    m_stats.h265Frames++;
    IOLockUnlock(m_statsLock);
    
    return DECODE_SUCCESS;
}

DecodeError IntelVideoDecoder::decodeVP9(const DecodeParams* params) {
    // Set VP9 mode
    setCodecMode(VIDEO_CODEC_VP9, params->profile);
    
    // Setup buffers
    uint64_t bitstreamAddr = params->bitstreamBuffer->getGPUAddress() +
                            params->bitstreamOffset;
    writeRegister(VD_BITSTREAM_PTR, (uint32_t)bitstreamAddr);
    writeRegister(VD_BITSTREAM_SIZE, params->bitstreamSize);
    
    uint64_t outputAddr = params->outputBuffer->getGPUAddress() +
                         params->outputOffset;
    writeRegister(VD_OUTPUT_PTR, (uint32_t)outputAddr);
    writeRegister(VD_OUTPUT_STRIDE, params->format.stride);
    
    // Start decode
    writeRegister(VD_COMMAND, 1);
    
    // Wait and check
    waitForCompletion(DECODE_TIMEOUT_MS);
    
    uint32_t status = readRegister(VD_STATUS);
    if (status & VD_STATUS_ERROR) {
        return DECODE_ERROR_HARDWARE_FAULT;
    }
    
    IOLockLock(m_statsLock);
    m_stats.vp9Frames++;
    IOLockUnlock(m_statsLock);
    
    return DECODE_SUCCESS;
}

DecodeError IntelVideoDecoder::decodeJPEG(const DecodeParams* params) {
    // Set JPEG mode
    setCodecMode(VIDEO_CODEC_JPEG, params->profile);
    
    // JPEG is simpler - single image decode
    uint64_t bitstreamAddr = params->bitstreamBuffer->getGPUAddress() +
                            params->bitstreamOffset;
    writeRegister(VD_BITSTREAM_PTR, (uint32_t)bitstreamAddr);
    writeRegister(VD_BITSTREAM_SIZE, params->bitstreamSize);
    
    uint64_t outputAddr = params->outputBuffer->getGPUAddress() +
                         params->outputOffset;
    writeRegister(VD_OUTPUT_PTR, (uint32_t)outputAddr);
    writeRegister(VD_OUTPUT_STRIDE, params->format.stride);
    
    // Start decode
    writeRegister(VD_COMMAND, 1);
    
    // Wait and check
    waitForCompletion(DECODE_TIMEOUT_MS);
    
    uint32_t status = readRegister(VD_STATUS);
    if (status & VD_STATUS_ERROR) {
        return DECODE_ERROR_HARDWARE_FAULT;
    }
    
    IOLockLock(m_statsLock);
    m_stats.jpegFrames++;
    IOLockUnlock(m_statsLock);
    
    return DECODE_SUCCESS;
}

// Slice processing
DecodeError IntelVideoDecoder::setupSliceParams(const DecodeParams* params,
                                               uint32_t sliceIndex) {
    if (sliceIndex >= MAX_SLICES_PER_FRAME) {
        return DECODE_ERROR_INVALID_PARAMS;
    }
    
    // Write slice parameters to hardware
    uint32_t sliceReg = VD_SLICE_PARAMS + (sliceIndex * 16);
    writeRegister(sliceReg + 0, params->sliceOffset);
    writeRegister(sliceReg + 4, params->sliceSize);
    writeRegister(sliceReg + 8, params->mbX | (params->mbY << 16));
    writeRegister(sliceReg + 12, params->quantization);
    
    return DECODE_SUCCESS;
}

DecodeError IntelVideoDecoder::processSlice(uint32_t sliceIndex) {
    // Process single slice
    writeRegister(VD_COMMAND, (2 << 16) | sliceIndex);
    
    // Wait for slice completion
    waitForCompletion(DECODE_TIMEOUT_MS / 10);
    
    uint32_t status = readRegister(VD_STATUS);
    if (status & VD_STATUS_ERROR) {
        return DECODE_ERROR_HARDWARE_FAULT;
    }
    
    IOLockLock(m_statsLock);
    m_stats.slicesDecoded++;
    IOLockUnlock(m_statsLock);
    
    return DECODE_SUCCESS;
}

// Reference frame helpers
ReferenceFrame* IntelVideoDecoder::findReferenceFrame(uint32_t frameNum) {
    for (uint32_t i = 0; i < MAX_REFERENCE_FRAMES; i++) {
        if (m_refFrames[i].used && m_refFrames[i].frameNum == frameNum) {
            return &m_refFrames[i];
        }
    }
    return nullptr;
}

DecodeError IntelVideoDecoder::setupReferenceFrames(const DecodeParams* params) {
    // Program reference frame addresses
    for (uint32_t i = 0; i < params->numRefFrames; i++) {
        if (params->refFrames[i]) {
            uint64_t addr = params->refFrames[i]->getGPUAddress();
            uint32_t reg = VD_REF_FRAME_BASE + (i * 8);
            writeRegister(reg, (uint32_t)addr);
            writeRegister(reg + 4, (uint32_t)(addr >> 32));
        }
    }
    
    return DECODE_SUCCESS;
}

void IntelVideoDecoder::updateReferenceFrames(uint32_t currentFrameNum) {
    IOLockLock(m_refFrameLock);
    
    // Age out old frames
    uint64_t currentTime = mach_absolute_time();
    for (uint32_t i = 0; i < MAX_REFERENCE_FRAMES; i++) {
        if (m_refFrames[i].used) {
            // Remove frames older than 1 second
            if ((currentTime - m_refFrames[i].timestamp) > 1000000000ULL) {
                m_refFrames[i].used = false;
                m_refFrames[i].buffer = nullptr;
                m_numRefFrames--;
            }
        }
    }
    
    IOLockUnlock(m_refFrameLock);
}

// Buffer management
DecodeError IntelVideoDecoder::allocateInternalBuffers() {
    // Allocate temporary decode buffer (4MB)
    m_tempBuffer = m_controller->getGEM()->createObject(4 * 1024 * 1024);
    if (!m_tempBuffer) {
        return DECODE_ERROR_NO_MEMORY;
    }
    
    // Allocate slice data buffer (1MB)
    m_sliceDataBuffer = m_controller->getGEM()->createObject(1 * 1024 * 1024);
    if (!m_sliceDataBuffer) {
        m_controller->getGEM()->destroyObject(m_tempBuffer);
        m_tempBuffer = nullptr;
        return DECODE_ERROR_NO_MEMORY;
    }
    
    return DECODE_SUCCESS;
}

void IntelVideoDecoder::freeInternalBuffers() {
    if (m_tempBuffer) {
        m_controller->getGEM()->destroyObject(m_tempBuffer);
        m_tempBuffer = nullptr;
    }
    
    if (m_sliceDataBuffer) {
        m_controller->getGEM()->destroyObject(m_sliceDataBuffer);
        m_sliceDataBuffer = nullptr;
    }
}

// Statistics tracking
void IntelVideoDecoder::updateStatistics(uint64_t decodeTime, DecodeError error) {
    IOLockLock(m_statsLock);
    
    m_stats.framesDecoded++;
    m_stats.totalTime += decodeTime;
    m_stats.averageTime = m_stats.totalTime / m_stats.framesDecoded;
    
    // Calculate FPS (assuming 1GHz timebase)
    if (decodeTime > 0) {
        m_stats.framesPerSecond = 1000000000.0f / (float)decodeTime;
    }
    
    if (error != DECODE_SUCCESS) {
        m_stats.errors++;
    }
    
    // Estimate hardware usage (simplified)
    m_stats.hardwareUsage = (decodeTime > 16666666) ? 100.0f :
                           (float)decodeTime / 16666666.0f * 100.0f;
    
    m_lastDecodeTime = decodeTime;
    
    IOLockUnlock(m_statsLock);
}

// Hardware detection
void IntelVideoDecoder::detectCapabilities() {
    // Gen12 (Tiger Lake) capabilities
    m_capabilities.h264Supported = true;
    m_capabilities.h265Supported = true;
    m_capabilities.vp9Supported = true;
    m_capabilities.jpegSupported = true;
    m_capabilities.av1Supported = false;  // Gen12.5+
    
    m_capabilities.maxWidth = 4096;
    m_capabilities.maxHeight = 4096;
    m_capabilities.maxRefFrames = 16;
    m_capabilities.maxBitrate = 100;  // 100 Mbps
    
    m_capabilities.bit10Supported = true;
    m_capabilities.bit12Supported = false;
    m_capabilities.interlacedSupported = true;
    m_capabilities.concurrentDecode = false;
    
    IOLog("IntelVideoDecoder: Capabilities detected\n");
    IOLog("  H.264: %s\n", m_capabilities.h264Supported ? "Yes" : "No");
    IOLog("  H.265: %s\n", m_capabilities.h265Supported ? "Yes" : "No");
    IOLog("  VP9: %s\n", m_capabilities.vp9Supported ? "Yes" : "No");
    IOLog("  Max resolution: %ux%u\n",
          m_capabilities.maxWidth, m_capabilities.maxHeight);
}
