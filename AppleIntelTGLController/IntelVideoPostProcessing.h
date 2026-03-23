/*
 * IntelVideoPostProcessing.h
 * Intel Graphics Driver
 *
 * Video post-processing engine (VPP Box) support for Intel Gen12+ GPUs.
 * Provides hardware-accelerated video processing: scaling, color conversion,
 * deinterlacing, denoising, sharpening, and composition.
 *
 * Week 32 - Phase 7: Video Engine (Final Week)
 */

#ifndef INTEL_VIDEO_POST_PROCESSING_H
#define INTEL_VIDEO_POST_PROCESSING_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "AppleIntelTGLController.h"
#include "IntelGEMObject.h"
#include "IntelRingBuffer.h"

// Forward declarations
class AppleIntelTGLController;
class IntelRingBuffer;

// Processing operation types
typedef enum {
    VPP_OP_SCALE = 0,           // Scaling (up/down)
    VPP_OP_COLOR_CONVERT,       // Color space conversion
    VPP_OP_DEINTERLACE,         // Deinterlacing
    VPP_OP_DENOISE,             // Noise reduction
    VPP_OP_SHARPEN,             // Sharpening
    VPP_OP_COMPOSE,             // Composition/blending
    VPP_OP_ROTATE,              // Rotation
    VPP_OP_MIRROR,              // Mirror/flip
    VPP_OP_CROP,                // Cropping
    VPP_OP_COUNT
} IntelVPPOperation;

// Scaling modes
typedef enum {
    SCALE_BILINEAR = 0,         // Bilinear interpolation
    SCALE_BICUBIC,              // Bicubic interpolation
    SCALE_LANCZOS,              // Lanczos (best quality)
    SCALE_NEAREST,              // Nearest neighbor (fast)
    SCALE_COUNT
} IntelScaleMode;

// Color spaces
typedef enum {
    COLOR_SPACE_BT601 = 0,      // BT.601 (SD)
    COLOR_SPACE_BT709,          // BT.709 (HD)
    COLOR_SPACE_BT2020,         // BT.2020 (UHD/HDR)
    COLOR_SPACE_sRGB,           // sRGB
    COLOR_SPACE_COUNT
} IntelColorSpace;

// Deinterlace modes
typedef enum {
    DEINTERLACE_BOB = 0,        // Bob (field doubling)
    DEINTERLACE_WEAVE,          // Weave (combine fields)
    DEINTERLACE_MOTION_ADAPTIVE, // Motion adaptive
    DEINTERLACE_MOTION_COMPENSATED, // Motion compensated (best)
    DEINTERLACE_COUNT
} IntelDeinterlaceMode;

// Rotation angles
typedef enum {
    ROTATE_NONE = 0,            // No rotation
    ROTATE_90,                  // 90 degrees CW
    ROTATE_180,                 // 180 degrees
    ROTATE_270,                 // 270 degrees CW
    ROTATE_COUNT
} IntelRotation;

// Blend modes
typedef enum {
    BLEND_ALPHA = 0,            // Alpha blending
    BLEND_ADDITIVE,             // Additive
    BLEND_MULTIPLY,             // Multiply
    BLEND_SCREEN,               // Screen
    BLEND_COUNT
} IntelBlendMode;

// VPP error codes
typedef enum {
    VPP_SUCCESS = 0,
    VPP_ERROR_INVALID_PARAMS,
    VPP_ERROR_UNSUPPORTED_FORMAT,
    VPP_ERROR_UNSUPPORTED_OPERATION,
    VPP_ERROR_NO_MEMORY,
    VPP_ERROR_HARDWARE_FAULT,
    VPP_ERROR_TIMEOUT,
    VPP_ERROR_INVALID_SIZE,
    VPP_ERROR_COUNT
} VPPError;

// Video surface description
typedef struct {
    uint32_t width;              // Width in pixels
    uint32_t height;             // Height in pixels
    uint32_t stride;             // Row stride in bytes
    uint32_t format;             // Pixel format (NV12, ARGB, etc)
    IntelColorSpace colorSpace;  // Color space
    bool interlaced;             // Interlaced?
    uint32_t bitDepth;           // Bit depth (8, 10, 12)
} SurfaceDesc;

// Rectangle structure
typedef struct {
    int32_t x;                   // X coordinate
    int32_t y;                   // Y coordinate
    uint32_t width;              // Width
    uint32_t height;             // Height
} VPPRect;

// Scaling parameters
typedef struct {
    IntelScaleMode mode;         // Scaling mode
    VPPRect srcRect;             // Source rectangle
    VPPRect dstRect;             // Destination rectangle
    float sharpness;             // Sharpness (0.0-1.0)
    bool aspectRatioCorrection;  // Maintain aspect ratio?
} ScaleParams;

// Color conversion parameters
typedef struct {
    IntelColorSpace srcColorSpace;  // Source color space
    IntelColorSpace dstColorSpace;  // Destination color space
    float brightness;            // Brightness (-1.0 to 1.0)
    float contrast;              // Contrast (0.0 to 2.0)
    float saturation;            // Saturation (0.0 to 2.0)
    float hue;                   // Hue rotation (-180 to 180)
} ColorConvertParams;

// Deinterlace parameters
typedef struct {
    IntelDeinterlaceMode mode;   // Deinterlace mode
    bool topFieldFirst;          // Top field first?
    uint32_t numRefFrames;       // Number of reference frames
} DeinterlaceParams;

// Denoise parameters
typedef struct {
    float strength;              // Denoise strength (0.0-1.0)
    bool temporal;               // Use temporal filtering?
    uint32_t numRefFrames;       // Number of reference frames
} DenoiseParams;

// Sharpen parameters
typedef struct {
    float strength;              // Sharpen strength (0.0-1.0)
    float radius;                // Sharpen radius (pixels)
    float threshold;             // Edge threshold
} SharpenParams;

// Composition layer
typedef struct {
    IntelGEMObject* surface;     // Surface buffer
    VPPRect srcRect;             // Source rectangle
    VPPRect dstRect;             // Destination rectangle
    float alpha;                 // Global alpha (0.0-1.0)
    IntelBlendMode blendMode;    // Blend mode
    int32_t zOrder;              // Z-order (lower = behind)
} CompositionLayer;

// Composition parameters
typedef struct {
    CompositionLayer* layers;    // Array of layers
    uint32_t numLayers;          // Number of layers
    uint32_t backgroundColor;    // Background color (ARGB)
} ComposeParams;

// Rotation/mirror parameters
typedef struct {
    IntelRotation rotation;      // Rotation angle
    bool mirrorHorizontal;       // Mirror horizontally?
    bool mirrorVertical;         // Mirror vertically?
} TransformParams;

// Processing parameters
typedef struct {
    IntelVPPOperation operation; // Operation type
    
    // Input/output surfaces
    IntelGEMObject* inputSurface;   // Input surface
    SurfaceDesc inputDesc;          // Input description
    IntelGEMObject* outputSurface;  // Output surface
    SurfaceDesc outputDesc;         // Output description
    
    // Operation-specific parameters
    union {
        ScaleParams scale;
        ColorConvertParams colorConvert;
        DeinterlaceParams deinterlace;
        DenoiseParams denoise;
        SharpenParams sharpen;
        ComposeParams compose;
        TransformParams transform;
    } params;
    
    // Reference frames (for temporal operations)
    IntelGEMObject* refFrames[8];   // Reference frames
    uint32_t numRefFrames;          // Number of refs
    
    // Quality/performance
    uint32_t quality;            // Quality level (0-100)
    bool lowLatency;             // Low latency mode?
} VPPParams;

// Processing statistics
typedef struct {
    // Performance
    uint64_t framesProcessed;    // Total frames processed
    uint64_t totalTime;          // Total processing time (ns)
    uint64_t averageTime;        // Average per frame (ns)
    float framesPerSecond;       // Processing FPS
    
    // Operations
    uint64_t scaleOps;           // Scaling operations
    uint64_t colorConvertOps;    // Color conversions
    uint64_t deinterlaceOps;     // Deinterlace operations
    uint64_t denoiseOps;         // Denoise operations
    uint64_t sharpenOps;         // Sharpen operations
    uint64_t composeOps;         // Composition operations
    uint64_t transformOps;       // Transform operations
    
    // Quality
    uint32_t errors;             // Processing errors
    float averageQuality;        // Average quality metric
    
    // Hardware utilization
    float hardwareUsage;         // VPP box usage (%)
    float bandwidthUsage;        // Memory bandwidth (%)
} VPPStatistics;

// Hardware capabilities
typedef struct {
    bool scalingSupported;       // Scaling support
    bool colorConvertSupported;  // Color conversion support
    bool deinterlaceSupported;   // Deinterlacing support
    bool denoiseSupported;       // Denoising support
    bool sharpenSupported;       // Sharpening support
    bool compositionSupported;   // Composition support
    bool rotationSupported;      // Rotation support
    
    uint32_t maxInputWidth;      // Max input width
    uint32_t maxInputHeight;     // Max input height
    uint32_t maxOutputWidth;     // Max output width
    uint32_t maxOutputHeight;    // Max output height
    float maxScaleFactor;        // Max scale factor
    uint32_t maxLayers;          // Max composition layers
    
    bool bit10Supported;         // 10-bit support
    bool hdrSupported;           // HDR support
    bool temporalDenoiseSupported; // Temporal denoise
} VPPCapabilities;

// VPP Box registers (Gen12)
#define VPP_REG_BASE                0x1E0000
#define VPP_CONTROL                 (VPP_REG_BASE + 0x0000)
#define VPP_STATUS                  (VPP_REG_BASE + 0x0004)
#define VPP_COMMAND                 (VPP_REG_BASE + 0x0008)
#define VPP_ERROR                   (VPP_REG_BASE + 0x000C)
#define VPP_INPUT_PTR               (VPP_REG_BASE + 0x0010)
#define VPP_INPUT_SIZE              (VPP_REG_BASE + 0x0014)
#define VPP_OUTPUT_PTR              (VPP_REG_BASE + 0x0018)
#define VPP_OUTPUT_SIZE             (VPP_REG_BASE + 0x001C)
#define VPP_SRC_RECT                (VPP_REG_BASE + 0x0020)
#define VPP_DST_RECT                (VPP_REG_BASE + 0x0024)
#define VPP_SCALE_FACTOR            (VPP_REG_BASE + 0x0028)
#define VPP_COLOR_MATRIX            (VPP_REG_BASE + 0x0030)
#define VPP_DENOISE_STRENGTH        (VPP_REG_BASE + 0x0040)
#define VPP_SHARPEN_STRENGTH        (VPP_REG_BASE + 0x0044)
#define VPP_LAYER_BASE              (VPP_REG_BASE + 0x0100)

// VPP Control bits
#define VPP_CONTROL_ENABLE          (1 << 0)
#define VPP_CONTROL_RESET           (1 << 1)
#define VPP_CONTROL_OP_MASK         (0xF << 4)
#define VPP_CONTROL_OP_SCALE        (0 << 4)
#define VPP_CONTROL_OP_COLOR        (1 << 4)
#define VPP_CONTROL_OP_DEINTERLACE  (2 << 4)
#define VPP_CONTROL_OP_DENOISE      (3 << 4)
#define VPP_CONTROL_OP_SHARPEN      (4 << 4)
#define VPP_CONTROL_OP_COMPOSE      (5 << 4)

// VPP Status bits
#define VPP_STATUS_IDLE             (1 << 0)
#define VPP_STATUS_BUSY             (1 << 1)
#define VPP_STATUS_DONE             (1 << 2)
#define VPP_STATUS_ERROR            (1 << 3)

// Constants
#define MAX_COMPOSITION_LAYERS      16
#define MAX_REFERENCE_FRAMES        8
#define VPP_TIMEOUT_MS              1000

class IntelVideoPostProcessing : public OSObject {
    OSDeclareDefaultStructors(IntelVideoPostProcessing)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    bool initWithController(AppleIntelTGLController* controller);
    
    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const { return m_running; }
    
    // Processing operations
    VPPError process(const VPPParams* params);
    VPPError scale(IntelGEMObject* input, IntelGEMObject* output,
                  const ScaleParams* params);
    VPPError colorConvert(IntelGEMObject* input, IntelGEMObject* output,
                         const ColorConvertParams* params);
    VPPError deinterlace(IntelGEMObject* input, IntelGEMObject* output,
                        const DeinterlaceParams* params);
    VPPError denoise(IntelGEMObject* input, IntelGEMObject* output,
                    const DenoiseParams* params);
    VPPError sharpen(IntelGEMObject* input, IntelGEMObject* output,
                    const SharpenParams* params);
    VPPError compose(const ComposeParams* params, IntelGEMObject* output);
    VPPError transform(IntelGEMObject* input, IntelGEMObject* output,
                      const TransformParams* params);
    
    void waitForCompletion(uint32_t timeoutMs = VPP_TIMEOUT_MS);
    void flush();
    
    // Format support
    bool isOperationSupported(IntelVPPOperation op) const;
    bool isFormatSupported(uint32_t format) const;
    VPPError validateParams(const VPPParams* params);
    
    // Statistics
    void getStatistics(VPPStatistics* stats);
    void resetStatistics();
    void printStatistics();
    
    // Capabilities
    void getCapabilities(VPPCapabilities* caps);
    uint32_t getMaxInputWidth() const;
    uint32_t getMaxInputHeight() const;
    float getMaxScaleFactor() const;
    
    // Hardware control
    bool enableHardware();
    void disableHardware();
    void resetHardware();
    bool isHardwareIdle();
    
    // Error handling
    const char* getErrorString(VPPError error);
    VPPError getLastError() const { return m_lastError; }
    
private:
    // Hardware access
    void writeRegister(uint32_t reg, uint32_t value);
    uint32_t readRegister(uint32_t reg);
    void setOperationMode(IntelVPPOperation op);
    
    // Operation implementation
    VPPError processScale(const VPPParams* params);
    VPPError processColorConvert(const VPPParams* params);
    VPPError processDeinterlace(const VPPParams* params);
    VPPError processDenoise(const VPPParams* params);
    VPPError processSharpen(const VPPParams* params);
    VPPError processCompose(const VPPParams* params);
    VPPError processTransform(const VPPParams* params);
    
    // Helper functions
    void setupScaling(const ScaleParams* params);
    void setupColorMatrix(const ColorConvertParams* params);
    void setupDeinterlace(const DeinterlaceParams* params);
    void setupLayers(const CompositionLayer* layers, uint32_t numLayers);
    VPPError setupSurfaces(IntelGEMObject* input, IntelGEMObject* output,
                          const SurfaceDesc* inputDesc,
                          const SurfaceDesc* outputDesc);
    
    // Buffer management
    VPPError allocateInternalBuffers();
    void freeInternalBuffers();
    
    // Statistics tracking
    void updateStatistics(uint64_t processTime, VPPError error);
    
    // Hardware detection
    void detectCapabilities();
    
    // Member variables
    AppleIntelTGLController* m_controller;
    IntelRingBuffer* m_ringBuffer;     // VECS ring
    bool m_running;
    
    // Statistics
    VPPStatistics m_stats;
    IOLock* m_statsLock;
    uint64_t m_lastProcessTime;
    
    // Capabilities
    VPPCapabilities m_capabilities;
    
    // Error tracking
    VPPError m_lastError;
    
    // Internal buffers
    IntelGEMObject* m_tempBuffer;      // Temporary processing buffer
    IntelGEMObject* m_layerBuffer;     // Layer composition buffer
};

#endif /* INTEL_VIDEO_POST_PROCESSING_H */
