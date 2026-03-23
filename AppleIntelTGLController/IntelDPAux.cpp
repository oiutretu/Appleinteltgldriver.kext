//
//  IntelDPAux.cpp
//
//
//  DisplayPort AUX channel communication implementation
//  Week 13-14: DisplayPort training
//

#include "IntelDPAux.h"
#include "IntelPort.h"
#include "IntelUncore.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelDPAux, OSObject)

// Timing constants (in microseconds)
#define AUX_TIMEOUT_MS              100
#define AUX_RETRY_INTERVAL_US       500
#define AUX_MAX_RETRIES             3
#define AUX_PRECHARGE_US            10
#define AUX_SETTLE_US               20

IntelDPAux* IntelDPAux::create(IntelUncore* uncore, IntelPort* port) {
    IntelDPAux* aux = new IntelDPAux;
    if (!aux) {
        return nullptr;
    }
    
    if (!aux->init(uncore, port)) {
        aux->release();
        return nullptr;
    }
    
    return aux;
}

bool IntelDPAux::init(IntelUncore* uncore, IntelPort* port) {
    if (!super::init()) {
        return false;
    }
    
    m_uncore = uncore;
    m_port = port;
    m_portIndex = 0; // TODO: Get from port
    
    m_lock = IOLockAlloc();
    if (!m_lock) {
        IOLog("IntelDPAux: Failed to allocate lock\n");
        return false;
    }
    
    // Initialize statistics
    m_totalTransactions = 0;
    m_successfulTransactions = 0;
    m_timeouts = 0;
    m_errors = 0;
    
    IOLog("IntelDPAux: Initialized for port %u\n", m_portIndex);
    return true;
}

void IntelDPAux::free() {
    if (m_lock) {
        IOLockFree(m_lock);
        m_lock = nullptr;
    }
    
    super::free();
}

bool IntelDPAux::readDPCD(uint32_t address, uint8_t* buffer, size_t size) {
    if (!buffer || size == 0 || size > 16) {
        IOLog("IntelDPAux: Invalid read parameters (size=%zu)\n", size);
        return false;
    }
    
    IOLockLock(m_lock);
    
    bool success = auxTransaction(DP_AUX_NATIVE_READ, address, buffer, size, false);
    
    if (success) {
        IOLog("IntelDPAux: Read %zu bytes from DPCD 0x%x\n", size, address);
    } else {
        IOLog("IntelDPAux: Failed to read from DPCD 0x%x\n", address);
    }
    
    IOLockUnlock(m_lock);
    return success;
}

bool IntelDPAux::writeDPCD(uint32_t address, const uint8_t* buffer, size_t size) {
    if (!buffer || size == 0 || size > 16) {
        IOLog("IntelDPAux: Invalid write parameters (size=%zu)\n", size);
        return false;
    }
    
    IOLockLock(m_lock);
    
    bool success = auxTransaction(DP_AUX_NATIVE_WRITE, address, 
                                  const_cast<uint8_t*>(buffer), size, true);
    
    if (success) {
        IOLog("IntelDPAux: Wrote %zu bytes to DPCD 0x%x\n", size, address);
    } else {
        IOLog("IntelDPAux: Failed to write to DPCD 0x%x\n", address);
    }
    
    IOLockUnlock(m_lock);
    return success;
}

bool IntelDPAux::readEDID(uint8_t* buffer) {
    if (!buffer) {
        return false;
    }
    
    IOLockLock(m_lock);
    
    // Read EDID over I2C-over-AUX
    // EDID is at I2C address 0x50
    const uint8_t edidAddress = 0x50;
    bool success = true;
    
    // Read in 16-byte chunks (AUX channel limit)
    for (size_t offset = 0; offset < 128; offset += 16) {
        // Set I2C address and offset
        uint8_t cmd[2] = { edidAddress << 1, (uint8_t)offset };
        if (!auxTransaction(DP_AUX_I2C_WRITE, edidAddress, cmd, 1, true)) {
            success = false;
            break;
        }
        
        // Read chunk
        if (!auxTransaction(DP_AUX_I2C_READ, edidAddress, buffer + offset, 16, false)) {
            success = false;
            break;
        }
        
        IODelay(1000); // 1ms between chunks
    }
    
    if (success) {
        IOLog("IntelDPAux: Read 128-byte EDID successfully\n");
    } else {
        IOLog("IntelDPAux: Failed to read EDID\n");
    }
    
    IOLockUnlock(m_lock);
    return success;
}

void IntelDPAux::getStatistics(uint32_t* total, uint32_t* success, 
                               uint32_t* timeouts, uint32_t* errors) {
    if (total) *total = m_totalTransactions;
    if (success) *success = m_successfulTransactions;
    if (timeouts) *timeouts = m_timeouts;
    if (errors) *errors = m_errors;
}

bool IntelDPAux::auxTransaction(uint8_t request, uint32_t address, uint8_t* buffer,
                                size_t size, bool isWrite) {
    if (!m_uncore || size > 16) {
        return false;
    }
    
    m_totalTransactions++;
    
    // Wait for AUX channel to be idle
    if (!waitForIdle(AUX_TIMEOUT_MS)) {
        IOLog("IntelDPAux: Timeout waiting for idle before transaction\n");
        m_timeouts++;
        return false;
    }
    
    // Pack message header
    uint32_t header = packAuxHeader(request, address, (uint8_t)size);
    
    // Write data registers for write operations
    if (isWrite) {
        for (size_t i = 0; i < size; i += 4) {
            uint32_t data = 0;
            for (size_t j = 0; j < 4 && (i + j) < size; j++) {
                data |= ((uint32_t)buffer[i + j]) << (j * 8);
            }
            m_uncore->writeRegister32(DP_AUX_CH_DATA(m_portIndex, i / 4), data);
        }
    }
    
    // Set message size and control bits
    uint32_t ctl = 0;
    ctl |= DP_AUX_CH_CTL_SEND_BUSY;
    ctl |= DP_AUX_CH_CTL_TIME_OUT_ERROR;  // Enable timeout detection
    ctl |= ((size << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) & DP_AUX_CH_CTL_MESSAGE_SIZE_MASK);
    ctl |= (0x2 << 16);  // 2us precharge time
    ctl |= 0x200;        // 400 KHz bit clock
    
    // Start transaction
    m_uncore->writeRegister32(DP_AUX_CH_CTL(m_portIndex), ctl | header);
    
    // Wait for completion with retries
    bool completed = false;
    for (int retry = 0; retry < AUX_MAX_RETRIES; retry++) {
        // Wait for done bit
        AbsoluteTime deadline;
        clock_interval_to_deadline(AUX_TIMEOUT_MS, kMillisecondScale, &deadline);
        
        while (true) {
            ctl = m_uncore->readRegister32(DP_AUX_CH_CTL(m_portIndex));
            
            if (ctl & DP_AUX_CH_CTL_DONE) {
                completed = true;
                break;
            }
            
            if (ctl & DP_AUX_CH_CTL_TIME_OUT_ERROR) {
                IOLog("IntelDPAux: AUX timeout error\n");
                break;
            }
            
            if (ctl & DP_AUX_CH_CTL_RECEIVE_ERROR) {
                IOLog("IntelDPAux: AUX receive error\n");
                break;
            }
            
            // Check deadline
            AbsoluteTime now;
            clock_get_uptime(&now);
            if (CMP_ABSOLUTETIME(&now, &deadline) > 0) {
                IOLog("IntelDPAux: Transaction timeout (retry %d)\n", retry);
                m_timeouts++;
                break;
            }
            
            IODelay(AUX_RETRY_INTERVAL_US);
        }
        
        if (completed) {
            break;
        }
        
        // Retry with increased delay
        IODelay(1000 * (retry + 1));
    }
    
    if (!completed) {
        m_errors++;
        return false;
    }
    
    // Check reply status
    uint8_t reply = unpackAuxReply(ctl);
    
    bool success = false;
    if (request == DP_AUX_NATIVE_READ || request == DP_AUX_NATIVE_WRITE) {
        success = (reply == DP_AUX_NATIVE_REPLY_ACK);
        if (reply == DP_AUX_NATIVE_REPLY_DEFER) {
            IOLog("IntelDPAux: Native transaction deferred\n");
        } else if (reply == DP_AUX_NATIVE_REPLY_NACK) {
            IOLog("IntelDPAux: Native transaction NACK\n");
        }
    } else {
        success = (reply == DP_AUX_I2C_REPLY_ACK);
        if (reply == DP_AUX_I2C_REPLY_DEFER) {
            IOLog("IntelDPAux: I2C transaction deferred\n");
        } else if (reply == DP_AUX_I2C_REPLY_NACK) {
            IOLog("IntelDPAux: I2C transaction NACK\n");
        }
    }
    
    if (!success) {
        m_errors++;
        return false;
    }
    
    // Read data registers for read operations
    if (!isWrite) {
        uint32_t receivedSize = (ctl & DP_AUX_CH_CTL_MESSAGE_SIZE_MASK) >> 
                                DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT;
        
        for (size_t i = 0; i < receivedSize && i < size; i += 4) {
            uint32_t data = m_uncore->readRegister32(DP_AUX_CH_DATA(m_portIndex, i / 4));
            for (size_t j = 0; j < 4 && (i + j) < size; j++) {
                buffer[i + j] = (data >> (j * 8)) & 0xff;
            }
        }
    }
    
    m_successfulTransactions++;
    return true;
}

bool IntelDPAux::waitForIdle(uint32_t timeoutMs) {
    AbsoluteTime deadline;
    clock_interval_to_deadline(timeoutMs, kMillisecondScale, &deadline);
    
    while (true) {
        uint32_t ctl = m_uncore->readRegister32(DP_AUX_CH_CTL(m_portIndex));
        
        if (!(ctl & DP_AUX_CH_CTL_SEND_BUSY)) {
            return true;
        }
        
        AbsoluteTime now;
        clock_get_uptime(&now);
        if (CMP_ABSOLUTETIME(&now, &deadline) > 0) {
            return false;
        }
        
        IODelay(100);
    }
}

uint32_t IntelDPAux::packAuxHeader(uint8_t request, uint32_t address, uint8_t size) {
    uint32_t header = 0;
    
    // Pack: [request:4][address:20][size:8]
    header |= ((uint32_t)request & 0xf) << 28;
    header |= (address & 0xfffff) << 8;
    header |= (size - 1) & 0xff;  // Size is encoded as (actual_size - 1)
    
    return header;
}

uint8_t IntelDPAux::unpackAuxReply(uint32_t ctl) {
    // Reply is in bits [27:24] for native, [31:28] for I2C
    // We need to check the message type to know which bits to read
    // For simplicity, check both and return the non-zero value
    
    uint8_t nativeReply = (ctl >> 24) & 0xf;
    uint8_t i2cReply = (ctl >> 28) & 0xf;
    
    // Native replies are in lower bits, I2C in upper
    if (nativeReply != 0) {
        return nativeReply;
    }
    return i2cReply;
}
