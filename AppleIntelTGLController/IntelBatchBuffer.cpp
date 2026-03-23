/*
 * IntelBatchBuffer.cpp
 */

#include "IntelBatchBuffer.h"
#include "IntelGEMObject.h"
#include "IntelRingBuffer.h"
#include <IOKit/IOLib.h>

IntelBatchBuffer::IntelBatchBuffer()
    : batchObj(NULL)
    , batchPtr(NULL)
    , size(0)
    , used(0)
    , building(false)
{
}

IntelBatchBuffer::~IntelBatchBuffer()
{
    cleanup();
}

bool IntelBatchBuffer::init(IntelGEMObject *obj)
{
    if (!obj) {
        return false;
    }
    
    batchObj = obj;
    batchObj->retain();
    
    size = obj->getSize();
    
    // Map for CPU access
    if (!obj->mapCPU(&batchPtr)) {
        return false;
    }
    
    return true;
}

void IntelBatchBuffer::cleanup()
{
    if (batchObj && batchPtr) {
        batchObj->unmapCPU();
        batchPtr = NULL;
    }
    
    if (batchObj) {
        batchObj->release();
        batchObj = NULL;
    }
}

bool IntelBatchBuffer::begin()
{
    if (building) {
        return false;
    }
    
    used = 0;
    building = true;
    return true;
}

bool IntelBatchBuffer::emit(u32 dword)
{
    if (!building || !batchPtr || used + 4 > size) {
        return false;
    }
    
    volatile u32 *ptr = (volatile u32 *)((uintptr_t)batchPtr + used);
    *ptr = dword;
    used += 4;
    
    return true;
}

bool IntelBatchBuffer::addCommand(u32 command)
{
    // Alias for emit() - used by blitter code
    return emit(command);
}

bool IntelBatchBuffer::emitDwords(const u32 *data, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!emit(data[i])) {
            return false;
        }
    }
    return true;
}

bool IntelBatchBuffer::end()
{
    if (!building) {
        return false;
    }
    
    // Emit batch buffer end
    emit(MI::BATCH_BUFFER_END);
    
    // Align to 8 bytes
    while ((used & 7) != 0) {
        emit(MI::NOOP);
    }
    
    building = false;
    return true;
}

bool IntelBatchBuffer::submit(IntelRingBuffer *ring)
{
    if (!ring || !batchObj || building) {
        return false;
    }
    
    u64 batchAddr = batchObj->getGTTAddress();
    if (batchAddr == 0) {
        IOLog("IntelBatch: Object not bound\n");
        return false;
    }
    
    // Submit via ring
    if (!ring->begin(4)) {
        return false;
    }
    
    // MI_BATCH_BUFFER_START
    ring->emit(0x31 << 23 | 1);  // MI_BATCH_BUFFER_START, length=1
    ring->emit((u32)batchAddr);
    ring->emit((u32)(batchAddr >> 32));
    
    return ring->advance();
}

u64 IntelBatchBuffer::getGPUAddress() const
{
    return batchObj ? batchObj->getGTTAddress() : 0;
}
