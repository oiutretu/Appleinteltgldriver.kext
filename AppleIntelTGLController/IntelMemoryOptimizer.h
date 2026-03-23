//
//  IntelMemoryOptimizer.h
//  Graphics Driver
//
//  Memory optimization system for bandwidth and cache efficiency
//  Week 28 - Phase 6 (Acceleration)
//

#ifndef IntelMemoryOptimizer_h
#define IntelMemoryOptimizer_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOTimerEventSource.h>

// Forward declarations
class AppleIntelTGLController;
class IntelGEMObject;

// Memory tiling modes
typedef enum {
    TILING_NONE = 0,              // Linear (no tiling)
    TILING_X,                     // X-tiling (legacy)
    TILING_Y,                     // Y-tiling (Gen9+)
    TILING_YF,                    // YF-tiling (Gen9+ framebuffer)
    TILING_YS,                    // YS-tiling (Gen12+ small)
    TILING_COUNT
} IntelTilingMode;

// Compression types
typedef enum {
    COMPRESSION_NONE = 0,         // No compression
    COMPRESSION_RC,               // Render compression
    COMPRESSION_MC,               // Media compression
    COMPRESSION_CC,               // Clear color compression
    COMPRESSION_COUNT
} IntelCompressionType;

// Cache management policies
typedef enum {
    CACHE_POLICY_CACHED = 0,      // Fully cached
    CACHE_POLICY_UNCACHED,        // Bypass cache
    CACHE_POLICY_WRITE_COMBINE,   // Write combining
    CACHE_POLICY_WRITE_THROUGH,   // Write-through
    CACHE_POLICY_WRITE_BACK,      // Write-back (default)
    CACHE_POLICY_COUNT
} IntelCachePolicy;

// Memory optimization strategies
typedef enum {
    OPTIMIZE_BANDWIDTH = 0,       // Maximize bandwidth
    OPTIMIZE_LATENCY,             // Minimize latency
    OPTIMIZE_POWER,               // Minimize power
    OPTIMIZE_BALANCED,            // Balance all
    OPTIMIZE_COUNT
} IntelOptimizationStrategy;

// Buffer usage hints
typedef enum {
    BUFFER_USAGE_STATIC = 0,      // Rarely modified
    BUFFER_USAGE_DYNAMIC,         // Frequently modified
    BUFFER_USAGE_STREAMING,       // Write-once per frame
    BUFFER_USAGE_STAGING,         // CPU->GPU transfer
    BUFFER_USAGE_READBACK,        // GPU->CPU transfer
    BUFFER_USAGE_COUNT
} IntelBufferUsage;

// Compression parameters
struct CompressionParams {
    IntelCompressionType type;
    uint32_t auxPlaneOffset;      // AUX surface offset
    uint32_t auxPlanePitch;       // AUX surface pitch
    uint32_t clearColor;          // Fast clear color
    bool lossless;                // Lossless compression
    bool fastClear;               // Fast clear enabled
    uint32_t compressionRatio;    // Achieved ratio (%)
};

// Tiling parameters
struct TilingParams {
    IntelTilingMode mode;
    uint32_t stride;              // Row stride (bytes)
    uint32_t height;              // Height in tiles
    uint32_t offsetX;             // X offset in tiles
    uint32_t offsetY;             // Y offset in tiles
    uint32_t tileWidth;           // Tile width (pixels)
    uint32_t tileHeight;          // Tile height (lines)
};

// Memory pool for buffer reuse
struct MemoryPool {
    uint64_t minSize;             // Minimum buffer size
    uint64_t maxSize;             // Maximum buffer size
    uint32_t maxBuffers;          // Pool capacity
    uint32_t numFree;             // Free buffers
    uint32_t numAllocated;        // Allocated buffers
    IntelGEMObject** freeList;    // Free buffer list
    IORecursiveLock* lock;        // Pool lock
    uint64_t totalAllocations;    // Total allocations
    uint64_t totalReuses;         // Total reuses
};

// Memory optimization statistics
struct MemoryOptimizationStats {
    // Compression statistics
    uint64_t compressedBuffers;   // Buffers with compression
    uint64_t compressionSavings;  // Bytes saved
    uint32_t avgCompressionRatio; // Average ratio (%)
    
    // Tiling statistics
    uint64_t tiledBuffers;        // Buffers with tiling
    uint64_t bandwidthSavings;    // Bandwidth saved (%)
    
    // Cache statistics
    uint64_t cacheHits;           // Cache hits
    uint64_t cacheMisses;         // Cache misses
    uint32_t cacheHitRate;        // Hit rate (%)
    
    // Pool statistics
    uint64_t poolAllocations;     // Pool allocations
    uint64_t poolReuses;          // Pool reuses
    uint32_t poolReuseRate;       // Reuse rate (%)
    
    // Memory usage
    uint64_t totalMemoryUsed;     // Total memory (bytes)
    uint64_t peakMemoryUsed;      // Peak memory (bytes)
    uint64_t compressedMemory;    // Compressed memory
    uint64_t uncompressedMemory;  // Uncompressed memory
    
    // Performance metrics
    uint32_t avgAllocTimeUs;      // Avg allocation time
    uint32_t avgFreeTimeUs;       // Avg free time
    uint64_t totalAllocations;    // Total allocations
    uint64_t totalFrees;          // Total frees
};

// Memory optimization error codes
typedef enum {
    MEMORY_OPT_SUCCESS = 0,
    MEMORY_OPT_ERROR_INVALID_PARAMS,
    MEMORY_OPT_ERROR_NOT_SUPPORTED,
    MEMORY_OPT_ERROR_NO_MEMORY,
    MEMORY_OPT_ERROR_COMPRESSION_FAILED,
    MEMORY_OPT_ERROR_TILING_FAILED,
    MEMORY_OPT_ERROR_POOL_FULL,
    MEMORY_OPT_ERROR_INVALID_MODE,
    MEMORY_OPT_ERROR_HARDWARE,
    MEMORY_OPT_ERROR_COUNT
} MemoryOptError;

// Hardware compression registers (Gen12+)
#define PLANE_AUX_DIST(plane)     (0x701C0 + (plane) * 0x1000)
#define PLANE_AUX_OFFSET(plane)   (0x701C4 + (plane) * 0x1000)
#define PLANE_CUS_CTL(plane)      (0x701C8 + (plane) * 0x1000)
#define PLANE_CC_VAL(plane)       (0x701CC + (plane) * 0x1000)

// Tiling control registers
#define TILE_CTL                  0x101000
#define TILE_CTL_ENABLE           (1 << 0)
#define TILE_CTL_Y_TILING         (1 << 1)
#define TILE_CTL_YF_TILING        (1 << 2)

// Cache control registers
#define MOCS_TABLE_BASE           0x00004000
#define MOCS_ENTRY_SIZE           0x4
#define MOCS_NUM_ENTRIES          64

// Memory optimization constants
#define MAX_MEMORY_POOLS          8
#define MAX_POOL_SIZE             256
#define COMPRESSION_THRESHOLD     (4 * 1024 * 1024)  // 4MB
#define POOL_CLEANUP_INTERVAL_MS  5000               // 5 seconds

class IntelMemoryOptimizer : public OSObject {
    OSDeclareDefaultStructors(IntelMemoryOptimizer)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    bool initWithController(AppleIntelTGLController* controller);
    
    // Lifecycle
    bool start();
    void stop();
    
    // Compression management
    MemoryOptError enableCompression(IntelGEMObject* object,
                                    IntelCompressionType type,
                                    CompressionParams* params);
    MemoryOptError disableCompression(IntelGEMObject* object);
    bool isCompressionSupported(IntelCompressionType type);
    MemoryOptError compressBuffer(IntelGEMObject* object);
    MemoryOptError decompressBuffer(IntelGEMObject* object);
    bool isCompressed(IntelGEMObject* object);
    uint32_t getCompressionRatio(IntelGEMObject* object);
    
    // Tiling management
    MemoryOptError setTiling(IntelGEMObject* object,
                            IntelTilingMode mode,
                            TilingParams* params);
    IntelTilingMode getTiling(IntelGEMObject* object);
    bool isTilingSupported(IntelTilingMode mode);
    MemoryOptError calculateTilingParams(uint32_t width,
                                        uint32_t height,
                                        uint32_t bpp,
                                        IntelTilingMode mode,
                                        TilingParams* params);
    
    // Cache management
    MemoryOptError setCachePolicy(IntelGEMObject* object,
                                 IntelCachePolicy policy);
    IntelCachePolicy getCachePolicy(IntelGEMObject* object);
    MemoryOptError flushCache(IntelGEMObject* object);
    MemoryOptError invalidateCache(IntelGEMObject* object);
    void flushAllCaches();
    
    // Memory pool management
    MemoryOptError createPool(uint64_t minSize,
                             uint64_t maxSize,
                             uint32_t maxBuffers,
                             MemoryPool** poolOut);
    MemoryOptError destroyPool(MemoryPool* pool);
    IntelGEMObject* allocateFromPool(MemoryPool* pool,
                                     uint64_t size,
                                     IntelBufferUsage usage);
    MemoryOptError returnToPool(MemoryPool* pool,
                               IntelGEMObject* object);
    void trimPool(MemoryPool* pool, uint32_t targetSize);
    void flushPool(MemoryPool* pool);
    
    // Optimization strategies
    MemoryOptError setStrategy(IntelOptimizationStrategy strategy);
    IntelOptimizationStrategy getStrategy();
    MemoryOptError optimizeBuffer(IntelGEMObject* object,
                                 IntelBufferUsage usage);
    MemoryOptError defragment();
    
    // Buffer usage hints
    MemoryOptError setBufferUsage(IntelGEMObject* object,
                                 IntelBufferUsage usage);
    IntelBufferUsage getBufferUsage(IntelGEMObject* object);
    
    // Statistics
    void getStatistics(MemoryOptimizationStats* stats);
    void resetStatistics();
    void printStatistics();
    
    // Hardware capabilities
    bool supportsCompression();
    bool supportsTiling();
    bool supportsFastClear();
    uint32_t getMaxCompressionRatio();
    uint32_t getCacheLineSize();
    uint32_t getL3CacheSize();
    
private:
    AppleIntelTGLController* controller;
    IORecursiveLock* lock;
    IOTimerEventSource* cleanupTimer;
    
    // Optimization state
    IntelOptimizationStrategy currentStrategy;
    bool compressionEnabled;
    bool tilingEnabled;
    bool poolingEnabled;
    
    // Memory pools
    MemoryPool* pools[MAX_MEMORY_POOLS];
    uint32_t numPools;
    
    // Statistics
    MemoryOptimizationStats stats;
    uint64_t statStartTime;
    
    // Hardware capabilities
    bool hwSupportsRC;           // Render compression
    bool hwSupportsMC;           // Media compression
    bool hwSupportsCC;           // Clear color compression
    bool hwSupportsTilingY;      // Y-tiling
    bool hwSupportsTilingYF;     // YF-tiling
    uint32_t hwCacheLineSize;    // Cache line size
    uint32_t hwL3CacheSize;      // L3 cache size
    
    // Compression helpers
    MemoryOptError setupRenderCompression(IntelGEMObject* object,
                                         CompressionParams* params);
    MemoryOptError setupMediaCompression(IntelGEMObject* object,
                                        CompressionParams* params);
    MemoryOptError setupClearColorCompression(IntelGEMObject* object,
                                             CompressionParams* params);
    uint32_t* buildCompressionCommand(uint32_t* cmd,
                                     IntelGEMObject* object,
                                     CompressionParams* params);
    
    // Tiling helpers
    uint32_t calculateTileStride(uint32_t width,
                                uint32_t bpp,
                                IntelTilingMode mode);
    uint32_t calculateTileHeight(uint32_t height,
                                IntelTilingMode mode);
    void getTileDimensions(IntelTilingMode mode,
                          uint32_t* widthOut,
                          uint32_t* heightOut);
    
    // Cache helpers
    MemoryOptError configureMOCS(uint32_t index,
                                IntelCachePolicy policy);
    uint32_t getMOCSIndex(IntelGEMObject* object);
    uint32_t buildMOCSEntry(IntelCachePolicy policy);
    
    // Pool helpers
    MemoryPool* findPoolForSize(uint64_t size);
    void cleanupPoolsTimerFired(OSObject* owner, IOTimerEventSource* timer);
    bool canReuseBuffer(IntelGEMObject* object,
                       uint64_t requestedSize,
                       IntelBufferUsage usage);
    
    // Optimization helpers
    MemoryOptError applyBandwidthOptimization(IntelGEMObject* object);
    MemoryOptError applyLatencyOptimization(IntelGEMObject* object);
    MemoryOptError applyPowerOptimization(IntelGEMObject* object);
    MemoryOptError applyBalancedOptimization(IntelGEMObject* object);
    
    // Statistics recording
    void recordCompression(uint64_t originalSize,
                          uint64_t compressedSize);
    void recordAllocation(uint64_t size,
                         uint64_t durationUs,
                         bool fromPool);
    void recordFree(uint64_t size, uint64_t durationUs);
    void recordCacheAccess(bool hit);
    
    // Validation
    bool validateCompressionParams(IntelCompressionType type,
                                  CompressionParams* params);
    bool validateTilingParams(IntelTilingMode mode,
                             TilingParams* params);
    bool validatePoolParams(uint64_t minSize,
                           uint64_t maxSize,
                           uint32_t maxBuffers);
};

#endif /* IntelMemoryOptimizer_h */
