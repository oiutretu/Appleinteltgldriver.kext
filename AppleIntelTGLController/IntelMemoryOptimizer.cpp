//
//  IntelMemoryOptimizer.cpp
// macOS Driver
//
//  Memory optimization implementation
//  Week 28 - Phase 6 (Acceleration)
//

#include "IntelMemoryOptimizer.h"
#include "AppleIntelTGLController.h"
#include "IntelGEMObject.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelMemoryOptimizer, OSObject)

// Initialization
bool IntelMemoryOptimizer::init() {
    if (!super::init()) {
        return false;
    }
    
    controller = nullptr;
    lock = nullptr;
    cleanupTimer = nullptr;
    currentStrategy = OPTIMIZE_BALANCED;
    compressionEnabled = true;
    tilingEnabled = true;
    poolingEnabled = true;
    numPools = 0;
    
    for (uint32_t i = 0; i < MAX_MEMORY_POOLS; i++) {
        pools[i] = nullptr;
    }
    
    memset(&stats, 0, sizeof(stats));
    statStartTime = 0;
    
    // Hardware capabilities (Gen12)
    hwSupportsRC = true;
    hwSupportsMC = true;
    hwSupportsCC = true;
    hwSupportsTilingY = true;
    hwSupportsTilingYF = true;
    hwCacheLineSize = 64;        // 64 bytes
    hwL3CacheSize = 3 * 1024 * 1024; // 3MB
    
    return true;
}

void IntelMemoryOptimizer::free() {
    stop();
    
    // Free all pools
    for (uint32_t i = 0; i < numPools; i++) {
        if (pools[i]) {
            destroyPool(pools[i]);
        }
    }
    
    if (lock) {
        IORecursiveLockFree(lock);
        lock = nullptr;
    }
    
    controller = nullptr;
    
    super::free();
}

bool IntelMemoryOptimizer::initWithController(AppleIntelTGLController* ctrl) {
    if (!ctrl) {
        return false;
    }
    
    controller = ctrl;
    
    lock = IORecursiveLockAlloc();
    if (!lock) {
        return false;
    }
    
    return true;
}

// Lifecycle
bool IntelMemoryOptimizer::start() {
    IORecursiveLockLock(lock);
    
    // Create cleanup timer (5 second interval)
    cleanupTimer = IOTimerEventSource::timerEventSource(
        this,
        OSMemberFunctionCast(IOTimerEventSource::Action,
                           this,
                           &IntelMemoryOptimizer::cleanupPoolsTimerFired));
    
    if (cleanupTimer) {
        controller->getWorkLoop()->addEventSource(cleanupTimer);
        cleanupTimer->setTimeoutMS(POOL_CLEANUP_INTERVAL_MS);
    }
    
    // Initialize statistics
    statStartTime = mach_absolute_time();
    memset(&stats, 0, sizeof(stats));
    
    IORecursiveLockUnlock(lock);
    
    IOLog("IntelMemoryOptimizer: Started (strategy=%s)\n",
          currentStrategy == OPTIMIZE_BANDWIDTH ? "bandwidth" :
          currentStrategy == OPTIMIZE_LATENCY ? "latency" :
          currentStrategy == OPTIMIZE_POWER ? "power" : "balanced");
    
    return true;
}

void IntelMemoryOptimizer::stop() {
    IORecursiveLockLock(lock);
    
    if (cleanupTimer) {
        cleanupTimer->cancelTimeout();
        controller->getWorkLoop()->removeEventSource(cleanupTimer);
        cleanupTimer->release();
        cleanupTimer = nullptr;
    }
    
    // Flush all pools
    for (uint32_t i = 0; i < numPools; i++) {
        if (pools[i]) {
            flushPool(pools[i]);
        }
    }
    
    IORecursiveLockUnlock(lock);
    
    IOLog("IntelMemoryOptimizer: Stopped\n");
}

// Compression management
MemoryOptError IntelMemoryOptimizer::enableCompression(
    IntelGEMObject* object,
    IntelCompressionType type,
    CompressionParams* params)
{
    if (!object || !params) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    if (!validateCompressionParams(type, params)) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    MemoryOptError result = MEMORY_OPT_SUCCESS;
    
    switch (type) {
        case COMPRESSION_RC:
            result = setupRenderCompression(object, params);
            break;
        case COMPRESSION_MC:
            result = setupMediaCompression(object, params);
            break;
        case COMPRESSION_CC:
            result = setupClearColorCompression(object, params);
            break;
        default:
            result = MEMORY_OPT_ERROR_NOT_SUPPORTED;
    }
    
    if (result == MEMORY_OPT_SUCCESS) {
        stats.compressedBuffers++;
    }
    
    IORecursiveLockUnlock(lock);
    
    return result;
}

MemoryOptError IntelMemoryOptimizer::disableCompression(IntelGEMObject* object) {
    if (!object) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    // Decompress buffer first
    if (isCompressed(object)) {
        decompressBuffer(object);
    }
    
    // Clear compression state
    // (In production, would update object metadata)
    
    if (stats.compressedBuffers > 0) {
        stats.compressedBuffers--;
    }
    
    IORecursiveLockUnlock(lock);
    
    return MEMORY_OPT_SUCCESS;
}

bool IntelMemoryOptimizer::isCompressionSupported(IntelCompressionType type) {
    switch (type) {
        case COMPRESSION_RC:
            return hwSupportsRC;
        case COMPRESSION_MC:
            return hwSupportsMC;
        case COMPRESSION_CC:
            return hwSupportsCC;
        default:
            return false;
    }
}

MemoryOptError IntelMemoryOptimizer::compressBuffer(IntelGEMObject* object) {
    if (!object) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    uint64_t originalSize = object->getSize();
    
    // Simulate compression (production would use hardware)
    uint32_t ratio = 70; // 70% compression
    uint64_t compressedSize = (originalSize * ratio) / 100;
    
    recordCompression(originalSize, compressedSize);
    
    return MEMORY_OPT_SUCCESS;
}

MemoryOptError IntelMemoryOptimizer::decompressBuffer(IntelGEMObject* object) {
    if (!object) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    // Simulate decompression
    return MEMORY_OPT_SUCCESS;
}

bool IntelMemoryOptimizer::isCompressed(IntelGEMObject* object) {
    // In production, would check object metadata
    return false;
}

uint32_t IntelMemoryOptimizer::getCompressionRatio(IntelGEMObject* object) {
    if (!object || !isCompressed(object)) {
        return 100; // No compression
    }
    
    return stats.avgCompressionRatio;
}

// Tiling management
MemoryOptError IntelMemoryOptimizer::setTiling(
    IntelGEMObject* object,
    IntelTilingMode mode,
    TilingParams* params)
{
    if (!object || !params) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    if (!isTilingSupported(mode)) {
        return MEMORY_OPT_ERROR_NOT_SUPPORTED;
    }
    
    if (!validateTilingParams(mode, params)) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    // Set tiling mode in object metadata
    // (Production would update GEM object tiling)
    
    stats.tiledBuffers++;
    
    IORecursiveLockUnlock(lock);
    
    return MEMORY_OPT_SUCCESS;
}

IntelTilingMode IntelMemoryOptimizer::getTiling(IntelGEMObject* object) {
    if (!object) {
        return TILING_NONE;
    }
    
    // In production, would read from object metadata
    return TILING_NONE;
}

bool IntelMemoryOptimizer::isTilingSupported(IntelTilingMode mode) {
    switch (mode) {
        case TILING_NONE:
        case TILING_X:
            return true;
        case TILING_Y:
            return hwSupportsTilingY;
        case TILING_YF:
        case TILING_YS:
            return hwSupportsTilingYF;
        default:
            return false;
    }
}

MemoryOptError IntelMemoryOptimizer::calculateTilingParams(
    uint32_t width,
    uint32_t height,
    uint32_t bpp,
    IntelTilingMode mode,
    TilingParams* params)
{
    if (!params) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    if (!isTilingSupported(mode)) {
        return MEMORY_OPT_ERROR_NOT_SUPPORTED;
    }
    
    params->mode = mode;
    params->stride = calculateTileStride(width, bpp, mode);
    params->height = calculateTileHeight(height, mode);
    params->offsetX = 0;
    params->offsetY = 0;
    
    getTileDimensions(mode, &params->tileWidth, &params->tileHeight);
    
    return MEMORY_OPT_SUCCESS;
}

// Cache management
MemoryOptError IntelMemoryOptimizer::setCachePolicy(
    IntelGEMObject* object,
    IntelCachePolicy policy)
{
    if (!object) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    if (policy >= CACHE_POLICY_COUNT) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    // Get MOCS index for object
    uint32_t mocsIndex = getMOCSIndex(object);
    
    // Configure MOCS table entry
    MemoryOptError result = configureMOCS(mocsIndex, policy);
    
    IORecursiveLockUnlock(lock);
    
    return result;
}

IntelCachePolicy IntelMemoryOptimizer::getCachePolicy(IntelGEMObject* object) {
    if (!object) {
        return CACHE_POLICY_CACHED;
    }
    
    // In production, would read from MOCS table
    return CACHE_POLICY_CACHED;
}

MemoryOptError IntelMemoryOptimizer::flushCache(IntelGEMObject* object) {
    if (!object) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    // Issue cache flush command
    // (Production would use MI_FLUSH_DW command)
    
    return MEMORY_OPT_SUCCESS;
}

MemoryOptError IntelMemoryOptimizer::invalidateCache(IntelGEMObject* object) {
    if (!object) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    // Issue cache invalidate command
    // (Production would use PIPE_CONTROL command)
    
    return MEMORY_OPT_SUCCESS;
}

void IntelMemoryOptimizer::flushAllCaches() {
    // Flush all cache levels (L1, L2, L3)
    // (Production would use GPU commands)
}

// Memory pool management
MemoryOptError IntelMemoryOptimizer::createPool(
    uint64_t minSize,
    uint64_t maxSize,
    uint32_t maxBuffers,
    MemoryPool** poolOut)
{
    if (!poolOut || !validatePoolParams(minSize, maxSize, maxBuffers)) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    if (numPools >= MAX_MEMORY_POOLS) {
        IORecursiveLockUnlock(lock);
        return MEMORY_OPT_ERROR_POOL_FULL;
    }
    
    MemoryPool* pool = (MemoryPool*)IOMalloc(sizeof(MemoryPool));
    if (!pool) {
        IORecursiveLockUnlock(lock);
        return MEMORY_OPT_ERROR_NO_MEMORY;
    }
    
    pool->minSize = minSize;
    pool->maxSize = maxSize;
    pool->maxBuffers = maxBuffers;
    pool->numFree = 0;
    pool->numAllocated = 0;
    pool->totalAllocations = 0;
    pool->totalReuses = 0;
    
    pool->freeList = (IntelGEMObject**)IOMalloc(
        maxBuffers * sizeof(IntelGEMObject*));
    if (!pool->freeList) {
        IOFree(pool, sizeof(MemoryPool));
        IORecursiveLockUnlock(lock);
        return MEMORY_OPT_ERROR_NO_MEMORY;
    }
    
    pool->lock = IORecursiveLockAlloc();
    if (!pool->lock) {
        IOFree(pool->freeList, maxBuffers * sizeof(IntelGEMObject*));
        IOFree(pool, sizeof(MemoryPool));
        IORecursiveLockUnlock(lock);
        return MEMORY_OPT_ERROR_NO_MEMORY;
    }
    
    pools[numPools++] = pool;
    *poolOut = pool;
    
    IORecursiveLockUnlock(lock);
    
    IOLog("IntelMemoryOptimizer: Created pool (min=%llu, max=%llu, capacity=%u)\n",
          minSize, maxSize, maxBuffers);
    
    return MEMORY_OPT_SUCCESS;
}

MemoryOptError IntelMemoryOptimizer::destroyPool(MemoryPool* pool) {
    if (!pool) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    // Flush pool first
    flushPool(pool);
    
    // Remove from pools array
    for (uint32_t i = 0; i < numPools; i++) {
        if (pools[i] == pool) {
            pools[i] = pools[numPools - 1];
            pools[numPools - 1] = nullptr;
            numPools--;
            break;
        }
    }
    
    // Free pool resources
    if (pool->lock) {
        IORecursiveLockFree(pool->lock);
    }
    
    if (pool->freeList) {
        IOFree(pool->freeList, pool->maxBuffers * sizeof(IntelGEMObject*));
    }
    
    IOFree(pool, sizeof(MemoryPool));
    
    IORecursiveLockUnlock(lock);
    
    return MEMORY_OPT_SUCCESS;
}

IntelGEMObject* IntelMemoryOptimizer::allocateFromPool(
    MemoryPool* pool,
    uint64_t size,
    IntelBufferUsage usage)
{
    if (!pool || size < pool->minSize || size > pool->maxSize) {
        return nullptr;
    }
    
    uint64_t startTime = mach_absolute_time();
    
    IORecursiveLockLock(pool->lock);
    
    IntelGEMObject* object = nullptr;
    
    // Try to reuse from free list
    for (uint32_t i = 0; i < pool->numFree; i++) {
        if (canReuseBuffer(pool->freeList[i], size, usage)) {
            object = pool->freeList[i];
            
            // Remove from free list
            pool->freeList[i] = pool->freeList[pool->numFree - 1];
            pool->numFree--;
            pool->totalReuses++;
            
            stats.poolReuses++;
            break;
        }
    }
    
    IORecursiveLockUnlock(pool->lock);
    
    uint64_t duration = mach_absolute_time() - startTime;
    uint64_t durationUs = duration / 1000;
    
    recordAllocation(size, durationUs, object != nullptr);
    
    if (object) {
        IOLog("IntelMemoryOptimizer: Reused buffer from pool (size=%llu)\n", size);
    }
    
    return object;
}

MemoryOptError IntelMemoryOptimizer::returnToPool(
    MemoryPool* pool,
    IntelGEMObject* object)
{
    if (!pool || !object) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    uint64_t startTime = mach_absolute_time();
    
    IORecursiveLockLock(pool->lock);
    
    if (pool->numFree >= pool->maxBuffers) {
        // Pool full, release object
        IORecursiveLockUnlock(pool->lock);
        object->release();
        return MEMORY_OPT_ERROR_POOL_FULL;
    }
    
    // Add to free list
    pool->freeList[pool->numFree++] = object;
    
    IORecursiveLockUnlock(pool->lock);
    
    uint64_t duration = mach_absolute_time() - startTime;
    uint64_t durationUs = duration / 1000;
    
    recordFree(object->getSize(), durationUs);
    
    return MEMORY_OPT_SUCCESS;
}

void IntelMemoryOptimizer::trimPool(MemoryPool* pool, uint32_t targetSize) {
    if (!pool || targetSize >= pool->numFree) {
        return;
    }
    
    IORecursiveLockLock(pool->lock);
    
    // Release excess buffers
    while (pool->numFree > targetSize) {
        IntelGEMObject* object = pool->freeList[--pool->numFree];
        if (object) {
            object->release();
        }
    }
    
    IORecursiveLockUnlock(pool->lock);
}

void IntelMemoryOptimizer::flushPool(MemoryPool* pool) {
    if (!pool) {
        return;
    }
    
    IORecursiveLockLock(pool->lock);
    
    // Release all free buffers
    for (uint32_t i = 0; i < pool->numFree; i++) {
        if (pool->freeList[i]) {
            pool->freeList[i]->release();
            pool->freeList[i] = nullptr;
        }
    }
    
    pool->numFree = 0;
    
    IORecursiveLockUnlock(pool->lock);
}

// Optimization strategies
MemoryOptError IntelMemoryOptimizer::setStrategy(
    IntelOptimizationStrategy strategy)
{
    if (strategy >= OPTIMIZE_COUNT) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    currentStrategy = strategy;
    IORecursiveLockUnlock(lock);
    
    IOLog("IntelMemoryOptimizer: Strategy changed to %s\n",
          strategy == OPTIMIZE_BANDWIDTH ? "bandwidth" :
          strategy == OPTIMIZE_LATENCY ? "latency" :
          strategy == OPTIMIZE_POWER ? "power" : "balanced");
    
    return MEMORY_OPT_SUCCESS;
}

IntelOptimizationStrategy IntelMemoryOptimizer::getStrategy() {
    return currentStrategy;
}

MemoryOptError IntelMemoryOptimizer::optimizeBuffer(
    IntelGEMObject* object,
    IntelBufferUsage usage)
{
    if (!object) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    IORecursiveLockLock(lock);
    
    MemoryOptError result = MEMORY_OPT_SUCCESS;
    
    switch (currentStrategy) {
        case OPTIMIZE_BANDWIDTH:
            result = applyBandwidthOptimization(object);
            break;
        case OPTIMIZE_LATENCY:
            result = applyLatencyOptimization(object);
            break;
        case OPTIMIZE_POWER:
            result = applyPowerOptimization(object);
            break;
        case OPTIMIZE_BALANCED:
            result = applyBalancedOptimization(object);
            break;
        default:
            result = MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    IORecursiveLockUnlock(lock);
    
    return result;
}

MemoryOptError IntelMemoryOptimizer::defragment() {
    IORecursiveLockLock(lock);
    
    // Defragmentation would:
    // 1. Identify fragmented regions
    // 2. Move buffers to consolidate free space
    // 3. Update GTT mappings
    // (Simplified for framework)
    
    IORecursiveLockUnlock(lock);
    
    return MEMORY_OPT_SUCCESS;
}

// Buffer usage hints
MemoryOptError IntelMemoryOptimizer::setBufferUsage(
    IntelGEMObject* object,
    IntelBufferUsage usage)
{
    if (!object || usage >= BUFFER_USAGE_COUNT) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    // Set usage hint in object metadata
    // (Production would optimize based on usage pattern)
    
    return MEMORY_OPT_SUCCESS;
}

IntelBufferUsage IntelMemoryOptimizer::getBufferUsage(IntelGEMObject* object) {
    if (!object) {
        return BUFFER_USAGE_STATIC;
    }
    
    // In production, would read from object metadata
    return BUFFER_USAGE_STATIC;
}

// Statistics
void IntelMemoryOptimizer::getStatistics(MemoryOptimizationStats* outStats) {
    if (!outStats) {
        return;
    }
    
    IORecursiveLockLock(lock);
    
    memcpy(outStats, &stats, sizeof(MemoryOptimizationStats));
    
    // Calculate derived statistics
    if (stats.totalAllocations > 0) {
        uint32_t poolAllocs = (uint32_t)(stats.poolAllocations + stats.poolReuses);
        outStats->poolReuseRate = stats.poolReuses > 0 ?
            (stats.poolReuses * 100) / poolAllocs : 0;
    }
    
    if (stats.cacheHits + stats.cacheMisses > 0) {
        outStats->cacheHitRate = 
            (stats.cacheHits * 100) / (stats.cacheHits + stats.cacheMisses);
    }
    
    IORecursiveLockUnlock(lock);
}

void IntelMemoryOptimizer::resetStatistics() {
    IORecursiveLockLock(lock);
    
    memset(&stats, 0, sizeof(stats));
    statStartTime = mach_absolute_time();
    
    IORecursiveLockUnlock(lock);
}

void IntelMemoryOptimizer::printStatistics() {
    MemoryOptimizationStats s;
    getStatistics(&s);
    
    IOLog("Compression:\n");
    IOLog("  Compressed buffers:  %llu\n", s.compressedBuffers);
    IOLog("  Bytes saved:         %llu\n", s.compressionSavings);
    IOLog("  Avg ratio:           %u%%\n", s.avgCompressionRatio);
    IOLog("Tiling:\n");
    IOLog("  Tiled buffers:       %llu\n", s.tiledBuffers);
    IOLog("  Bandwidth savings:   %llu%%\n", s.bandwidthSavings);
    IOLog("Cache:\n");
    IOLog("  Hits:                %llu\n", s.cacheHits);
    IOLog("  Misses:              %llu\n", s.cacheMisses);
    IOLog("  Hit rate:            %u%%\n", s.cacheHitRate);
    IOLog("Pool:\n");
    IOLog("  Allocations:         %llu\n", s.poolAllocations);
    IOLog("  Reuses:              %llu\n", s.poolReuses);
    IOLog("  Reuse rate:          %u%%\n", s.poolReuseRate);
    IOLog("Memory:\n");
    IOLog("  Total used:          %llu bytes\n", s.totalMemoryUsed);
    IOLog("  Peak used:           %llu bytes\n", s.peakMemoryUsed);
    IOLog("  Compressed:          %llu bytes\n", s.compressedMemory);
    IOLog("Performance:\n");
    IOLog("  Avg alloc time:      %u us\n", s.avgAllocTimeUs);
    IOLog("  Avg free time:       %u us\n", s.avgFreeTimeUs);
    IOLog("  Total allocations:   %llu\n", s.totalAllocations);
    IOLog("  Total frees:         %llu\n", s.totalFrees);
}

// Hardware capabilities
bool IntelMemoryOptimizer::supportsCompression() {
    return hwSupportsRC || hwSupportsMC || hwSupportsCC;
}

bool IntelMemoryOptimizer::supportsTiling() {
    return hwSupportsTilingY || hwSupportsTilingYF;
}

bool IntelMemoryOptimizer::supportsFastClear() {
    return hwSupportsCC;
}

uint32_t IntelMemoryOptimizer::getMaxCompressionRatio() {
    return 75; // 75% typical for render compression
}

uint32_t IntelMemoryOptimizer::getCacheLineSize() {
    return hwCacheLineSize;
}

uint32_t IntelMemoryOptimizer::getL3CacheSize() {
    return hwL3CacheSize;
}

// Private methods - Compression helpers
MemoryOptError IntelMemoryOptimizer::setupRenderCompression(
    IntelGEMObject* object,
    CompressionParams* params)
{
    // Configure render compression (RC)
    // Gen12 supports lossless render target compression
    
    if (!hwSupportsRC) {
        return MEMORY_OPT_ERROR_NOT_SUPPORTED;
    }
    
    // Calculate AUX surface size (typically 1/256 of main surface)
    uint64_t auxSize = object->getSize() / 256;
    params->auxPlaneOffset = object->getSize();
    params->auxPlanePitch = 64; // Cache line size
    params->lossless = true;
    
    return MEMORY_OPT_SUCCESS;
}

MemoryOptError IntelMemoryOptimizer::setupMediaCompression(
    IntelGEMObject* object,
    CompressionParams* params)
{
    // Configure media compression (MC)
    // Used for video decode/encode
    
    if (!hwSupportsMC) {
        return MEMORY_OPT_ERROR_NOT_SUPPORTED;
    }
    
    params->lossless = false; // Media can be lossy
    
    return MEMORY_OPT_SUCCESS;
}

MemoryOptError IntelMemoryOptimizer::setupClearColorCompression(
    IntelGEMObject* object,
    CompressionParams* params)
{
    // Configure clear color compression (CC)
    // Fast clear using single color value
    
    if (!hwSupportsCC) {
        return MEMORY_OPT_ERROR_NOT_SUPPORTED;
    }
    
    params->fastClear = true;
    params->clearColor = 0x00000000; // Black
    
    return MEMORY_OPT_SUCCESS;
}

uint32_t* IntelMemoryOptimizer::buildCompressionCommand(
    uint32_t* cmd,
    IntelGEMObject* object,
    CompressionParams* params)
{
    // Build MI_SET_COMPRESSION command
    // (Simplified for framework)
    
    *cmd++ = 0x7A000000; // MI_SET_COMPRESSION
    *cmd++ = (uint32_t)(object->getGPUAddress() & 0xFFFFFFFF);
    *cmd++ = (uint32_t)(object->getGPUAddress() >> 32);
    *cmd++ = params->type | (params->lossless ? (1 << 8) : 0);
    
    return cmd;
}

// Tiling helpers
uint32_t IntelMemoryOptimizer::calculateTileStride(
    uint32_t width,
    uint32_t bpp,
    IntelTilingMode mode)
{
    uint32_t bytesPerPixel = (bpp + 7) / 8;
    uint32_t stride = width * bytesPerPixel;
    
    // Align to tile width
    switch (mode) {
        case TILING_X:
            stride = (stride + 511) & ~511;  // 512-byte align
            break;
        case TILING_Y:
        case TILING_YF:
        case TILING_YS:
            stride = (stride + 127) & ~127;  // 128-byte align
            break;
        default:
            stride = (stride + 63) & ~63;    // 64-byte align
    }
    
    return stride;
}

uint32_t IntelMemoryOptimizer::calculateTileHeight(
    uint32_t height,
    IntelTilingMode mode)
{
    uint32_t tileHeight = 0;
    
    switch (mode) {
        case TILING_X:
            tileHeight = 8;
            break;
        case TILING_Y:
            tileHeight = 32;
            break;
        case TILING_YF:
            tileHeight = 64;
            break;
        case TILING_YS:
            tileHeight = 16;
            break;
        default:
            tileHeight = 1;
    }
    
    return (height + tileHeight - 1) / tileHeight;
}

void IntelMemoryOptimizer::getTileDimensions(
    IntelTilingMode mode,
    uint32_t* widthOut,
    uint32_t* heightOut)
{
    switch (mode) {
        case TILING_X:
            *widthOut = 512;
            *heightOut = 8;
            break;
        case TILING_Y:
            *widthOut = 128;
            *heightOut = 32;
            break;
        case TILING_YF:
            *widthOut = 128;
            *heightOut = 64;
            break;
        case TILING_YS:
            *widthOut = 64;
            *heightOut = 16;
            break;
        default:
            *widthOut = 64;
            *heightOut = 1;
    }
}

// Cache helpers
MemoryOptError IntelMemoryOptimizer::configureMOCS(
    uint32_t index,
    IntelCachePolicy policy)
{
    if (index >= MOCS_NUM_ENTRIES) {
        return MEMORY_OPT_ERROR_INVALID_PARAMS;
    }
    
    uint32_t mocsEntry = buildMOCSEntry(policy);
    uint32_t mocsAddr = MOCS_TABLE_BASE + (index * MOCS_ENTRY_SIZE);
    
    // Write MOCS table entry
    // (Production would use controller->writeRegister)
    
    return MEMORY_OPT_SUCCESS;
}

uint32_t IntelMemoryOptimizer::getMOCSIndex(IntelGEMObject* object) {
    // In production, would return index from object metadata
    return 0; // Default index
}

uint32_t IntelMemoryOptimizer::buildMOCSEntry(IntelCachePolicy policy) {
    uint32_t entry = 0;
    
    switch (policy) {
        case CACHE_POLICY_CACHED:
            entry = 0x3; // L3 + LLC cached
            break;
        case CACHE_POLICY_UNCACHED:
            entry = 0x1; // Uncached
            break;
        case CACHE_POLICY_WRITE_COMBINE:
            entry = 0x2; // Write-combining
            break;
        case CACHE_POLICY_WRITE_THROUGH:
            entry = 0x4; // Write-through
            break;
        case CACHE_POLICY_WRITE_BACK:
            entry = 0x3; // Write-back (default)
            break;
        default:
            entry = 0x3;
    }
    
    return entry;
}

// Pool helpers
MemoryPool* IntelMemoryOptimizer::findPoolForSize(uint64_t size) {
    for (uint32_t i = 0; i < numPools; i++) {
        MemoryPool* pool = pools[i];
        if (pool && size >= pool->minSize && size <= pool->maxSize) {
            return pool;
        }
    }
    return nullptr;
}

void IntelMemoryOptimizer::cleanupPoolsTimerFired(
    OSObject* owner,
    IOTimerEventSource* timer)
{
    // Trim pools to reduce memory usage
    for (uint32_t i = 0; i < numPools; i++) {
        if (pools[i] && pools[i]->numFree > 0) {
            uint32_t targetSize = pools[i]->numFree / 2; // Keep 50%
            trimPool(pools[i], targetSize);
        }
    }
    
    // Reschedule timer
    if (timer) {
        timer->setTimeoutMS(POOL_CLEANUP_INTERVAL_MS);
    }
}

bool IntelMemoryOptimizer::canReuseBuffer(
    IntelGEMObject* object,
    uint64_t requestedSize,
    IntelBufferUsage usage)
{
    if (!object) {
        return false;
    }
    
    uint64_t bufferSize = object->getSize();
    
    // Buffer must be large enough
    if (bufferSize < requestedSize) {
        return false;
    }
    
    // Buffer shouldn't be too large (avoid waste)
    if (bufferSize > requestedSize * 2) {
        return false;
    }
    
    return true;
}

// Optimization helpers
MemoryOptError IntelMemoryOptimizer::applyBandwidthOptimization(
    IntelGEMObject* object)
{
    // Bandwidth optimization:



    
    CompressionParams compression;
    compression.type = COMPRESSION_RC;
    compression.lossless = true;
    enableCompression(object, COMPRESSION_RC, &compression);
    
    TilingParams tiling;
    calculateTilingParams(1920, 1080, 32, TILING_Y, &tiling);
    setTiling(object, TILING_Y, &tiling);
    
    setCachePolicy(object, CACHE_POLICY_WRITE_COMBINE);
    
    return MEMORY_OPT_SUCCESS;
}

MemoryOptError IntelMemoryOptimizer::applyLatencyOptimization(
    IntelGEMObject* object)
{
    // Latency optimization:



    
    disableCompression(object);
    
    TilingParams tiling;
    calculateTilingParams(1920, 1080, 32, TILING_NONE, &tiling);
    setTiling(object, TILING_NONE, &tiling);
    
    setCachePolicy(object, CACHE_POLICY_CACHED);
    
    return MEMORY_OPT_SUCCESS;
}

MemoryOptError IntelMemoryOptimizer::applyPowerOptimization(
    IntelGEMObject* object)
{
    // Power optimization:



    
    CompressionParams compression;
    compression.type = COMPRESSION_RC;
    compression.lossless = true;
    enableCompression(object, COMPRESSION_RC, &compression);
    
    TilingParams tiling;
    calculateTilingParams(1920, 1080, 32, TILING_Y, &tiling);
    setTiling(object, TILING_Y, &tiling);
    
    setCachePolicy(object, CACHE_POLICY_UNCACHED);
    
    return MEMORY_OPT_SUCCESS;
}

MemoryOptError IntelMemoryOptimizer::applyBalancedOptimization(
    IntelGEMObject* object)
{
    // Balanced optimization:



    
    uint64_t size = object->getSize();
    
    if (size >= COMPRESSION_THRESHOLD) {
        CompressionParams compression;
        compression.type = COMPRESSION_RC;
        compression.lossless = true;
        enableCompression(object, COMPRESSION_RC, &compression);
    }
    
    IntelTilingMode mode = (size >= (1920 * 1080 * 4)) ? TILING_Y : TILING_NONE;
    TilingParams tiling;
    calculateTilingParams(1920, 1080, 32, mode, &tiling);
    setTiling(object, mode, &tiling);
    
    setCachePolicy(object, CACHE_POLICY_WRITE_BACK);
    
    return MEMORY_OPT_SUCCESS;
}

// Statistics recording
void IntelMemoryOptimizer::recordCompression(
    uint64_t originalSize,
    uint64_t compressedSize)
{
    stats.compressionSavings += (originalSize - compressedSize);
    
    uint32_t ratio = (compressedSize * 100) / originalSize;
    stats.avgCompressionRatio = 
        (stats.avgCompressionRatio + ratio) / 2;
}

void IntelMemoryOptimizer::recordAllocation(
    uint64_t size,
    uint64_t durationUs,
    bool fromPool)
{
    stats.totalAllocations++;
    stats.totalMemoryUsed += size;
    
    if (stats.totalMemoryUsed > stats.peakMemoryUsed) {
        stats.peakMemoryUsed = stats.totalMemoryUsed;
    }
    
    if (fromPool) {
        stats.poolAllocations++;
    }
    
    stats.avgAllocTimeUs = 
        (stats.avgAllocTimeUs + (uint32_t)durationUs) / 2;
}

void IntelMemoryOptimizer::recordFree(uint64_t size, uint64_t durationUs) {
    stats.totalFrees++;
    
    if (stats.totalMemoryUsed >= size) {
        stats.totalMemoryUsed -= size;
    }
    
    stats.avgFreeTimeUs = 
        (stats.avgFreeTimeUs + (uint32_t)durationUs) / 2;
}

void IntelMemoryOptimizer::recordCacheAccess(bool hit) {
    if (hit) {
        stats.cacheHits++;
    } else {
        stats.cacheMisses++;
    }
}

// Validation
bool IntelMemoryOptimizer::validateCompressionParams(
    IntelCompressionType type,
    CompressionParams* params)
{
    if (!params) {
        return false;
    }
    
    if (type >= COMPRESSION_COUNT) {
        return false;
    }
    
    if (!isCompressionSupported(type)) {
        return false;
    }
    
    return true;
}

bool IntelMemoryOptimizer::validateTilingParams(
    IntelTilingMode mode,
    TilingParams* params)
{
    if (!params) {
        return false;
    }
    
    if (mode >= TILING_COUNT) {
        return false;
    }
    
    if (!isTilingSupported(mode)) {
        return false;
    }
    
    if (params->stride == 0) {
        return false;
    }
    
    return true;
}

bool IntelMemoryOptimizer::validatePoolParams(
    uint64_t minSize,
    uint64_t maxSize,
    uint32_t maxBuffers)
{
    if (minSize == 0 || maxSize == 0 || maxBuffers == 0) {
        return false;
    }
    
    if (minSize > maxSize) {
        return false;
    }
    
    if (maxBuffers > MAX_POOL_SIZE) {
        return false;
    }
    
    return true;
}
