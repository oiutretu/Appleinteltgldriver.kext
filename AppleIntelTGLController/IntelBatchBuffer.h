/*
 * IntelBatchBuffer.h
 * 
 * Batch buffer helper for building command sequences
 */

#ifndef INTEL_BATCHBUFFER_H
#define INTEL_BATCHBUFFER_H

#include "linux_compat.h"

class IntelGEMObject;
class IntelRingBuffer;

class IntelBatchBuffer {
public:
    IntelBatchBuffer();
    ~IntelBatchBuffer();
    
    // Initialization
    bool init(IntelGEMObject *obj);
    void cleanup();
    
    // Building
    bool begin();
    bool emit(u32 dword);
    bool emitDwords(const u32 *data, size_t count);
    bool addCommand(u32 command);  // Alias for emit for compatibility
    bool end();
    
    // Submission
    bool submit(IntelRingBuffer *ring);
    
    // Query
    size_t getSize() const { return size; }
    size_t getUsed() const { return used; }
    u64 getGPUAddress() const;
    
    // Additional accessors for validation
    IntelGEMObject* getBatchObject() const { return batchObj; }
    size_t getLength() const { return used; }
    
private:
    IntelGEMObject *batchObj;
    void *batchPtr;
    size_t size;
    size_t used;
    bool building;
};

#endif // INTEL_BATCHBUFFER_H
