/*
 * IntelVideoEncoder.cpp
 * Intel Graphics Driver
 *
 * Video encode engine (VE Box) implementation.
 * Week 31 - Phase 7: Video Engine
 */

#include "IntelVideoEncoder.h"
#include "IntelUncore.h"
#include "IntelGEM.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelVideoEncoder, OSObject)

// Initialization
bool IntelVideoEncoder::init() {
    if (!super::init()) {
        return false;
    }
    
    m_controller = nullptr;
    m_ringBuffer = nullptr;
    m_running = false;
    m_lastError = ENCODE_SUCCESS;
    m_tempBuffer = nullptr;
    m_bitstreamBuffer = nullptr;
    m_lastEncodeTime = 0;
    m_frameCount = 0;
    m_gopPosition = 0;
    m_numRefFrames = 0;
    
    // Initialize reference frames
    for (uint32_t i = 0; i < 4; i++) {
        m_refFrames[i] = nullptr;
    }
    
    // Initialize statistics
    memset(&m_stats, 0, sizeof(EncodeStatistics));
    memset(&m_capabilities, 0, sizeof(EncodeCapabilities));
    
    // Initialize rate control
    memset(&m_rateControl, 0, sizeof(RateControlParams));
    m_rateControl.mode = RATE_CONTROL_VBR;
    m_rateControl.targetBitrate = 5000;  // 5 Mbps default
    m_rateControl.maxBitrate = 10000;
    m_rateControl.minBitrate = 1000;
    m_rateControl.gopSize = 30;
    m_rateControl.idrInterval = 120;
    m_rateControl.bFrames = 0;
    
    // Initialize quality
    memset(&m_quality, 0, sizeof(QualityParams));
    m_quality.preset = QUALITY_BALANCED;
    m_quality.qpI = 26;
    m_quality.qpP = 28;
    m_quality.qpB = 30;
    m_quality.qpMin = 10;
    m_quality.qpMax = 51;
    m_quality.adaptiveQuantization = true;
    m_quality.cabac = true;
    
    // Create locks
    m_refFrameLock = IOLockAlloc();
    m_statsLock = IOLockAlloc();
    if (!m_refFrameLock || !m_statsLock) {
        return false;
    }
    
    IOLog("IntelVideoEncoder::init() - Initialized\n");
    return true;
}

void IntelVideoEncoder::free() {
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

bool IntelVideoEncoder::initWithController(AppleIntelTGLController* controller) {
    if (!controller) {
        return false;
    }
    
    m_controller = controller;
    
    // Get VCS ring buffer (video command streamer)
    m_ringBuffer = controller->getRingBuffer(RING_VCS0);
    if (!m_ringBuffer) {
        IOLog("IntelVideoEncoder: Failed to get VCS ring buffer\n");
        return false;
    }
    
    // Detect hardware capabilities
    detectCapabilities();
    
    IOLog("IntelVideoEncoder::initWithController() - Ready\n");
    return true;
}

// Lifecycle
bool IntelVideoEncoder::start() {
    if (m_running) {
        return true;
    }
    
    // Allocate internal buffers
    EncodeError error = allocateInternalBuffers();
    if (error != ENCODE_SUCCESS) {
        IOLog("IntelVideoEncoder: Failed to allocate buffers: %s\n",
              getErrorString(error));
        return false;
    }
    
    // Enable hardware
    if (!enableHardware()) {
        IOLog("IntelVideoEncoder: Failed to enable hardware\n");
        freeInternalBuffers();
        return false;
    }
    
    m_running = true;
    IOLog("IntelVideoEncoder::start() - Video encoder started\n");
    return true;
}

void IntelVideoEncoder::stop() {
    if (!m_running) {
        return;
    }
    
    // Wait for completion
    waitForCompletion(ENCODE_TIMEOUT_MS);
    
    // Disable hardware
    disableHardware();
    
    // Free buffers
    freeInternalBuffers();
    
    m_running = false;
    IOLog("IntelVideoEncoder::stop() - Video encoder stopped\n");
}

// Encode operations
EncodeError IntelVideoEncoder::encode(const EncodeParams* params,
                                     uint32_t* outputSize) {
    if (!m_running) {
        m_lastError = ENCODE_ERROR_INVALID_PARAMS;
        return m_lastError;
    }
    
    if (!params || !outputSize) {
        m_lastError = ENCODE_ERROR_INVALID_PARAMS;
        return m_lastError;
    }
    
    // Validate parameters
    EncodeError error = validateParams(params);
    if (error != ENCODE_SUCCESS) {
        m_lastError = error;
        return error;
    }
    
    uint64_t startTime = mach_absolute_time();
    
    // Setup reference frames
    error = setupReferenceFrames(params);
    if (error != ENCODE_SUCCESS) {
        m_lastError = error;
        return error;
    }
    
    // Encode based on codec
    switch (params->codec) {
        case ENCODE_CODEC_H264:
            error = encodeH264(params, outputSize);
            break;
        case ENCODE_CODEC_H265:
            error = encodeH265(params, outputSize);
            break;
        case ENCODE_CODEC_VP9:
            error = encodeVP9(params, outputSize);
            break;
        case ENCODE_CODEC_JPEG:
            error = encodeJPEG(params, outputSize);
            break;
        default:
            error = ENCODE_ERROR_UNSUPPORTED_CODEC;
            break;
    }
    
    // Update statistics
    uint64_t endTime = mach_absolute_time();
    uint64_t encodeTime = endTime - startTime;
    updateStatistics(encodeTime, *outputSize, error);
    
    m_lastError = error;
    return error;
}

EncodeError IntelVideoEncoder::encodeFrame(IntelEncodeCodec codec,
                                          IntelGEMObject* input,
                                          IntelGEMObject* output,
                                          const EncodeFormat* format,
                                          uint32_t* outputSize) {
    EncodeParams params;
    memset(&params, 0, sizeof(EncodeParams));
    
    params.codec = codec;
    params.inputBuffer = input;
    params.inputOffset = 0;
    params.outputBuffer = output;
    params.outputOffset = 0;
    params.outputMaxSize = output->getSize();
    params.frameType = determineFrameType(m_frameCount);
    params.frameNumber = m_frameCount++;
    
    if (format) {
        params.format = *format;
    }
    
    params.rateControl = m_rateControl;
    params.quality = m_quality;
    
    return encode(&params, outputSize);
}

void IntelVideoEncoder::waitForCompletion(uint32_t timeoutMs) {
    if (!m_running) {
        return;
    }
    
    uint64_t startTime = mach_absolute_time();
    uint64_t timeoutNs = timeoutMs * 1000000ULL;
    
    while (!isHardwareIdle()) {
        uint64_t currentTime = mach_absolute_time();
        if ((currentTime - startTime) > timeoutNs) {
            IOLog("IntelVideoEncoder: Encode timeout\n");
            m_lastError = ENCODE_ERROR_TIMEOUT;
            resetHardware();
            break;
        }
        IOSleep(1);
    }
}

void IntelVideoEncoder::flush() {
    waitForCompletion(ENCODE_TIMEOUT_MS);
    m_gopPosition = 0;
}

// Rate control
EncodeError IntelVideoEncoder::setRateControl(const RateControlParams* params) {
    if (!params) {
        return ENCODE_ERROR_INVALID_PARAMS;
    }
    
    m_rateControl = *params;
    
    // Program hardware
    if (m_running) {
        writeRegister(VE_BITRATE, params->targetBitrate);
        writeRegister(VE_GOP_SIZE, params->gopSize);
        setRateControlMode(params->mode);
    }
    
    return ENCODE_SUCCESS;
}

void IntelVideoEncoder::getRateControl(RateControlParams* params) {
    if (params) {
        *params = m_rateControl;
    }
}

EncodeError IntelVideoEncoder::updateBitrate(uint32_t bitrate) {
    m_rateControl.targetBitrate = bitrate;
    
    if (m_running) {
        writeRegister(VE_BITRATE, bitrate);
    }
    
    return ENCODE_SUCCESS;
}

// Quality control
EncodeError IntelVideoEncoder::setQuality(const QualityParams* params) {
    if (!params) {
        return ENCODE_ERROR_INVALID_PARAMS;
    }
    
    m_quality = *params;
    
    // Program hardware
    if (m_running) {
        uint32_t qp = (params->qpI) | (params->qpP << 8) | (params->qpB << 16);
        writeRegister(VE_QP, qp);
    }
    
    return ENCODE_SUCCESS;
}

void IntelVideoEncoder::getQuality(QualityParams* params) {
    if (params) {
        *params = m_quality;
    }
}

EncodeError IntelVideoEncoder::setQualityPreset(IntelQualityPreset preset) {
    applyQualityPreset(preset);
    return ENCODE_SUCCESS;
}

// Format support
bool IntelVideoEncoder::isCodecSupported(IntelEncodeCodec codec) const {
    switch (codec) {
        case ENCODE_CODEC_H264:
            return m_capabilities.h264Supported;
        case ENCODE_CODEC_H265:
            return m_capabilities.h265Supported;
        case ENCODE_CODEC_VP9:
            return m_capabilities.vp9Supported;
        case ENCODE_CODEC_JPEG:
            return m_capabilities.jpegSupported;
        default:
            return false;
    }
}

bool IntelVideoEncoder::isProfileSupported(IntelEncodeProfile profile) const {
    // Gen12+ supports all common profiles
    return true;
}

bool IntelVideoEncoder::isFormatSupported(const EncodeFormat* format) const {
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
    
    // Check interlaced
    if (format->interlaced && !m_capabilities.interlacedSupported) {
        return false;
    }
    
    return true;
}

EncodeError IntelVideoEncoder::validateParams(const EncodeParams* params) {
    if (!params) {
        return ENCODE_ERROR_INVALID_PARAMS;
    }
    
    // Check codec support
    if (!isCodecSupported(params->codec)) {
        return ENCODE_ERROR_UNSUPPORTED_CODEC;
    }
    
    // Check profile support
    if (!isProfileSupported(params->profile)) {
        return ENCODE_ERROR_UNSUPPORTED_PROFILE;
    }
    
    // Check format
    if (!isFormatSupported(&params->format)) {
        return ENCODE_ERROR_INVALID_FORMAT;
    }
    
    // Check buffers
    if (!params->inputBuffer || !params->outputBuffer) {
        return ENCODE_ERROR_INVALID_PARAMS;
    }
    
    // Check output buffer size
    if (params->outputMaxSize < (params->format.width * params->format.height / 2)) {
        return ENCODE_ERROR_BUFFER_TOO_SMALL;
    }
    
    return ENCODE_SUCCESS;
}

// Statistics
void IntelVideoEncoder::getStatistics(EncodeStatistics* stats) {
    if (!stats) {
        return;
    }
    
    IOLockLock(m_statsLock);
    *stats = m_stats;
    IOLockUnlock(m_statsLock);
}

void IntelVideoEncoder::resetStatistics() {
    IOLockLock(m_statsLock);
    memset(&m_stats, 0, sizeof(EncodeStatistics));
    IOLockUnlock(m_statsLock);
}

void IntelVideoEncoder::printStatistics() {
    EncodeStatistics stats;
    getStatistics(&stats);
    
    IOLog("IntelVideoEncoder Statistics:\n");
    IOLog("  Performance:\n");
    IOLog("    Frames encoded: %llu\n", stats.framesEncoded);
    IOLog("    Average time: %llu ns\n", stats.averageTime);
    IOLog("    FPS: %.2f\n", stats.framesPerSecond);
    IOLog("  Bitrate:\n");
    IOLog("    Average: %u kbps\n", stats.averageBitrate);
    IOLog("    Peak: %u kbps\n", stats.peakBitrate);
    IOLog("  Quality:\n");
    IOLog("    Average QP: %.2f\n", stats.averageQP);
    IOLog("    Average PSNR: %.2f dB\n", stats.averagePSNR);
    IOLog("    Errors: %u\n", stats.errors);
    IOLog("  Frame types:\n");
    IOLog("    I: %llu, P: %llu, B: %llu\n",
          stats.iFrames, stats.pFrames, stats.bFrames);
    IOLog("  Hardware:\n");
    IOLog("    VE box usage: %.1f%%\n", stats.hardwareUsage);
    IOLog("    Bandwidth usage: %.1f%%\n", stats.bandwidthUsage);
    IOLog("  Codecs:\n");
    IOLog("    H.264: %llu frames\n", stats.h264Frames);
    IOLog("    H.265: %llu frames\n", stats.h265Frames);
    IOLog("    VP9: %llu frames\n", stats.vp9Frames);
}

// Capabilities
void IntelVideoEncoder::getCapabilities(EncodeCapabilities* caps) {
    if (!caps) {
        return;
    }
    *caps = m_capabilities;
}

uint32_t IntelVideoEncoder::getMaxWidth() const {
    return m_capabilities.maxWidth;
}

uint32_t IntelVideoEncoder::getMaxHeight() const {
    return m_capabilities.maxHeight;
}

uint32_t IntelVideoEncoder::getMaxBitrate() const {
    return m_capabilities.maxBitrate;
}

// Hardware control
bool IntelVideoEncoder::enableHardware() {
    // Enable VE box
    uint32_t control = readRegister(VE_CONTROL);
    control |= VE_CONTROL_ENABLE;
    writeRegister(VE_CONTROL, control);
    
    // Wait for ready
    for (int i = 0; i < 100; i++) {
        uint32_t status = readRegister(VE_STATUS);
        if (status & VE_STATUS_IDLE) {
            IOLog("IntelVideoEncoder: Hardware enabled\n");
            return true;
        }
        IOSleep(1);
    }
    
    IOLog("IntelVideoEncoder: Hardware enable timeout\n");
    return false;
}

void IntelVideoEncoder::disableHardware() {
    // Disable VE box
    uint32_t control = readRegister(VE_CONTROL);
    control &= ~VE_CONTROL_ENABLE;
    writeRegister(VE_CONTROL, control);
    
    IOLog("IntelVideoEncoder: Hardware disabled\n");
}

void IntelVideoEncoder::resetHardware() {
    // Reset VE box
    uint32_t control = readRegister(VE_CONTROL);
    control |= VE_CONTROL_RESET;
    writeRegister(VE_CONTROL, control);
    
    IOSleep(1);
    
    control &= ~VE_CONTROL_RESET;
    writeRegister(VE_CONTROL, control);
    
    // Wait for idle
    for (int i = 0; i < 100; i++) {
        uint32_t status = readRegister(VE_STATUS);
        if (status & VE_STATUS_IDLE) {
            IOLog("IntelVideoEncoder: Hardware reset complete\n");
            return;
        }
        IOSleep(1);
    }
    
    IOLog("IntelVideoEncoder: Hardware reset timeout\n");
}

bool IntelVideoEncoder::isHardwareIdle() {
    uint32_t status = readRegister(VE_STATUS);
    return (status & VE_STATUS_IDLE) != 0;
}

// Error handling
const char* IntelVideoEncoder::getErrorString(EncodeError error) {
    switch (error) {
        case ENCODE_SUCCESS:
            return "Success";
        case ENCODE_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case ENCODE_ERROR_UNSUPPORTED_CODEC:
            return "Unsupported codec";
        case ENCODE_ERROR_UNSUPPORTED_PROFILE:
            return "Unsupported profile";
        case ENCODE_ERROR_INVALID_FORMAT:
            return "Invalid format";
        case ENCODE_ERROR_NO_MEMORY:
            return "Out of memory";
        case ENCODE_ERROR_HARDWARE_FAULT:
            return "Hardware fault";
        case ENCODE_ERROR_TIMEOUT:
            return "Timeout";
        case ENCODE_ERROR_BITRATE_EXCEEDED:
            return "Bitrate exceeded";
        case ENCODE_ERROR_BUFFER_TOO_SMALL:
            return "Buffer too small";
        default:
            return "Unknown error";
    }
}

// Private methods

void IntelVideoEncoder::writeRegister(uint32_t reg, uint32_t value) {
    if (m_controller && m_controller->getUncore()) {
        m_controller->getUncore()->writeRegister32(reg, value);
    }
}

uint32_t IntelVideoEncoder::readRegister(uint32_t reg) {
    if (m_controller && m_controller->getUncore()) {
        return m_controller->getUncore()->readRegister32(reg);
    }
    return 0;
}

void IntelVideoEncoder::setCodecMode(IntelEncodeCodec codec,
                                    IntelEncodeProfile profile) {
    uint32_t control = readRegister(VE_CONTROL);
    
    // Clear codec field
    control &= ~VE_CONTROL_CODEC_MASK;
    
    // Set codec
    switch (codec) {
        case ENCODE_CODEC_H264:
            control |= VE_CONTROL_CODEC_H264;
            break;
        case ENCODE_CODEC_H265:
            control |= VE_CONTROL_CODEC_H265;
            break;
        case ENCODE_CODEC_VP9:
            control |= VE_CONTROL_CODEC_VP9;
            break;
        default:
            break;
    }
    
    writeRegister(VE_CONTROL, control);
}

void IntelVideoEncoder::setRateControlMode(IntelRateControlMode mode) {
    uint32_t control = readRegister(VE_CONTROL);
    
    // Clear rate control field
    control &= ~VE_CONTROL_RC_MASK;
    
    // Set rate control mode
    switch (mode) {
        case RATE_CONTROL_CQP:
            control |= VE_CONTROL_RC_CQP;
            break;
        case RATE_CONTROL_CBR:
            control |= VE_CONTROL_RC_CBR;
            break;
        case RATE_CONTROL_VBR:
            control |= VE_CONTROL_RC_VBR;
            break;
        default:
            break;
    }
    
    writeRegister(VE_CONTROL, control);
}

// Encode implementation
EncodeError IntelVideoEncoder::encodeH264(const EncodeParams* params,
                                         uint32_t* outputSize) {
    // Set H.264 mode
    setCodecMode(ENCODE_CODEC_H264, params->profile);
    setRateControlMode(params->rateControl.mode);
    
    // Setup frame type
    setupFrameType(params->frameType);
    
    // Setup input pointer
    uint64_t inputAddr = params->inputBuffer->getGPUAddress() +
                        params->inputOffset;
    writeRegister(VE_INPUT_PTR, (uint32_t)inputAddr);
    writeRegister(VE_INPUT_STRIDE, params->format.stride);
    
    // Setup output buffer
    uint64_t outputAddr = params->outputBuffer->getGPUAddress() +
                         params->outputOffset;
    writeRegister(VE_OUTPUT_PTR, (uint32_t)outputAddr);
    writeRegister(VE_OUTPUT_SIZE, params->outputMaxSize);
    
    // Setup rate control
    writeRegister(VE_BITRATE, params->rateControl.targetBitrate);
    writeRegister(VE_GOP_SIZE, params->rateControl.gopSize);
    
    // Setup quality
    uint32_t qp = (params->quality.qpI) |
                  (params->quality.qpP << 8) |
                  (params->quality.qpB << 16);
    writeRegister(VE_QP, qp);
    
    // Start encode
    writeRegister(VE_COMMAND, 1);
    
    // Wait for completion
    waitForCompletion(ENCODE_TIMEOUT_MS);
    
    // Check status
    uint32_t status = readRegister(VE_STATUS);
    if (status & VE_STATUS_ERROR) {
        return ENCODE_ERROR_HARDWARE_FAULT;
    }
    if (status & VE_STATUS_OVERFLOW) {
        return ENCODE_ERROR_BITRATE_EXCEEDED;
    }
    
    // Read output size
    *outputSize = readRegister(VE_OUTPUT_SIZE);
    
    IOLockLock(m_statsLock);
    m_stats.h264Frames++;
    if (params->frameType == FRAME_TYPE_I || params->frameType == FRAME_TYPE_IDR) {
        m_stats.iFrames++;
    } else if (params->frameType == FRAME_TYPE_P) {
        m_stats.pFrames++;
    } else if (params->frameType == FRAME_TYPE_B) {
        m_stats.bFrames++;
    }
    IOLockUnlock(m_statsLock);
    
    return ENCODE_SUCCESS;
}

EncodeError IntelVideoEncoder::encodeH265(const EncodeParams* params,
                                         uint32_t* outputSize) {
    // Set H.265 mode
    setCodecMode(ENCODE_CODEC_H265, params->profile);
    setRateControlMode(params->rateControl.mode);
    
    // Setup frame type
    setupFrameType(params->frameType);
    
    // Setup buffers (similar to H.264)
    uint64_t inputAddr = params->inputBuffer->getGPUAddress() +
                        params->inputOffset;
    writeRegister(VE_INPUT_PTR, (uint32_t)inputAddr);
    writeRegister(VE_INPUT_STRIDE, params->format.stride);
    
    uint64_t outputAddr = params->outputBuffer->getGPUAddress() +
                         params->outputOffset;
    writeRegister(VE_OUTPUT_PTR, (uint32_t)outputAddr);
    writeRegister(VE_OUTPUT_SIZE, params->outputMaxSize);
    
    // Setup rate control and quality
    writeRegister(VE_BITRATE, params->rateControl.targetBitrate);
    writeRegister(VE_GOP_SIZE, params->rateControl.gopSize);
    
    uint32_t qp = (params->quality.qpI) |
                  (params->quality.qpP << 8) |
                  (params->quality.qpB << 16);
    writeRegister(VE_QP, qp);
    
    // Start encode
    writeRegister(VE_COMMAND, 1);
    
    // Wait and check
    waitForCompletion(ENCODE_TIMEOUT_MS);
    
    uint32_t status = readRegister(VE_STATUS);
    if (status & VE_STATUS_ERROR) {
        return ENCODE_ERROR_HARDWARE_FAULT;
    }
    
    *outputSize = readRegister(VE_OUTPUT_SIZE);
    
    IOLockLock(m_statsLock);
    m_stats.h265Frames++;
    if (params->frameType == FRAME_TYPE_I || params->frameType == FRAME_TYPE_IDR) {
        m_stats.iFrames++;
    } else if (params->frameType == FRAME_TYPE_P) {
        m_stats.pFrames++;
    }
    IOLockUnlock(m_statsLock);
    
    return ENCODE_SUCCESS;
}

EncodeError IntelVideoEncoder::encodeVP9(const EncodeParams* params,
                                        uint32_t* outputSize) {
    // Set VP9 mode
    setCodecMode(ENCODE_CODEC_VP9, params->profile);
    setRateControlMode(params->rateControl.mode);
    
    // Setup buffers
    uint64_t inputAddr = params->inputBuffer->getGPUAddress() +
                        params->inputOffset;
    writeRegister(VE_INPUT_PTR, (uint32_t)inputAddr);
    writeRegister(VE_INPUT_STRIDE, params->format.stride);
    
    uint64_t outputAddr = params->outputBuffer->getGPUAddress() +
                         params->outputOffset;
    writeRegister(VE_OUTPUT_PTR, (uint32_t)outputAddr);
    writeRegister(VE_OUTPUT_SIZE, params->outputMaxSize);
    
    // Setup parameters
    writeRegister(VE_BITRATE, params->rateControl.targetBitrate);
    
    // Start encode
    writeRegister(VE_COMMAND, 1);
    
    // Wait and check
    waitForCompletion(ENCODE_TIMEOUT_MS);
    
    uint32_t status = readRegister(VE_STATUS);
    if (status & VE_STATUS_ERROR) {
        return ENCODE_ERROR_HARDWARE_FAULT;
    }
    
    *outputSize = readRegister(VE_OUTPUT_SIZE);
    
    IOLockLock(m_statsLock);
    m_stats.vp9Frames++;
    IOLockUnlock(m_statsLock);
    
    return ENCODE_SUCCESS;
}

EncodeError IntelVideoEncoder::encodeJPEG(const EncodeParams* params,
                                         uint32_t* outputSize) {
    // JPEG encoding is simpler
    setCodecMode(ENCODE_CODEC_JPEG, params->profile);
    
    uint64_t inputAddr = params->inputBuffer->getGPUAddress() +
                        params->inputOffset;
    writeRegister(VE_INPUT_PTR, (uint32_t)inputAddr);
    writeRegister(VE_INPUT_STRIDE, params->format.stride);
    
    uint64_t outputAddr = params->outputBuffer->getGPUAddress() +
                         params->outputOffset;
    writeRegister(VE_OUTPUT_PTR, (uint32_t)outputAddr);
    writeRegister(VE_OUTPUT_SIZE, params->outputMaxSize);
    
    // Quality for JPEG (0-100)
    uint32_t quality = 100 - params->quality.qpI;
    writeRegister(VE_QP, quality);
    
    // Start encode
    writeRegister(VE_COMMAND, 1);
    
    // Wait and check
    waitForCompletion(ENCODE_TIMEOUT_MS);
    
    uint32_t status = readRegister(VE_STATUS);
    if (status & VE_STATUS_ERROR) {
        return ENCODE_ERROR_HARDWARE_FAULT;
    }
    
    *outputSize = readRegister(VE_OUTPUT_SIZE);
    
    return ENCODE_SUCCESS;
}

// Frame type helpers
void IntelVideoEncoder::setupFrameType(IntelFrameType frameType) {
    // Frame type is encoded in command register
    // Implementation would set appropriate hardware bits
}

IntelFrameType IntelVideoEncoder::determineFrameType(uint32_t frameNumber) {
    // Simple GOP structure
    if (m_gopPosition == 0 || frameNumber % m_rateControl.idrInterval == 0) {
        m_gopPosition = 0;
        return FRAME_TYPE_IDR;
    }
    
    if (m_gopPosition % m_rateControl.gopSize == 0) {
        m_gopPosition = 0;
        return FRAME_TYPE_I;
    }
    
    m_gopPosition++;
    
    // Use B frames if enabled
    if (m_rateControl.bFrames > 0) {
        uint32_t bPosition = m_gopPosition % (m_rateControl.bFrames + 1);
        if (bPosition != 0) {
            return FRAME_TYPE_B;
        }
    }
    
    return FRAME_TYPE_P;
}

// Reference frame helpers
EncodeError IntelVideoEncoder::setupReferenceFrames(const EncodeParams* params) {
    IOLockLock(m_refFrameLock);
    
    // Program reference frame addresses
    for (uint32_t i = 0; i < params->numRefFrames && i < 4; i++) {
        if (params->refFrames[i]) {
            uint64_t addr = params->refFrames[i]->getGPUAddress();
            uint32_t reg = VE_REF_FRAME_BASE + (i * 8);
            writeRegister(reg, (uint32_t)addr);
            writeRegister(reg + 4, (uint32_t)(addr >> 32));
        }
    }
    
    IOLockUnlock(m_refFrameLock);
    return ENCODE_SUCCESS;
}

void IntelVideoEncoder::updateReferenceFrames(IntelGEMObject* currentFrame) {
    IOLockLock(m_refFrameLock);
    
    // Shift reference frames
    for (int i = 3; i > 0; i--) {
        m_refFrames[i] = m_refFrames[i-1];
    }
    m_refFrames[0] = currentFrame;
    m_numRefFrames = (m_numRefFrames < 4) ? m_numRefFrames + 1 : 4;
    
    IOLockUnlock(m_refFrameLock);
}

// Buffer management
EncodeError IntelVideoEncoder::allocateInternalBuffers() {
    // Allocate temporary encode buffer (4MB)
    m_tempBuffer = m_controller->getGEM()->createObject(4 * 1024 * 1024);
    if (!m_tempBuffer) {
        return ENCODE_ERROR_NO_MEMORY;
    }
    
    // Allocate bitstream assembly buffer (2MB)
    m_bitstreamBuffer = m_controller->getGEM()->createObject(2 * 1024 * 1024);
    if (!m_bitstreamBuffer) {
        m_controller->getGEM()->destroyObject(m_tempBuffer);
        m_tempBuffer = nullptr;
        return ENCODE_ERROR_NO_MEMORY;
    }
    
    return ENCODE_SUCCESS;
}

void IntelVideoEncoder::freeInternalBuffers() {
    if (m_tempBuffer) {
        m_controller->getGEM()->destroyObject(m_tempBuffer);
        m_tempBuffer = nullptr;
    }
    
    if (m_bitstreamBuffer) {
        m_controller->getGEM()->destroyObject(m_bitstreamBuffer);
        m_bitstreamBuffer = nullptr;
    }
}

// Statistics tracking
void IntelVideoEncoder::updateStatistics(uint64_t encodeTime,
                                        uint32_t outputSize,
                                        EncodeError error) {
    IOLockLock(m_statsLock);
    
    m_stats.framesEncoded++;
    m_stats.totalTime += encodeTime;
    m_stats.averageTime = m_stats.totalTime / m_stats.framesEncoded;
    
    // Calculate FPS (assuming 1GHz timebase)
    if (encodeTime > 0) {
        m_stats.framesPerSecond = 1000000000.0f / (float)encodeTime;
    }
    
    // Update bitrate
    uint32_t bitsThisFrame = outputSize * 8;
    m_stats.totalBits += bitsThisFrame;
    m_stats.averageBitrate = (uint32_t)(m_stats.totalBits / m_stats.framesEncoded / 1000);
    
    uint32_t instantBitrate = (uint32_t)(bitsThisFrame * m_rateControl.frameRateNum /
                                         m_rateControl.frameRateDen / 1000);
    if (instantBitrate > m_stats.peakBitrate) {
        m_stats.peakBitrate = instantBitrate;
    }
    
    if (error != ENCODE_SUCCESS) {
        m_stats.errors++;
    }
    
    // Estimate hardware usage
    m_stats.hardwareUsage = (encodeTime > 16666666) ? 100.0f :
                           (float)encodeTime / 16666666.0f * 100.0f;
    
    m_lastEncodeTime = encodeTime;
    
    IOLockUnlock(m_statsLock);
}

// Hardware detection
void IntelVideoEncoder::detectCapabilities() {
    // Gen12 (Tiger Lake) capabilities
    m_capabilities.h264Supported = true;
    m_capabilities.h265Supported = true;
    m_capabilities.vp9Supported = true;
    m_capabilities.jpegSupported = true;
    
    m_capabilities.maxWidth = 4096;
    m_capabilities.maxHeight = 4096;
    m_capabilities.maxBitrate = 100;  // 100 Mbps
    m_capabilities.maxFrameRate = 60;
    
    m_capabilities.bit10Supported = true;
    m_capabilities.interlacedSupported = false;
    m_capabilities.bFramesSupported = true;
    m_capabilities.cabacSupported = true;
    m_capabilities.lowLatencySupported = true;
    
    IOLog("IntelVideoEncoder: Capabilities detected\n");
    IOLog("  H.264: %s\n", m_capabilities.h264Supported ? "Yes" : "No");
    IOLog("  H.265: %s\n", m_capabilities.h265Supported ? "Yes" : "No");
    IOLog("  VP9: %s\n", m_capabilities.vp9Supported ? "Yes" : "No");
    IOLog("  Max resolution: %ux%u @ %u fps\n",
          m_capabilities.maxWidth, m_capabilities.maxHeight,
          m_capabilities.maxFrameRate);
}

bool IntelVideoEncoder::checkCodecSupport(IntelEncodeCodec codec) {
    return isCodecSupported(codec);
}

// Quality preset helpers
void IntelVideoEncoder::applyQualityPreset(IntelQualityPreset preset) {
    m_quality.preset = preset;
    
    switch (preset) {
        case QUALITY_SPEED:
            m_quality.qpI = 30;
            m_quality.qpP = 32;
            m_quality.qpB = 34;
            m_quality.searchRange = 16;
            m_quality.trellis = false;
            break;
            
        case QUALITY_BALANCED:
            m_quality.qpI = 26;
            m_quality.qpP = 28;
            m_quality.qpB = 30;
            m_quality.searchRange = 32;
            m_quality.trellis = false;
            break;
            
        case QUALITY_QUALITY:
            m_quality.qpI = 22;
            m_quality.qpP = 24;
            m_quality.qpB = 26;
            m_quality.searchRange = 64;
            m_quality.trellis = true;
            break;
            
        default:
            break;
    }
}
