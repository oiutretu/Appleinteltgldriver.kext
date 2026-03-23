//
//  IntelPerformance.h
//  Graphics Driver
//
//  Performance profiling and tuning system
//  Week 29 - Phase 6 (Acceleration) - Final Week!
//

#ifndef IntelPerformance_h
#define IntelPerformance_h

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOTimerEventSource.h>

// Forward declarations
class AppleIntelTGLController;
class IntelRingBuffer;
class IntelRequest;
class IntelGEMObject;

// Performance counter types
typedef enum {
    COUNTER_GPU_CYCLES = 0,       // GPU cycles elapsed
    COUNTER_SHADER_OPS,           // Shader operations
    COUNTER_MEMORY_READS,         // Memory read operations
    COUNTER_MEMORY_WRITES,        // Memory write operations
    COUNTER_CACHE_HITS,           // Cache hits
    COUNTER_CACHE_MISSES,         // Cache misses
    COUNTER_STALLS,               // Pipeline stalls
    COUNTER_TRIANGLES,            // Triangles processed
    COUNTER_VERTICES,             // Vertices processed
    COUNTER_PIXELS,               // Pixels rendered
    COUNTER_COMPUTE_THREADS,      // Compute threads executed
    COUNTER_BANDWIDTH_USED,       // Memory bandwidth used
    COUNTER_COUNT
} IntelCounterType;

// Profiling scopes
typedef enum {
    SCOPE_FRAME = 0,              // Per-frame profiling
    SCOPE_DRAW_CALL,              // Per-draw-call profiling
    SCOPE_DISPATCH,               // Per-dispatch profiling
    SCOPE_COMMAND,                // Per-command profiling
    SCOPE_COUNT
} IntelProfileScope;

// Bottleneck types
typedef enum {
    BOTTLENECK_NONE = 0,          // No bottleneck
    BOTTLENECK_CPU,               // CPU-bound
    BOTTLENECK_GPU,               // GPU-bound
    BOTTLENECK_MEMORY,            // Memory-bound
    BOTTLENECK_BANDWIDTH,         // Bandwidth-bound
    BOTTLENECK_CACHE,             // Cache-bound
    BOTTLENECK_VERTEX_SHADER,     // Vertex shader-bound
    BOTTLENECK_PIXEL_SHADER,      // Pixel shader-bound
    BOTTLENECK_COMPUTE,           // Compute shader-bound
    BOTTLENECK_COUNT
} IntelBottleneckType;

// Optimization recommendations
typedef enum {
    OPT_REDUCE_DRAW_CALLS = 0,    // Batch more geometry
    OPT_REDUCE_STATE_CHANGES,     // Minimize state switches
    OPT_OPTIMIZE_SHADERS,         // Simplify shader code
    OPT_REDUCE_OVERDRAW,          // Z-prepass or sorting
    OPT_USE_COMPRESSION,          // Enable framebuffer compression
    OPT_USE_TILING,               // Use tiled resources
    OPT_REDUCE_MEMORY_TRAFFIC,    // Optimize data access patterns
    OPT_INCREASE_CACHE_USAGE,     // Improve cache locality
    OPT_ENABLE_POOLING,           // Use buffer pooling
    OPT_ADJUST_LOD,               // Level-of-detail optimization
    OPT_COUNT
} IntelOptimization;

// Performance profile
struct PerformanceProfile {
    char name[64];                // Profile name
    IntelProfileScope scope;      // Profiling scope
    uint64_t startTime;           // Start timestamp
    uint64_t endTime;             // End timestamp
    uint64_t duration;            // Duration (ns)
    uint64_t gpuCycles;           // GPU cycles used
    uint64_t counters[COUNTER_COUNT]; // Hardware counters
    float gpuUtilization;         // GPU utilization (%)
    float memoryBandwidthUsed;    // Memory bandwidth (GB/s)
    float cacheHitRate;           // Cache hit rate (%)
};

// Bottleneck analysis
struct BottleneckAnalysis {
    IntelBottleneckType primary;  // Primary bottleneck
    IntelBottleneckType secondary; // Secondary bottleneck
    float confidence;             // Confidence level (0-1)
    char description[256];        // Human-readable description
    IntelOptimization recommendations[8]; // Optimization suggestions
    uint32_t numRecommendations;  // Number of recommendations
};

// Performance statistics
struct PerformanceStats {
    // Frame statistics
    uint64_t totalFrames;         // Total frames rendered
    uint32_t currentFPS;          // Current FPS
    uint32_t averageFPS;          // Average FPS
    uint32_t minFPS;              // Minimum FPS
    uint32_t maxFPS;              // Maximum FPS
    uint64_t frameTimeUs;         // Current frame time (uss)
    uint64_t averageFrameTimeUs;  // Average frame time (uss)
    
    // GPU statistics
    float gpuUtilization;         // GPU utilization (%)
    float averageGpuUtilization;  // Average GPU util (%)
    uint64_t gpuCycles;           // Total GPU cycles
    uint64_t idleCycles;          // Idle cycles
    
    // Memory statistics
    float memoryBandwidth;        // Current bandwidth (GB/s)
    float peakBandwidth;          // Peak bandwidth (GB/s)
    float averageBandwidth;       // Average bandwidth (GB/s)
    uint64_t bytesRead;           // Bytes read from memory
    uint64_t bytesWritten;        // Bytes written to memory
    
    // Cache statistics
    uint64_t cacheHits;           // Cache hits
    uint64_t cacheMisses;         // Cache misses
    float cacheHitRate;           // Hit rate (%)
    
    // Pipeline statistics
    uint64_t drawCalls;           // Draw calls
    uint64_t triangles;           // Triangles
    uint64_t vertices;            // Vertices
    uint64_t pixels;              // Pixels rendered
    uint64_t computeDispatches;   // Compute dispatches
    
    // Bottleneck analysis
    IntelBottleneckType bottleneck; // Current bottleneck
    float bottleneckSeverity;     // Severity (0-1)
};

// Performance tuning parameters
struct TuningParams {
    // Batching parameters
    uint32_t maxBatchSize;        // Max batch size (KB)
    uint32_t minBatchSize;        // Min batch size (KB)
    uint32_t batchCoalescingDelay; // Delay (uss)
    
    // Cache parameters
    bool enableL3Cache;           // L3 cache enabled
    bool enableCompression;       // Compression enabled
    bool enableTiling;            // Tiling enabled
    
    // Memory parameters
    bool enablePooling;           // Buffer pooling
    uint32_t poolSize;            // Pool size
    bool enablePrefetching;       // Memory prefetching
    
    // Scheduling parameters
    uint32_t commandBufferSize;   // Command buffer size
    uint32_t priorityLevels;      // Priority levels
    bool enablePreemption;        // Preemption support
    
    // Power parameters
    bool enableRC6;               // Render standby
    bool enableTurbo;             // Turbo boost
    uint32_t minFrequency;        // Min frequency (MHz)
    uint32_t maxFrequency;        // Max frequency (MHz)
};

// Benchmark results
struct BenchmarkResults {
    char name[64];                // Benchmark name
    uint32_t score;               // Overall score
    float fps;                    // Frames per second
    float trianglesPerSec;        // Million triangles/sec
    float pixelsPerSec;           // Gigapixels/sec
    float bandwidthGBps;          // Memory bandwidth GB/s
    uint64_t durationMs;          // Duration (ms)
    bool passed;                  // Pass/fail
};

// Hardware counter registers (Gen12)
#define OA_UNIT_BASE              0x00002B00
#define OA_CONTROL                (OA_UNIT_BASE + 0x00)
#define OA_STATUS                 (OA_UNIT_BASE + 0x04)
#define OA_HEAD                   (OA_UNIT_BASE + 0x08)
#define OA_TAIL                   (OA_UNIT_BASE + 0x0C)
#define OA_BUFFER_ADDR            (OA_UNIT_BASE + 0x10)

#define PERF_CNT_1                0x00002750
#define PERF_CNT_2                0x00002754
#define GPU_BUSY_TIME             0x00002248
#define MEMORY_BANDWIDTH_USED     0x00002750

// Performance tuning constants
#define MAX_PROFILES              1024
#define PROFILE_HISTORY_SIZE      100
#define BOTTLENECK_THRESHOLD      0.7f  // 70% threshold
#define FPS_SAMPLE_INTERVAL_MS    1000  // 1 second
#define COUNTER_POLL_INTERVAL_MS  16    // 16ms (~60Hz)

class IntelPerformance : public OSObject {
    OSDeclareDefaultStructors(IntelPerformance)
    
public:
    // Initialization
    virtual bool init() override;
    virtual void free() override;
    bool initWithController(AppleIntelTGLController* controller);
    
    // Lifecycle
    bool start();
    void stop();
    
    // Profiling
    uint32_t beginProfile(const char* name, IntelProfileScope scope);
    void endProfile(uint32_t profileId);
    void getProfile(uint32_t profileId, PerformanceProfile* profile);
    void printProfile(uint32_t profileId);
    
    // Performance counters
    uint64_t readCounter(IntelCounterType type);
    void resetCounters();
    void enableCounter(IntelCounterType type);
    void disableCounter(IntelCounterType type);
    
    // Statistics
    void getStatistics(PerformanceStats* stats);
    void resetStatistics();
    void printStatistics();
    void updateStatistics();
    
    // Bottleneck analysis
    void analyzeBottlenecks(BottleneckAnalysis* analysis);
    IntelBottleneckType detectBottleneck();
    void getOptimizationRecommendations(IntelOptimization* recommendations,
                                       uint32_t maxRecommendations,
                                       uint32_t* numRecommendations);
    
    // Performance tuning
    void getTuningParams(TuningParams* params);
    void setTuningParams(const TuningParams* params);
    void autoTune();
    void applyOptimization(IntelOptimization opt);
    void resetTuning();
    
    // Benchmarking
    bool runBenchmark(const char* name, BenchmarkResults* results);
    void runAllBenchmarks();
    void printBenchmarkResults();
    
    // Frame timing
    void beginFrame();
    void endFrame();
    uint32_t getCurrentFPS();
    uint64_t getAverageFrameTime();
    
    // GPU utilization
    float getGPUUtilization();
    float getMemoryBandwidth();
    float getCacheHitRate();
    
    // Heat map generation
    void generateHeatMap(uint32_t width, uint32_t height, uint8_t* output);
    void captureFrameProfile();
    
private:
    AppleIntelTGLController* controller;
    IORecursiveLock* lock;
    IOTimerEventSource* updateTimer;
    
    // Profiling state
    PerformanceProfile profiles[MAX_PROFILES];
    uint32_t numProfiles;
    uint32_t currentProfileId;
    
    // Statistics
    PerformanceStats stats;
    uint64_t statStartTime;
    
    // Frame timing
    uint64_t frameStartTime;
    uint64_t lastFrameTime;
    uint32_t frameHistory[PROFILE_HISTORY_SIZE];
    uint32_t frameHistoryIndex;
    
    // Tuning parameters
    TuningParams tuningParams;
    TuningParams defaultParams;
    
    // Bottleneck detection
    IntelBottleneckType lastBottleneck;
    float bottleneckHistory[PROFILE_HISTORY_SIZE];
    uint32_t bottleneckHistoryIndex;
    
    // Hardware counter state
    bool countersEnabled[COUNTER_COUNT];
    uint64_t counterBaseline[COUNTER_COUNT];
    
    // Benchmark results
    BenchmarkResults benchmarkResults[16];
    uint32_t numBenchmarks;
    
    // Counter reading
    uint64_t readHardwareCounter(uint32_t counterReg);
    void writeHardwareCounter(uint32_t counterReg, uint64_t value);
    void configureCounters();
    
    // Bottleneck detection helpers
    float calculateGPUUtilization();
    float calculateMemoryBandwidth();
    float calculateCacheHitRate();
    bool isGPUBound();
    bool isCPUBound();
    bool isMemoryBound();
    bool isBandwidthBound();
    bool isCacheBound();
    bool isShaderBound(const char* shaderType);
    
    // Optimization helpers
    void optimizeDrawCalls();
    void optimizeStateChanges();
    void optimizeMemoryAccess();
    void optimizeCacheUsage();
    void optimizeShaders();
    
    // Tuning helpers
    void tuneForLatency();
    void tuneForThroughput();
    void tuneForPower();
    void tuneForBalance();
    void applyTuningParams();
    
    // Benchmarks
    bool runTriangleBenchmark(BenchmarkResults* results);
    bool runPixelBenchmark(BenchmarkResults* results);
    bool runComputeBenchmark(BenchmarkResults* results);
    bool runBandwidthBenchmark(BenchmarkResults* results);
    
    // Statistics helpers
    void updateFPSStats();
    void updateGPUStats();
    void updateMemoryStats();
    void updateCacheStats();
    void updatePipelineStats();
    
    // Timer callback
    static void updateTimerFired(OSObject* owner, IOTimerEventSource* timer);
    
    // Validation
    bool validateProfileId(uint32_t profileId);
    bool validateScope(IntelProfileScope scope);
    
    
    
#include <libkern/libkern.h>

static inline double fmax(double a, double b) {
    return (a > b) ? a : b;
}
    
    
    
};

#endif /* IntelPerformance_h */
