//
//  IntelBlitter.h
// Graphics Driver
//
//  Week 25: 2D Blitter Acceleration
//  Implements BLT engine operations for hardware-accelerated 2D rendering
//
//  Created by Copilot on December 17, 2025.
//

#ifndef IntelBlitter_h
#define IntelBlitter_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>

// Forward declarations
class AppleIntelTGLController;
class IntelRingBuffer;
class IntelGEMObject;
class IntelContext;
class IntelRequest;

// BLT command opcodes (Gen12+)
#define MI_NOOP                     0x00000000
#define MI_BATCH_BUFFER_END         0x0A000000
#define MI_FLUSH_DW                 0x26000000

#define XY_COLOR_BLT                0x50000000  // Fast color fill
#define XY_SRC_COPY_BLT             0x53000000  // Screen-to-screen copy
#define XY_FULL_BLT                 0x54000000  // Full featured blit
#define XY_MONO_SRC_COPY_BLT        0x55000000  // Monochrome pattern
#define XY_SETUP_BLT                0x11000000  // Setup for tiled blits
#define XY_FAST_COPY_BLT            0x42000000  // Fast uncompressed copy

// BLT operation flags
#define BLT_ROP_COPY                0xCC        // Source copy (standard blit)
#define BLT_ROP_FILL                0xF0        // Solid fill
#define BLT_ROP_XOR                 0x66        // XOR operation
#define BLT_ROP_INVERT              0x55        // Invert destination

// Tiling modes
enum IntelTilingMode {
    TILING_NONE = 0,                // Linear (no tiling)
    TILING_X,                        // X-tiled (row-major 128-byte tiles)
    TILING_Y,                        // Y-tiled (column-major)
    TILING_Yf,                       // Y-tiled with flexed layout
    TILING_Ys                        // Y-tiled with small tiles
};

// Color formats
enum IntelColorFormat {
    FORMAT_RGB565 = 0,               // 16-bit RGB
    FORMAT_XRGB1555,                 // 15-bit RGB with X bit
    FORMAT_XRGB8888,                 // 32-bit RGB (8 bits per channel)
    FORMAT_ARGB8888,                 // 32-bit ARGB with alpha
    FORMAT_YUY2,                     // YUV 4:2:2 packed
    FORMAT_UYVY,                     // YUV 4:2:2 alternate
};

// Blit operation types
enum BlitOperation {
    BLIT_OP_FILL = 0,                // Solid color fill
    BLIT_OP_COPY,                    // Source to destination copy
    BLIT_OP_PATTERN,                 // Pattern fill
    BLIT_OP_STRETCH,                 // Stretch blit (scaling)
    BLIT_OP_ALPHA_BLEND,             // Alpha blending
    BLIT_OP_ROTATE,                  // Rotation (90/180/270)
};

// Blit error codes
enum BlitError {
    BLIT_SUCCESS = 0,
    BLIT_ERROR_INVALID_PARAMS,       // Invalid parameters
    BLIT_ERROR_SIZE_MISMATCH,        // Source/dest size mismatch
    BLIT_ERROR_FORMAT_UNSUPPORTED,   // Unsupported format
    BLIT_ERROR_TILING_MISMATCH,      // Incompatible tiling modes
    BLIT_ERROR_OUT_OF_BOUNDS,        // Rectangle out of bounds
    BLIT_ERROR_ENGINE_BUSY,          // BLT engine busy
    BLIT_ERROR_TIMEOUT,              // Operation timeout
    BLIT_ERROR_MEMORY_FAULT,         // GPU memory fault
};

// Surface descriptor
struct IntelSurface {
    IntelGEMObject* object;          // GEM object backing the surface
    uint32_t width;                  // Width in pixels
    uint32_t height;                 // Height in pixels
    uint32_t stride;                 // Pitch/stride in bytes
    uint32_t bpp;                    // Bits per pixel (16/24/32)
    IntelColorFormat format;         // Pixel format
    IntelTilingMode tiling;          // Tiling mode
    uint64_t offset;                 // Offset within GEM object
    uint64_t gpuAddress;             // GPU virtual address
};

// Rectangle descriptor
struct IntelRect {
    uint32_t x;                      // X coordinate
    uint32_t y;                      // Y coordinate
    uint32_t width;                  // Width in pixels
    uint32_t height;                 // Height in pixels
};

// Blit parameters
struct BlitParams {
    IntelSurface* src;               // Source surface (NULL for fill)
    IntelSurface* dst;               // Destination surface
    IntelRect srcRect;               // Source rectangle
    IntelRect dstRect;               // Destination rectangle
    uint32_t color;                  // Fill color (ARGB)
    uint8_t rop;                     // Raster operation
    BlitOperation operation;         // Operation type
    bool enableAlpha;                // Alpha blending enabled
    uint8_t alphaValue;              // Constant alpha (0-255)
};

// BLT engine statistics
struct BlitterStats {
    uint64_t totalBlits;             // Total blit operations
    uint64_t fillOperations;         // Fast fill count
    uint64_t copyOperations;         // Copy blit count
    uint64_t alphaBlendOps;          // Alpha blend count
    uint64_t totalPixelsProcessed;   // Total pixels blitted
    uint64_t totalBytesTransferred;  // Total data transferred
    uint32_t averageLatencyUs;       // Average blit latency
    uint32_t maxLatencyUs;           // Maximum latency
    uint32_t engineUtilization;      // BLT engine utilization (%)
    uint32_t errors;                 // Error count
};

// IntelBlitter class
class IntelBlitter : public OSObject {
    OSDeclareDefaultStructors(IntelBlitter)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    bool initWithController(AppleIntelTGLController* controller);
    bool start();
    void stop();
    
    // Basic blit operations
    BlitError fillRect(IntelSurface* dst, const IntelRect* rect, uint32_t color);
    BlitError copyRect(IntelSurface* src, IntelSurface* dst,
                      const IntelRect* srcRect, const IntelRect* dstRect);
    BlitError stretchBlit(IntelSurface* src, IntelSurface* dst,
                         const IntelRect* srcRect, const IntelRect* dstRect);
    
    // Advanced operations
    BlitError alphaBlend(IntelSurface* src, IntelSurface* dst,
                        const IntelRect* srcRect, const IntelRect* dstRect,
                        uint8_t alphaValue);
    BlitError patternFill(IntelSurface* dst, const IntelRect* rect,
                         uint32_t* pattern, uint32_t patternSize);
    BlitError rotateBlit(IntelSurface* src, IntelSurface* dst,
                        uint32_t angle);  // 90/180/270 degrees
    
    // Generic blit with full parameters
    BlitError blit(const BlitParams* params);
    
    // Fast paths for common operations
    BlitError fastClearScreen(IntelSurface* surface, uint32_t color);
    BlitError fastScrollScreen(IntelSurface* surface, int32_t dx, int32_t dy);
    BlitError fastUpdateRegion(IntelSurface* dst, IntelSurface* src,
                              const IntelRect* region);
    
    // Synchronization
    BlitError waitForIdle(uint32_t timeoutMs);
    bool isIdle();
    void flush();
    
    // Surface management
    IntelSurface* createSurface(uint32_t width, uint32_t height,
                               IntelColorFormat format, IntelTilingMode tiling);
    void destroySurface(IntelSurface* surface);
    bool validateSurface(IntelSurface* surface);
    
    // Format conversion
    uint32_t getBytesPerPixel(IntelColorFormat format);
    uint32_t calculateStride(uint32_t width, IntelColorFormat format,
                            IntelTilingMode tiling);
    bool areFormatsCompatible(IntelColorFormat src, IntelColorFormat dst);
    
    // Tiling support
    uint64_t getTiledOffset(IntelSurface* surface, uint32_t x, uint32_t y);
    bool canUseFastCopy(IntelSurface* src, IntelSurface* dst);
    
    // Statistics
    void getStatistics(BlitterStats* stats);
    void resetStatistics();
    void printStatistics();
    
    // Hardware detection
    bool supportsAlphaBlending();
    bool supportsCompression();
    uint32_t getMaxBlitSize();  // Maximum pixels per blit
    
private:
    // Command generation
    uint32_t* buildColorBlt(uint32_t* cmd, const BlitParams* params);
    uint32_t* buildSrcCopyBlt(uint32_t* cmd, const BlitParams* params);
    uint32_t* buildFullBlt(uint32_t* cmd, const BlitParams* params);
    uint32_t* buildFastCopyBlt(uint32_t* cmd, IntelSurface* src,
                              IntelSurface* dst, const IntelRect* rect);
    
    // Command submission
    BlitError submitBlitCommand(uint32_t* commands, uint32_t numDwords,
                               IntelRequest** requestOut);
    BlitError waitForCompletion(IntelRequest* request, uint32_t timeoutMs);
    
    // Validation
    bool validateBlitParams(const BlitParams* params);
    bool validateRectangle(const IntelRect* rect, uint32_t maxWidth,
                          uint32_t maxHeight);
    bool validateAlignment(IntelSurface* surface);
    
    // Format helpers
    uint32_t getFormatBitsPerPixel(IntelColorFormat format);
    uint32_t convertColorFormat(uint32_t color, IntelColorFormat srcFormat,
                               IntelColorFormat dstFormat);
    
    // Tiling calculations
    void getTileSize(IntelTilingMode tiling, uint32_t* width, uint32_t* height);
    uint32_t alignToTile(uint32_t value, IntelTilingMode tiling);
    
    // Performance optimization
    bool shouldUseFastPath(const BlitParams* params);
    void optimizeBlitParams(BlitParams* params);
    uint32_t estimateBlitTime(const BlitParams* params);
    
    // Statistics tracking
    void recordBlitStart();
    void recordBlitComplete(uint64_t pixelsProcessed, uint64_t bytesTransferred);
    void recordError(BlitError error);
    
    // Member variables
    AppleIntelTGLController* controller;
    IntelRingBuffer* bltRing;        // BLT engine ring buffer
    IntelContext* blitContext;       // Dedicated blit context
    
    IORecursiveLock* lock;           // Thread safety
    
    // Engine state
    bool engineInitialized;
    bool engineIdle;
    uint32_t lastBlitSeqno;          // Last blit sequence number
    
    // Statistics
    BlitterStats stats;
    uint64_t blitStartTime;
    
    // Hardware capabilities
    uint32_t maxBlitWidth;           // Max pixels per scanline
    uint32_t maxBlitHeight;          // Max scanlines
    bool supportsAlpha;
    bool hasCompressionSupport;      // Renamed to avoid conflict with method
    bool supportsFastCopy;
    
    // Configuration
    static const uint32_t MAX_BLIT_SIZE = 16384;  // 16K pixels max
    static const uint32_t BLIT_TIMEOUT_MS = 1000; // 1 second timeout
    static const uint32_t MAX_COMMAND_SIZE = 64;  // DWORDs per command
};

#endif /* IntelBlitter_h */
