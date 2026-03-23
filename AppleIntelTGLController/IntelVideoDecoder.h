/*
 * IntelVideoDecoder.h
 * Intel Graphics Driver
 *
 * Video decode engine (VD Box) support for Intel Gen12+ GPUs.
 * Provides hardware-accelerated video decoding for H.264, H.265, VP9, and JPEG.
 *
 * Week 30 - Phase 7: Video Engine
 */

#ifndef INTEL_VIDEO_DECODER_H
#define INTEL_VIDEO_DECODER_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "AppleIntelTGLController.h"
#include "IntelGEMObject.h"
#include "IntelRingBuffer.h"

// Forward declarations
class AppleIntelTGLController;
class IntelRingBuffer;

// Video codec types
typedef enum {
    VIDEO_CODEC_H264 = 0,       // H.264/AVC (most common)
    VIDEO_CODEC_H265,           // H.265/HEVC (4K, HDR)
    VIDEO_CODEC_VP9,            // VP9 (YouTube, WebM)
    VIDEO_CODEC_JPEG,           // JPEG/MJPEG
    VIDEO_CODEC_AV1,            // AV1 (future)
    VIDEO_CODEC_COUNT
} IntelVideoCodec;

// Video profile levels
typedef enum {
    H264_PROFILE_BASELINE = 0,
    H264_PROFILE_MAIN,
    H264_PROFILE_HIGH,
    H265_PROFILE_MAIN,
    H265_PROFILE_MAIN10,
    H265_PROFILE_MAIN_STILL_PICTURE,
    VP9_PROFILE_0,
    VP9_PROFILE_1,
    VP9_PROFILE_2,
    VP9_PROFILE_3,
    PROFILE_COUNT
} IntelVideoProfile;

// Decode operation types
typedef enum {
    DECODE_OP_SLICE = 0,        // Decode slice
    DECODE_OP_FRAME,            // Decode full frame
    DECODE_OP_FIELD,            // Decode field (interlaced)
    DECODE_OP_MACROBLOCK,       // Decode macroblock
    DECODE_OP_COUNT
} IntelDecodeOp;

// Reference frame types
typedef enum {
    REF_FRAME_NONE = 0,
    REF_FRAME_SHORT_TERM,       // Short-term reference
    REF_FRAME_LONG_TERM,        // Long-term reference
    REF_FRAME_DISPLAY,          // For display only
    REF_FRAME_COUNT
} IntelRefFrameType;

// Decode error codes
typedef enum {
    DECODE_SUCCESS = 0,
    DECODE_ERROR_INVALID_PARAMS,
    DECODE_ERROR_UNSUPPORTED_CODEC,
    DECODE_ERROR_UNSUPPORTED_PROFILE,
    DECODE_ERROR_INVALID_BITSTREAM,
    DECODE_ERROR_NO_MEMORY,
    DECODE_ERROR_HARDWARE_FAULT,
    DECODE_ERROR_TIMEOUT,
    DECODE_ERROR_TOO_MANY_REFS,
    DECODE_ERROR_BUFFER_TOO_SMALL,
    DECODE_ERROR_COUNT
} DecodeError;

// Video format information
typedef struct {
    uint32_t width;              // Frame width (pixels)
    uint32_t height;             // Frame height (pixels)
    uint32_t stride;             // Row stride (bytes)
    uint32_t format;             // Pixel format (NV12, YUY2, etc)
    uint32_t bitDepth;           // Bit depth (8, 10, 12)
    bool interlaced;             // Interlaced video?
    bool chromaSubsampling;      // 4:2:0, 4:2:2, 4:4:4
} VideoFormat;

// Decode parameters
typedef struct {
    IntelVideoCodec codec;       // Video codec
    IntelVideoProfile profile;   // Profile level
    IntelDecodeOp operation;     // Decode operation
    VideoFormat format;          // Video format
    
    // Bitstream
    IntelGEMObject* bitstreamBuffer;    // Compressed data
    uint32_t bitstreamOffset;           // Offset in buffer
    uint32_t bitstreamSize;             // Size in bytes
    
    // Output
    IntelGEMObject* outputBuffer;       // Decoded frame
    uint32_t outputOffset;              // Offset in buffer
    
    // Reference frames
    IntelGEMObject* refFrames[16];      // Reference frame buffers
    IntelRefFrameType refTypes[16];     // Reference types
    uint32_t numRefFrames;              // Number of refs
    
    // Slice/macroblock parameters
    uint32_t sliceOffset;        // Slice data offset
    uint32_t sliceSize;          // Slice data size
    uint32_t mbX;                // Macroblock X position
    uint32_t mbY;                // Macroblock Y position
    
    // Quality parameters
    uint32_t quantization;       // Quantization parameter
    bool deblocking;             // Deblocking filter
    bool errorConcealment;       // Error concealment
} DecodeParams;

// Reference frame information
typedef struct {
    IntelGEMObject* buffer;      // Frame buffer
    IntelRefFrameType type;      // Reference type
    uint32_t frameNum;           // Frame number
    uint32_t pocLsb;             // Picture order count
    bool used;                   // In use?
    uint64_t timestamp;          // Decode timestamp
} ReferenceFrame;

// Decode statistics
typedef struct {
    // Performance
    uint64_t framesDecoded;      // Total frames decoded
    uint64_t slicesDecoded;      // Total slices decoded
    uint64_t macroblocks;        // Total macroblocks
    uint64_t totalTime;          // Total decode time (ns)
    uint64_t averageTime;        // Average per frame (ns)
    float framesPerSecond;       // Decode FPS
    
    // Quality
    uint32_t errors;             // Decode errors
    uint32_t warnings;           // Decode warnings
    uint32_t errorsCorrected;    // Errors corrected
    uint32_t errorConcealed;     // Errors concealed
    
    // Hardware utilization
    float hardwareUsage;         // VD box usage (%)
    float bandwidthUsage;        // Memory bandwidth (%)
    
    // Codec breakdown
    uint64_t h264Frames;         // H.264 frames
    uint64_t h265Frames;         // H.265 frames
    uint64_t vp9Frames;          // VP9 frames
    uint64_t jpegFrames;         // JPEG frames
} DecodeStatistics;

// Hardware capabilities
typedef struct {
    bool h264Supported;          // H.264 support
    bool h265Supported;          // H.265 support
    bool vp9Supported;           // VP9 support
    bool jpegSupported;          // JPEG support
    bool av1Supported;           // AV1 support
    
    uint32_t maxWidth;           // Max frame width
    uint32_t maxHeight;          // Max frame height
    uint32_t maxRefFrames;       // Max reference frames
    uint32_t maxBitrate;         // Max bitrate (Mbps)
    
    bool bit10Supported;         // 10-bit support
    bool bit12Supported;         // 12-bit support
    bool interlacedSupported;    // Interlaced support
    bool concurrentDecode;       // Concurrent decode streams
} DecodeCapabilities;

// VD Box registers (Gen12)
#define VD_REG_BASE                 0x1C0000
#define VD_CONTROL                  (VD_REG_BASE + 0x0000)
#define VD_STATUS                   (VD_REG_BASE + 0x0004)
#define VD_COMMAND                  (VD_REG_BASE + 0x0008)
#define VD_ERROR                    (VD_REG_BASE + 0x000C)
#define VD_BITSTREAM_PTR            (VD_REG_BASE + 0x0010)
#define VD_BITSTREAM_SIZE           (VD_REG_BASE + 0x0014)
#define VD_OUTPUT_PTR               (VD_REG_BASE + 0x0018)
#define VD_OUTPUT_STRIDE            (VD_REG_BASE + 0x001C)
#define VD_REF_FRAME_BASE           (VD_REG_BASE + 0x0100)
#define VD_SLICE_PARAMS             (VD_REG_BASE + 0x0200)
#define VD_MB_PARAMS                (VD_REG_BASE + 0x0300)

// VD Control bits
#define VD_CONTROL_ENABLE           (1 << 0)
#define VD_CONTROL_RESET            (1 << 1)
#define VD_CONTROL_CODEC_MASK       (0xF << 4)
#define VD_CONTROL_CODEC_H264       (0 << 4)
#define VD_CONTROL_CODEC_H265       (1 << 4)
#define VD_CONTROL_CODEC_VP9        (2 << 4)
#define VD_CONTROL_CODEC_JPEG       (3 << 4)
#define VD_CONTROL_PROFILE_MASK     (0xF << 8)

// VD Status bits
#define VD_STATUS_IDLE              (1 << 0)
#define VD_STATUS_BUSY              (1 << 1)
#define VD_STATUS_DONE              (1 << 2)
#define VD_STATUS_ERROR             (1 << 3)
#define VD_STATUS_TIMEOUT           (1 << 4)

// Constants
#define MAX_REFERENCE_FRAMES        16
#define MAX_SLICES_PER_FRAME        256
#define MAX_MACROBLOCKS_PER_SLICE   8192
#define DECODE_TIMEOUT_MS           1000
#define MAX_CONCURRENT_DECODES      4

class IntelVideoDecoder : public OSObject {
    OSDeclareDefaultStructors(IntelVideoDecoder)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    bool initWithController(AppleIntelTGLController* controller);
    
    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const { return m_running; }
    
    // Decode operations
    DecodeError decode(const DecodeParams* params);
    DecodeError decodeFrame(IntelVideoCodec codec, IntelGEMObject* input,
                          IntelGEMObject* output, const VideoFormat* format);
    DecodeError decodeSlice(const DecodeParams* params, uint32_t sliceIndex);
    void waitForCompletion(uint32_t timeoutMs = DECODE_TIMEOUT_MS);
    void flush();
    
    // Reference frame management
    DecodeError addReferenceFrame(IntelGEMObject* buffer, IntelRefFrameType type,
                                 uint32_t frameNum);
    DecodeError removeReferenceFrame(uint32_t frameNum);
    DecodeError clearReferenceFrames();
    uint32_t getReferenceFrameCount() const { return m_numRefFrames; }
    ReferenceFrame* getReferenceFrame(uint32_t index);
    
    // Format support
    bool isCodecSupported(IntelVideoCodec codec) const;
    bool isProfileSupported(IntelVideoProfile profile) const;
    bool isFormatSupported(const VideoFormat* format) const;
    DecodeError validateParams(const DecodeParams* params);
    
    // Statistics
    void getStatistics(DecodeStatistics* stats);
    void resetStatistics();
    void printStatistics();
    
    // Capabilities
    void getCapabilities(DecodeCapabilities* caps);
    uint32_t getMaxWidth() const;
    uint32_t getMaxHeight() const;
    uint32_t getMaxRefFrames() const { return MAX_REFERENCE_FRAMES; }
    
    // Hardware control
    bool enableHardware();
    void disableHardware();
    void resetHardware();
    bool isHardwareIdle();
    
    // Error handling
    const char* getErrorString(DecodeError error);
    DecodeError getLastError() const { return m_lastError; }
    
private:
    // Hardware access
    void writeRegister(uint32_t reg, uint32_t value);
    uint32_t readRegister(uint32_t reg);
    void setCodecMode(IntelVideoCodec codec, IntelVideoProfile profile);
    
    // Decode implementation
    DecodeError decodeH264(const DecodeParams* params);
    DecodeError decodeH265(const DecodeParams* params);
    DecodeError decodeVP9(const DecodeParams* params);
    DecodeError decodeJPEG(const DecodeParams* params);
    
    // Slice processing
    DecodeError setupSliceParams(const DecodeParams* params, uint32_t sliceIndex);
    DecodeError processSlice(uint32_t sliceIndex);
    
    // Reference frame helpers
    ReferenceFrame* findReferenceFrame(uint32_t frameNum);
    DecodeError setupReferenceFrames(const DecodeParams* params);
    void updateReferenceFrames(uint32_t currentFrameNum);
    
    // Buffer management
    DecodeError allocateInternalBuffers();
    void freeInternalBuffers();
    
    // Statistics tracking
    void updateStatistics(uint64_t decodeTime, DecodeError error);
    
    // Hardware detection
    void detectCapabilities();
    bool checkCodecSupport(IntelVideoCodec codec);
    
    // Member variables
    AppleIntelTGLController* m_controller;
    IntelRingBuffer* m_ringBuffer;     // VCS0/VCS1 ring
    bool m_running;
    
    // Reference frames
    ReferenceFrame m_refFrames[MAX_REFERENCE_FRAMES];
    uint32_t m_numRefFrames;
    IOLock* m_refFrameLock;
    
    // Statistics
    DecodeStatistics m_stats;
    IOLock* m_statsLock;
    uint64_t m_lastDecodeTime;
    
    // Capabilities
    DecodeCapabilities m_capabilities;
    
    // Error tracking
    DecodeError m_lastError;
    
    // Internal buffers
    IntelGEMObject* m_tempBuffer;      // Temporary decode buffer
    IntelGEMObject* m_sliceDataBuffer; // Slice data buffer
};

#endif /* INTEL_VIDEO_DECODER_H */
