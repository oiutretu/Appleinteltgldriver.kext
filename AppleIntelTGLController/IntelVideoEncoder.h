/*
 * IntelVideoEncoder.h
 * Intel Graphics Driver
 *
 * Video encode engine (VE Box) support for Intel Gen12+ GPUs.
 * Provides hardware-accelerated video encoding for H.264, H.265, and VP9.
 *
 * Week 31 - Phase 7: Video Engine
 */

#ifndef INTEL_VIDEO_ENCODER_H
#define INTEL_VIDEO_ENCODER_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "AppleIntelTGLController.h"
#include "IntelGEMObject.h"
#include "IntelRingBuffer.h"

// Forward declarations
class AppleIntelTGLController;
class IntelRingBuffer;

// Encode codec types
typedef enum {
    ENCODE_CODEC_H264 = 0,      // H.264/AVC
    ENCODE_CODEC_H265,          // H.265/HEVC
    ENCODE_CODEC_VP9,           // VP9
    ENCODE_CODEC_JPEG,          // JPEG
    ENCODE_CODEC_COUNT
} IntelEncodeCodec;

// Encode profile levels
typedef enum {
    H264_ENC_BASELINE = 0,
    H264_ENC_MAIN,
    H264_ENC_HIGH,
    H264_ENC_HIGH10,
    H265_ENC_MAIN,
    H265_ENC_MAIN10,
    H265_ENC_MAIN_STILL,
    VP9_ENC_PROFILE_0,
    VP9_ENC_PROFILE_2,
    ENC_PROFILE_COUNT
} IntelEncodeProfile;

// Rate control modes
typedef enum {
    RATE_CONTROL_CQP = 0,       // Constant Quantization Parameter
    RATE_CONTROL_CBR,           // Constant Bitrate
    RATE_CONTROL_VBR,           // Variable Bitrate
    RATE_CONTROL_AVBR,          // Average Variable Bitrate
    RATE_CONTROL_QVBR,          // Quality Variable Bitrate
    RATE_CONTROL_COUNT
} IntelRateControlMode;

// Quality presets
typedef enum {
    QUALITY_SPEED = 0,          // Fastest encoding
    QUALITY_BALANCED,           // Balance speed/quality
    QUALITY_QUALITY,            // Best quality
    QUALITY_COUNT
} IntelQualityPreset;

// Frame types
typedef enum {
    FRAME_TYPE_I = 0,           // Intra (keyframe)
    FRAME_TYPE_P,               // Predicted
    FRAME_TYPE_B,               // Bi-directional
    FRAME_TYPE_IDR,             // IDR keyframe
    FRAME_TYPE_COUNT
} IntelFrameType;

// Encode error codes
typedef enum {
    ENCODE_SUCCESS = 0,
    ENCODE_ERROR_INVALID_PARAMS,
    ENCODE_ERROR_UNSUPPORTED_CODEC,
    ENCODE_ERROR_UNSUPPORTED_PROFILE,
    ENCODE_ERROR_INVALID_FORMAT,
    ENCODE_ERROR_NO_MEMORY,
    ENCODE_ERROR_HARDWARE_FAULT,
    ENCODE_ERROR_TIMEOUT,
    ENCODE_ERROR_BITRATE_EXCEEDED,
    ENCODE_ERROR_BUFFER_TOO_SMALL,
    ENCODE_ERROR_COUNT
} EncodeError;

// Video input format
typedef struct {
    uint32_t width;              // Frame width
    uint32_t height;             // Frame height
    uint32_t stride;             // Row stride
    uint32_t format;             // Pixel format (NV12, YUY2, etc)
    uint32_t bitDepth;           // Bit depth (8, 10)
    uint32_t frameRate;          // Frame rate (fps * 1000)
    bool interlaced;             // Interlaced?
} EncodeFormat;

// Rate control parameters
typedef struct {
    IntelRateControlMode mode;   // Rate control mode
    uint32_t targetBitrate;      // Target bitrate (kbps)
    uint32_t maxBitrate;         // Max bitrate (kbps)
    uint32_t minBitrate;         // Min bitrate (kbps)
    uint32_t vbvBufferSize;      // VBV buffer size (bits)
    uint32_t vbvInitialDelay;    // VBV initial delay (ms)
    uint32_t frameRateNum;       // Frame rate numerator
    uint32_t frameRateDen;       // Frame rate denominator
    uint32_t gopSize;            // GOP size (frames)
    uint32_t idrInterval;        // IDR interval (frames)
    uint32_t ipRatio;            // I:P ratio
    uint32_t bFrames;            // B frames between refs
} RateControlParams;

// Quality parameters
typedef struct {
    IntelQualityPreset preset;   // Quality preset
    uint32_t qpI;                // QP for I frames (0-51)
    uint32_t qpP;                // QP for P frames (0-51)
    uint32_t qpB;                // QP for B frames (0-51)
    uint32_t qpMin;              // Minimum QP
    uint32_t qpMax;              // Maximum QP
    bool adaptiveQuantization;   // Adaptive QP?
    bool trellis;                // Trellis quantization?
    bool cabac;                  // CABAC entropy coding?
    uint32_t searchRange;        // Motion search range
} QualityParams;

// Encode parameters
typedef struct {
    IntelEncodeCodec codec;      // Video codec
    IntelEncodeProfile profile;  // Profile level
    EncodeFormat format;         // Input format
    
    // Input/output
    IntelGEMObject* inputBuffer;        // Raw frame data
    uint32_t inputOffset;               // Offset in buffer
    IntelGEMObject* outputBuffer;       // Compressed bitstream
    uint32_t outputOffset;              // Offset in buffer
    uint32_t outputMaxSize;             // Max output size
    
    // Frame control
    IntelFrameType frameType;    // Frame type
    uint32_t frameNumber;        // Frame number
    uint64_t timestamp;          // Presentation timestamp
    bool forceKeyframe;          // Force IDR?
    
    // Reference frames
    IntelGEMObject* refFrames[4];       // Reference frames
    uint32_t numRefFrames;              // Number of refs
    
    // Rate control
    RateControlParams rateControl;
    
    // Quality
    QualityParams quality;
    
    // Advanced
    bool lowLatency;             // Low latency mode?
    bool lossless;               // Lossless mode?
    uint32_t slices;             // Number of slices
    uint32_t tiles;              // Number of tiles
} EncodeParams;

// Encode statistics
typedef struct {
    // Performance
    uint64_t framesEncoded;      // Total frames encoded
    uint64_t totalTime;          // Total encode time (ns)
    uint64_t averageTime;        // Average per frame (ns)
    float framesPerSecond;       // Encode FPS
    
    // Bitrate
    uint64_t totalBits;          // Total bits output
    uint32_t averageBitrate;     // Average bitrate (kbps)
    uint32_t peakBitrate;        // Peak bitrate (kbps)
    
    // Quality
    float averageQP;             // Average QP
    float averagePSNR;           // Average PSNR (dB)
    uint32_t skippedFrames;      // Frames skipped
    uint32_t errors;             // Encode errors
    
    // Frame types
    uint64_t iFrames;            // I frames
    uint64_t pFrames;            // P frames
    uint64_t bFrames;            // B frames
    
    // Hardware utilization
    float hardwareUsage;         // VE box usage (%)
    float bandwidthUsage;        // Memory bandwidth (%)
    
    // Codec breakdown
    uint64_t h264Frames;         // H.264 frames
    uint64_t h265Frames;         // H.265 frames
    uint64_t vp9Frames;          // VP9 frames
} EncodeStatistics;

// Hardware capabilities
typedef struct {
    bool h264Supported;          // H.264 support
    bool h265Supported;          // H.265 support
    bool vp9Supported;           // VP9 support
    bool jpegSupported;          // JPEG support
    
    uint32_t maxWidth;           // Max width
    uint32_t maxHeight;          // Max height
    uint32_t maxBitrate;         // Max bitrate (Mbps)
    uint32_t maxFrameRate;       // Max frame rate
    
    bool bit10Supported;         // 10-bit support
    bool interlacedSupported;    // Interlaced support
    bool bFramesSupported;       // B frames support
    bool cabacSupported;         // CABAC support
    bool lowLatencySupported;    // Low latency support
} EncodeCapabilities;

// VE Box registers (Gen12)
#define VE_REG_BASE                 0x1D0000
#define VE_CONTROL                  (VE_REG_BASE + 0x0000)
#define VE_STATUS                   (VE_REG_BASE + 0x0004)
#define VE_COMMAND                  (VE_REG_BASE + 0x0008)
#define VE_ERROR                    (VE_REG_BASE + 0x000C)
#define VE_INPUT_PTR                (VE_REG_BASE + 0x0010)
#define VE_INPUT_STRIDE             (VE_REG_BASE + 0x0014)
#define VE_OUTPUT_PTR               (VE_REG_BASE + 0x0018)
#define VE_OUTPUT_SIZE              (VE_REG_BASE + 0x001C)
#define VE_BITRATE                  (VE_REG_BASE + 0x0020)
#define VE_FRAME_RATE               (VE_REG_BASE + 0x0024)
#define VE_QP                       (VE_REG_BASE + 0x0028)
#define VE_GOP_SIZE                 (VE_REG_BASE + 0x002C)
#define VE_REF_FRAME_BASE           (VE_REG_BASE + 0x0100)

// VE Control bits
#define VE_CONTROL_ENABLE           (1 << 0)
#define VE_CONTROL_RESET            (1 << 1)
#define VE_CONTROL_CODEC_MASK       (0xF << 4)
#define VE_CONTROL_CODEC_H264       (0 << 4)
#define VE_CONTROL_CODEC_H265       (1 << 4)
#define VE_CONTROL_CODEC_VP9        (2 << 4)
#define VE_CONTROL_RC_MASK          (0xF << 8)
#define VE_CONTROL_RC_CQP           (0 << 8)
#define VE_CONTROL_RC_CBR           (1 << 8)
#define VE_CONTROL_RC_VBR           (2 << 8)

// VE Status bits
#define VE_STATUS_IDLE              (1 << 0)
#define VE_STATUS_BUSY              (1 << 1)
#define VE_STATUS_DONE              (1 << 2)
#define VE_STATUS_ERROR             (1 << 3)
#define VE_STATUS_OVERFLOW          (1 << 4)

// Constants
#define MAX_GOP_SIZE                300
#define MAX_B_FRAMES                3
#define MAX_SLICES                  16
#define ENCODE_TIMEOUT_MS           5000

class IntelVideoEncoder : public OSObject {
    OSDeclareDefaultStructors(IntelVideoEncoder)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    bool initWithController(AppleIntelTGLController* controller);
    
    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const { return m_running; }
    
    // Encode operations
    EncodeError encode(const EncodeParams* params, uint32_t* outputSize);
    EncodeError encodeFrame(IntelEncodeCodec codec, IntelGEMObject* input,
                          IntelGEMObject* output, const EncodeFormat* format,
                          uint32_t* outputSize);
    void waitForCompletion(uint32_t timeoutMs = ENCODE_TIMEOUT_MS);
    void flush();
    
    // Rate control
    EncodeError setRateControl(const RateControlParams* params);
    void getRateControl(RateControlParams* params);
    EncodeError updateBitrate(uint32_t bitrate);
    
    // Quality control
    EncodeError setQuality(const QualityParams* params);
    void getQuality(QualityParams* params);
    EncodeError setQualityPreset(IntelQualityPreset preset);
    
    // Format support
    bool isCodecSupported(IntelEncodeCodec codec) const;
    bool isProfileSupported(IntelEncodeProfile profile) const;
    bool isFormatSupported(const EncodeFormat* format) const;
    EncodeError validateParams(const EncodeParams* params);
    
    // Statistics
    void getStatistics(EncodeStatistics* stats);
    void resetStatistics();
    void printStatistics();
    
    // Capabilities
    void getCapabilities(EncodeCapabilities* caps);
    uint32_t getMaxWidth() const;
    uint32_t getMaxHeight() const;
    uint32_t getMaxBitrate() const;
    
    // Hardware control
    bool enableHardware();
    void disableHardware();
    void resetHardware();
    bool isHardwareIdle();
    
    // Error handling
    const char* getErrorString(EncodeError error);
    EncodeError getLastError() const { return m_lastError; }
    
private:
    // Hardware access
    void writeRegister(uint32_t reg, uint32_t value);
    uint32_t readRegister(uint32_t reg);
    void setCodecMode(IntelEncodeCodec codec, IntelEncodeProfile profile);
    void setRateControlMode(IntelRateControlMode mode);
    
    // Encode implementation
    EncodeError encodeH264(const EncodeParams* params, uint32_t* outputSize);
    EncodeError encodeH265(const EncodeParams* params, uint32_t* outputSize);
    EncodeError encodeVP9(const EncodeParams* params, uint32_t* outputSize);
    EncodeError encodeJPEG(const EncodeParams* params, uint32_t* outputSize);
    
    // Frame type helpers
    void setupFrameType(IntelFrameType frameType);
    IntelFrameType determineFrameType(uint32_t frameNumber);
    
    // Reference frame helpers
    EncodeError setupReferenceFrames(const EncodeParams* params);
    void updateReferenceFrames(IntelGEMObject* currentFrame);
    
    // Buffer management
    EncodeError allocateInternalBuffers();
    void freeInternalBuffers();
    
    // Statistics tracking
    void updateStatistics(uint64_t encodeTime, uint32_t outputSize,
                         EncodeError error);
    
    // Hardware detection
    void detectCapabilities();
    bool checkCodecSupport(IntelEncodeCodec codec);
    
    // Quality preset helpers
    void applyQualityPreset(IntelQualityPreset preset);
    
    // Member variables
    AppleIntelTGLController* m_controller;
    IntelRingBuffer* m_ringBuffer;     // VCS0/VCS1 ring
    bool m_running;
    
    // Configuration
    RateControlParams m_rateControl;
    QualityParams m_quality;
    
    // Reference frames
    IntelGEMObject* m_refFrames[4];
    uint32_t m_numRefFrames;
    IOLock* m_refFrameLock;
    
    // Statistics
    EncodeStatistics m_stats;
    IOLock* m_statsLock;
    uint64_t m_lastEncodeTime;
    
    // Capabilities
    EncodeCapabilities m_capabilities;
    
    // Error tracking
    EncodeError m_lastError;
    
    // Internal buffers
    IntelGEMObject* m_tempBuffer;      // Temporary encode buffer
    IntelGEMObject* m_bitstreamBuffer; // Bitstream assembly buffer
    
    // Frame tracking
    uint32_t m_frameCount;
    uint32_t m_gopPosition;
};

#endif /* INTEL_VIDEO_ENCODER_H */
