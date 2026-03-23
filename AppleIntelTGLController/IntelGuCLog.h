/*
 * IntelGuCLog.h - GuC Firmware Logging and Error Capture
 * Week 40: GuC SLPC & Logging
 * 
 * Captures and processes logs from GuC firmware for debugging and diagnostics.
 * GuC maintains a circular log buffer in GPU memory that the host can read.
 */

#ifndef INTEL_GUC_LOG_H
#define INTEL_GUC_LOG_H

#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>

// Forward declarations
class IntelGuC;
class AppleIntelTGLController;


// MARK: - GuC Log Constants


// Log buffer sizes
#define GUC_LOG_BUFFER_SIZE         (64 * 1024)     // 64KB per type
#define GUC_LOG_DPC_BUFFER_SIZE     (32 * 1024)     // 32KB for DPC
#define GUC_LOG_ISR_BUFFER_SIZE     (16 * 1024)     // 16KB for ISR
#define GUC_LOG_CRASH_BUFFER_SIZE   (8 * 1024)      // 8KB for crash dumps

// Log levels
#define GUC_LOG_LEVEL_DISABLED      0
#define GUC_LOG_LEVEL_ERROR         1
#define GUC_LOG_LEVEL_WARNING       2
#define GUC_LOG_LEVEL_INFO          3
#define GUC_LOG_LEVEL_DEBUG         4
#define GUC_LOG_LEVEL_VERBOSE       5

// Log types
enum GuCLogType {
    GUC_LOG_TYPE_ISR        = 0,    // Interrupt Service Routine logs
    GUC_LOG_TYPE_DPC        = 1,    // Deferred Procedure Call logs
    GUC_LOG_TYPE_CRASH_DUMP = 2,    // Crash dump logs
    GUC_LOG_TYPE_DEBUG      = 3,    // Debug logs
    GUC_LOG_TYPE_COUNT      = 4
};

// Log relay flags
#define GUC_LOG_RELAY_FLAG_ENABLE   (1 << 0)
#define GUC_LOG_RELAY_FLAG_FLUSH    (1 << 1)


// MARK: - GuC Log Entry


struct GuCLogEntry {
    uint32_t timestamp;             // Timestamp in microseconds
    uint32_t level;                 // Log level
    uint32_t moduleId;              // GuC module ID
    uint32_t lineNumber;            // Source line number
    char message[256];              // Log message
} __attribute__((packed));


// MARK: - GuC Log Buffer


struct GuCLogBuffer {
    uint32_t version;               // Buffer format version
    uint32_t size;                  // Buffer size
    volatile uint32_t head;         // Write pointer (updated by GuC)
    volatile uint32_t tail;         // Read pointer (updated by host)
    uint32_t flags;                 // Buffer flags
    uint32_t reserved[3];
    uint8_t data[];                 // Log data
} __attribute__((packed));


// MARK: - GuC Error Info


struct GuCErrorInfo {
    uint32_t errorCode;             // Error code
    uint32_t timestamp;             // When error occurred
    uint32_t pc;                    // Program counter
    uint32_t moduleId;              // Module where error occurred
    uint32_t severity;              // Error severity
    char description[128];          // Error description
} __attribute__((packed));


// MARK: - IntelGuCLog Class


class IntelGuCLog : public OSObject {
    OSDeclareDefaultStructors(IntelGuCLog)
    
public:
    // Initialization
    bool init(IntelGuC* guc, AppleIntelTGLController* controller);
    void free() override;
    
    bool initializeLogging();
    void shutdownLogging();
    

    // Log Control

    
    // Enable/disable logging
    bool enableLogging();
    bool disableLogging();
    bool isLoggingEnabled() { return loggingEnabled; }
    
    // Set log level
    bool setLogLevel(uint32_t level);
    uint32_t getLogLevel() { return logLevel; }
    
    // Enable/disable specific log types
    bool enableLogType(GuCLogType type);
    bool disableLogType(GuCLogType type);
    bool isLogTypeEnabled(GuCLogType type);
    

    // Log Capture

    
    // Capture logs from GuC
    bool captureLogs();
    bool captureLogType(GuCLogType type);
    
    // Flush log buffers
    bool flushLogs();
    bool flushLogType(GuCLogType type);
    
    // Get log buffer status
    uint32_t getLogBufferFillLevel(GuCLogType type);
    bool isLogBufferFull(GuCLogType type);
    

    // Log Reading

    
    // Read log entries
    bool readLogEntry(GuCLogType type, GuCLogEntry* entry);
    uint32_t readLogEntries(GuCLogType type, GuCLogEntry* entries, uint32_t maxCount);
    
    // Parse log buffer
    bool parseLogBuffer(GuCLogType type);
    

    // Error Capture

    
    // Get error information
    bool getLastError(GuCErrorInfo* error);
    uint32_t getErrorCount() { return errorCount; }
    
    // Error history
    static const int MAX_ERROR_HISTORY = 32;
    bool getErrorHistory(GuCErrorInfo* errors, uint32_t maxCount, uint32_t* outCount);
    

    // Crash Dump

    
    // Check for crash
    bool hasCrashDump();
    bool captureCrashDump();
    
    // Get crash information
    bool getCrashInfo(GuCErrorInfo* error);
    
    // Save crash dump to file (for debugging)
    bool saveCrashDump(const char* path);
    

    // Statistics

    
    struct LogStatistics {
        uint64_t totalEntriesRead;      // Total log entries read
        uint64_t entriesPerType[GUC_LOG_TYPE_COUNT]; // Per-type counts
        uint64_t bufferOverruns;        // Buffer overrun count
        uint64_t parseErrors;           // Parse error count
        uint32_t lastCaptureTimeMs;     // Last capture time
        uint32_t averageCaptureTimeMs;  // Average capture time
    };
    
    void getStatistics(LogStatistics* stats);
    void resetStatistics();
    

    // Debug Output

    
    // Print logs to console
    void dumpLogs(GuCLogType type, uint32_t count);
    void dumpAllLogs();
    
    // Print log statistics
    void dumpStatistics();
    
    // Print error history
    void dumpErrorHistory();
    
private:
    // Core components
    IntelGuC* guc;
    AppleIntelTGLController* controller;
    bool initialized;
    bool loggingEnabled;
    
    // Log configuration
    uint32_t logLevel;
    uint32_t enabledLogTypes;       // Bitmask of enabled types
    
    // Log buffers (mapped from GPU memory)
    IOBufferMemoryDescriptor* logBufferMemory[GUC_LOG_TYPE_COUNT];
    GuCLogBuffer* logBuffers[GUC_LOG_TYPE_COUNT];
    IOLock* bufferLocks[GUC_LOG_TYPE_COUNT];
    
    // Error tracking
    GuCErrorInfo errorHistory[MAX_ERROR_HISTORY];
    uint32_t errorCount;
    uint32_t errorIndex;
    IOLock* errorLock;
    
    // Crash dump
    IOBufferMemoryDescriptor* crashDumpMemory;
    void* crashDumpBuffer;
    bool hasCrash;
    
    // Statistics
    LogStatistics stats;
    IOLock* statsLock;
    
    // Update timer for periodic log capture
    IOTimerEventSource* captureTimer;
    static const uint32_t CAPTURE_INTERVAL_MS = 100;  // 100ms
    
    // Helper methods
    bool allocateLogBuffer(GuCLogType type, uint32_t size);
    void releaseLogBuffer(GuCLogType type);
    
    bool readLogData(GuCLogType type, uint8_t* buffer, uint32_t size);
    bool parseLogEntry(const uint8_t* data, uint32_t size, GuCLogEntry* entry);
    
    void recordError(const GuCErrorInfo* error);
    
    static void captureTimerFired(OSObject* owner, IOTimerEventSource* timer);
    
    const char* getLogLevelString(uint32_t level);
    const char* getLogTypeString(GuCLogType type);
};

#endif // INTEL_GUC_LOG_H
