//
//  IntelDPLinkTraining.cpp
//
//
//  DisplayPort link training implementation
//  Week 13-14: Complete DP link training state machine
//

#include "IntelDPLinkTraining.h"
#include "IntelDPAux.h"
#include "IntelPort.h"
#include "IntelUncore.h"
#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndStructors(IntelDPLinkTraining, OSObject)

// Training timing constants (from DP spec)
#define CLOCK_RECOVERY_DELAY_US     100
#define EQUALIZATION_DELAY_US       400
#define MAX_CR_TRIES                5
#define MAX_EQ_TRIES                5
#define LINK_TRAINING_TIMEOUT_MS    100

IntelDPLinkTraining* IntelDPLinkTraining::create(IntelUncore* uncore, 
                                                  IntelPort* port,
                                                  IntelDPAux* aux) {
    IntelDPLinkTraining* training = new IntelDPLinkTraining;
    if (!training) {
        return nullptr;
    }
    
    if (!training->init(uncore, port, aux)) {
        training->release();
        return nullptr;
    }
    
    return training;
}

bool IntelDPLinkTraining::init(IntelUncore* uncore, IntelPort* port, IntelDPAux* aux) {
    if (!super::init()) {
        return false;
    }
    
    m_uncore = uncore;
    m_port = port;
    m_aux = aux;
    m_portIndex = 0; // TODO: Get from port
    
    m_lock = IOLockAlloc();
    if (!m_lock) {
        IOLog("IntelDPLinkTraining: Failed to allocate lock\n");
        return false;
    }
    
    m_state = DP_TRAINING_STATE_IDLE;
    
    // Initialize config to safe defaults
    bzero(&m_config, sizeof(m_config));
    m_config.linkRate = DP_LINK_RATE_1_62;
    m_config.laneCount = DP_LANES_1;
    m_config.enhancedFraming = false;
    
    // Initialize statistics
    m_trainingAttempts = 0;
    m_trainingSuccesses = 0;
    m_trainingFailures = 0;
    
    IOLog("IntelDPLinkTraining: Initialized for port %u\n", m_portIndex);
    return true;
}

void IntelDPLinkTraining::free() {
    if (m_lock) {
        IOLockFree(m_lock);
        m_lock = nullptr;
    }
    
    super::free();
}

bool IntelDPLinkTraining::trainLink(const DPLinkConfig* config) {
    if (!config) {
        return false;
    }
    
    // Declare variables at top to avoid goto issues
    uint8_t linkBw = 0;
    uint8_t laneCount = 0;
    uint8_t trainingPattern = 0;
    
    IOLockLock(m_lock);
    
    IOLog("IntelDPLinkTraining: Starting training (rate=%d KHz, lanes=%d)\n",
          config->linkRate, config->laneCount);
    
    m_trainingAttempts++;
    m_config = *config;
    m_state = DP_TRAINING_STATE_IDLE;
    
    // Disable link first
    disableLink();
    IODelay(1000);
    
    // Configure link rate and lane count in DPCD
    switch (m_config.linkRate) {
        case DP_LINK_RATE_1_62: linkBw = 0x06; break;
        case DP_LINK_RATE_2_7:  linkBw = 0x0a; break;
        case DP_LINK_RATE_5_4:  linkBw = 0x14; break;
        case DP_LINK_RATE_8_1:  linkBw = 0x1e; break;
        default:
            IOLog("IntelDPLinkTraining: Invalid link rate\n");
            goto fail;
    }
    
    if (!m_aux->writeDPCD(0x100, &linkBw, 1)) {
        IOLog("IntelDPLinkTraining: Failed to write link bandwidth\n");
        goto fail;
    }
    
    laneCount = (uint8_t)m_config.laneCount;
    if (m_config.enhancedFraming) {
        laneCount |= 0x80;
    }
    
    if (!m_aux->writeDPCD(0x101, &laneCount, 1)) {
        IOLog("IntelDPLinkTraining: Failed to write lane count\n");
        goto fail;
    }
    
    // Enable DDI buffer
    if (!enableDDIBuffer()) {
        IOLog("IntelDPLinkTraining: Failed to enable DDI buffer\n");
        goto fail;
    }
    
    IODelay(1000);
    
    // Phase 1: Clock recovery
    m_state = DP_TRAINING_STATE_CLOCK_RECOVERY;
    if (!clockRecovery()) {
        IOLog("IntelDPLinkTraining: Clock recovery failed\n");
        goto fail;
    }
    
    IOLog("IntelDPLinkTraining: Clock recovery successful\n");
    
    // Phase 2: Channel equalization
    m_state = DP_TRAINING_STATE_EQUALIZATION;
    if (!channelEqualization()) {
        IOLog("IntelDPLinkTraining: Channel equalization failed\n");
        goto fail;
    }
    
    IOLog("IntelDPLinkTraining: Channel equalization successful\n");
    
    // Training complete - set normal operation
    setTrainingPattern(DP_TRAINING_PATTERN_DISABLE);
    
    trainingPattern = 0;
    m_aux->writeDPCD(0x102, &trainingPattern, 1);
    
    m_state = DP_TRAINING_STATE_COMPLETE;
    m_trainingSuccesses++;
    
    IOLog("IntelDPLinkTraining: Link training complete!\n");
    
    IOLockUnlock(m_lock);
    return true;
    
fail:
    m_state = DP_TRAINING_STATE_FAILED;
    m_trainingFailures++;
    disableLink();
    IOLockUnlock(m_lock);
    return false;
}

bool IntelDPLinkTraining::autoTrainLink() {
    IOLockLock(m_lock);
    
    IOLog("IntelDPLinkTraining: Starting auto link training\n");
    
    // Read sink capabilities
    if (!readSinkCapabilities(&m_sinkCaps)) {
        IOLog("IntelDPLinkTraining: Failed to read sink capabilities\n");
        IOLockUnlock(m_lock);
        return false;
    }
    
    IOLog("IntelDPLinkTraining: Sink caps - DPCD rev 0x%x, max rate 0x%x, max lanes %d\n",
          m_sinkCaps.dpcdRev, m_sinkCaps.maxLinkRate, m_sinkCaps.maxLaneCount);
    
    // Select optimal configuration
    selectLinkConfig();
    
    IOLockUnlock(m_lock);
    
    // Try training with fallback
    for (int attempt = 0; attempt < 5; attempt++) {
        if (trainLink(&m_config)) {
            return true;
        }
        
        IOLog("IntelDPLinkTraining: Training failed, trying fallback config\n");
        
        IOLockLock(m_lock);
        if (!fallbackLinkConfig()) {
            IOLog("IntelDPLinkTraining: No more fallback configs available\n");
            IOLockUnlock(m_lock);
            break;
        }
        IOLockUnlock(m_lock);
        
        IODelay(10000); // 10ms between attempts
    }
    
    IOLog("IntelDPLinkTraining: Auto link training failed\n");
    return false;
}

bool IntelDPLinkTraining::retrainLink() {
    return trainLink(&m_config);
}

void IntelDPLinkTraining::disableLink() {
    // Set idle pattern
    setTrainingPattern(DP_TRAINING_PATTERN_DISABLE);
    
    // Disable DDI buffer
    disableDDIBuffer();
    
    m_state = DP_TRAINING_STATE_IDLE;
}

bool IntelDPLinkTraining::readSinkCapabilities(DPSinkCaps* caps) {
    if (!caps) {
        return false;
    }
    
    // Read DPCD registers 0x000-0x003
    uint8_t dpcdData[4];
    if (!m_aux->readDPCD(0x000, dpcdData, 4)) {
        return false;
    }
    
    caps->dpcdRev = dpcdData[0];
    caps->maxLinkRate = dpcdData[1];
    caps->maxLaneCount = dpcdData[2] & 0x1f;
    caps->enhancedFraming = (dpcdData[2] & 0x80) != 0;
    caps->downspreadSupport = (dpcdData[3] & 0x01) != 0;
    caps->fastLinkTraining = false; // TODO: Check extended caps
    
    return true;
}

void IntelDPLinkTraining::getCurrentConfig(DPLinkConfig* config) {
    if (config) {
        *config = m_config;
    }
}

void IntelDPLinkTraining::getStatistics(uint32_t* attempts, uint32_t* successes, 
                                        uint32_t* failures) {
    if (attempts) *attempts = m_trainingAttempts;
    if (successes) *successes = m_trainingSuccesses;
    if (failures) *failures = m_trainingFailures;
}

bool IntelDPLinkTraining::clockRecovery() {
    // Initialize voltage swing and pre-emphasis to level 0
    for (int i = 0; i < 4; i++) {
        m_config.voltageSwing[i] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0;
        m_config.preEmphasis[i] = DP_TRAIN_PRE_EMPH_LEVEL_0;
    }
    
    // Set training pattern 1
    setTrainingPattern(DP_TRAINING_PATTERN_1);
    setVoltageSwing();
    setPreEmphasis();
    
    // Tell sink to use training pattern 1
    uint8_t trainingPattern = 0x21; // Pattern 1, scrambling disabled
    m_aux->writeDPCD(0x102, &trainingPattern, 1);
    
    // Write initial training values to sink
    uint8_t trainingLane[4];
    for (int i = 0; i < m_config.laneCount; i++) {
        trainingLane[i] = (m_config.voltageSwing[i] << 0) | 
                         (m_config.preEmphasis[i] << 3);
    }
    m_aux->writeDPCD(0x103, trainingLane, m_config.laneCount);
    
    // Try clock recovery
    for (int try_count = 0; try_count < MAX_CR_TRIES; try_count++) {
        IODelay(CLOCK_RECOVERY_DELAY_US);
        
        if (checkClockRecovery()) {
            return true;
        }
        
        // Update training values based on sink requests
        if (!updateTrainingValues()) {
            // Values maxed out, CR failed
            IOLog("IntelDPLinkTraining: CR training values maxed out\n");
            return false;
        }
        
        setVoltageSwing();
        setPreEmphasis();
        
        // Write updated values to sink
        for (int i = 0; i < m_config.laneCount; i++) {
            trainingLane[i] = (m_config.voltageSwing[i] << 0) | 
                             (m_config.preEmphasis[i] << 3);
            if (m_config.voltageSwing[i] == DP_TRAIN_VOLTAGE_SWING_LEVEL_3) {
                trainingLane[i] |= 0x04; // Max swing reached
            }
            if (m_config.preEmphasis[i] == DP_TRAIN_PRE_EMPH_LEVEL_3) {
                trainingLane[i] |= 0x20; // Max pre-emphasis reached
            }
        }
        m_aux->writeDPCD(0x103, trainingLane, m_config.laneCount);
    }
    
    return false;
}

bool IntelDPLinkTraining::channelEqualization() {
    // Set training pattern 2 (or 3 for HBR2+)
    uint32_t pattern = (m_config.linkRate >= DP_LINK_RATE_5_4) ? 
                       DP_TRAINING_PATTERN_3 : DP_TRAINING_PATTERN_2;
    
    setTrainingPattern(pattern);
    
    // Tell sink
    uint8_t trainingPattern = (pattern == DP_TRAINING_PATTERN_3) ? 0x23 : 0x22;
    m_aux->writeDPCD(0x102, &trainingPattern, 1);
    
    // Try equalization
    for (int try_count = 0; try_count < MAX_EQ_TRIES; try_count++) {
        IODelay(EQUALIZATION_DELAY_US);
        
        if (checkChannelEqualization()) {
            return true;
        }
        
        // Update training values
        if (!updateTrainingValues()) {
            IOLog("IntelDPLinkTraining: EQ training values maxed out\n");
            return false;
        }
        
        setVoltageSwing();
        setPreEmphasis();
        
        // Write updated values
        uint8_t trainingLane[4];
        for (int i = 0; i < m_config.laneCount; i++) {
            trainingLane[i] = (m_config.voltageSwing[i] << 0) | 
                             (m_config.preEmphasis[i] << 3);
            if (m_config.voltageSwing[i] == DP_TRAIN_VOLTAGE_SWING_LEVEL_3) {
                trainingLane[i] |= 0x04;
            }
            if (m_config.preEmphasis[i] == DP_TRAIN_PRE_EMPH_LEVEL_3) {
                trainingLane[i] |= 0x20;
            }
        }
        m_aux->writeDPCD(0x103, trainingLane, m_config.laneCount);
    }
    
    return false;
}

bool IntelDPLinkTraining::checkClockRecovery() {
    uint8_t laneStatus[2];
    if (!m_aux->readDPCD(0x202, laneStatus, 2)) {
        return false;
    }
    
    // Check CR done bit for each lane
    for (int i = 0; i < m_config.laneCount; i++) {
        uint8_t status = (i < 2) ? laneStatus[0] : laneStatus[1];
        uint8_t shift = (i & 1) * 4;
        
        if (!(status & (0x01 << shift))) {
            return false; // CR not done on this lane
        }
    }
    
    return true;
}

bool IntelDPLinkTraining::checkChannelEqualization() {
    uint8_t laneStatus[3];
    if (!m_aux->readDPCD(0x202, laneStatus, 3)) {
        return false;
    }
    
    // Check EQ done and symbol lock for each lane
    for (int i = 0; i < m_config.laneCount; i++) {
        uint8_t status = (i < 2) ? laneStatus[0] : laneStatus[1];
        uint8_t shift = (i & 1) * 4;
        
        // Need CR + EQ + symbol lock
        if ((status & (0x07 << shift)) != (0x07 << shift)) {
            return false;
        }
    }
    
    // Check interlane alignment
    if (!(laneStatus[2] & 0x01)) {
        return false;
    }
    
    return true;
}

bool IntelDPLinkTraining::enableDDIBuffer() {
    uint32_t portWidth = 0;
    switch (m_config.laneCount) {
        case DP_LANES_1: portWidth = 0; break;
        case DP_LANES_2: portWidth = 1; break;
        case DP_LANES_4: portWidth = 3; break;
        default: return false;
    }
    
    uint32_t bufCtl = DDI_BUF_CTL_ENABLE | (portWidth << DDI_PORT_WIDTH_SHIFT);
    m_uncore->writeRegister32(DDI_BUF_CTL(m_portIndex), bufCtl);
    
    // Wait for buffer to become active
    for (int i = 0; i < 10; i++) {
        IODelay(1000);
        uint32_t status = m_uncore->readRegister32(DDI_BUF_CTL(m_portIndex));
        if (!(status & DDI_BUF_CTL_IDLE_STATUS)) {
            return true;
        }
    }
    
    IOLog("IntelDPLinkTraining: Timeout waiting for DDI buffer enable\n");
    return false;
}

void IntelDPLinkTraining::disableDDIBuffer() {
    m_uncore->writeRegister32(DDI_BUF_CTL(m_portIndex), 0);
}

void IntelDPLinkTraining::setTrainingPattern(uint32_t pattern) {
    uint32_t tpCtl = m_uncore->readRegister32(DP_TP_CTL(m_portIndex));
    
    tpCtl &= ~DP_TP_CTL_LINK_TRAIN_MASK;
    
    switch (pattern) {
        case DP_TRAINING_PATTERN_1:
            tpCtl |= DP_TP_CTL_LINK_TRAIN_PAT1;
            break;
        case DP_TRAINING_PATTERN_2:
            tpCtl |= DP_TP_CTL_LINK_TRAIN_PAT2;
            break;
        case DP_TRAINING_PATTERN_3:
            tpCtl |= DP_TP_CTL_LINK_TRAIN_PAT3;
            break;
        case DP_TRAINING_PATTERN_DISABLE:
            tpCtl |= DP_TP_CTL_LINK_TRAIN_NORMAL;
            break;
    }
    
    if (m_config.enhancedFraming) {
        tpCtl |= DP_TP_CTL_ENHANCED_FRAME_ENABLE;
    }
    
    tpCtl |= DP_TP_CTL_ENABLE;
    
    m_uncore->writeRegister32(DP_TP_CTL(m_portIndex), tpCtl);
}

void IntelDPLinkTraining::setVoltageSwing() {
    // TODO: Program voltage swing registers for Tiger Lake
    // This requires DDI buffer translation tables
    // For now, just log the values
    IOLog("IntelDPLinkTraining: Setting voltage swing: [%d,%d,%d,%d]\n",
          m_config.voltageSwing[0], m_config.voltageSwing[1],
          m_config.voltageSwing[2], m_config.voltageSwing[3]);
}

void IntelDPLinkTraining::setPreEmphasis() {
    // TODO: Program pre-emphasis registers for Tiger Lake
    IOLog("IntelDPLinkTraining: Setting pre-emphasis: [%d,%d,%d,%d]\n",
          m_config.preEmphasis[0], m_config.preEmphasis[1],
          m_config.preEmphasis[2], m_config.preEmphasis[3]);
}

bool IntelDPLinkTraining::updateTrainingValues() {
    uint8_t adjustReq[2];
    if (!m_aux->readDPCD(0x206, adjustReq, 2)) {
        return false;
    }
    
    bool maxedOut = true;
    
    for (int i = 0; i < m_config.laneCount; i++) {
        uint8_t req = (i < 2) ? adjustReq[0] : adjustReq[1];
        uint8_t shift = (i & 1) * 4;
        
        uint8_t newVoltage = (req >> shift) & 0x03;
        uint8_t newPreEmph = ((req >> shift) >> 2) & 0x03;
        
        if (newVoltage < DP_TRAIN_VOLTAGE_SWING_LEVEL_3) {
            maxedOut = false;
        }
        if (newPreEmph < DP_TRAIN_PRE_EMPH_LEVEL_3) {
            maxedOut = false;
        }
        
        m_config.voltageSwing[i] = newVoltage;
        m_config.preEmphasis[i] = newPreEmph;
    }
    
    return !maxedOut;
}

void IntelDPLinkTraining::selectLinkConfig() {
    // Start with highest supported rate
    if (m_sinkCaps.maxLinkRate >= 0x1e) {
        m_config.linkRate = DP_LINK_RATE_8_1;
    } else if (m_sinkCaps.maxLinkRate >= 0x14) {
        m_config.linkRate = DP_LINK_RATE_5_4;
    } else if (m_sinkCaps.maxLinkRate >= 0x0a) {
        m_config.linkRate = DP_LINK_RATE_2_7;
    } else {
        m_config.linkRate = DP_LINK_RATE_1_62;
    }
    
    // Use maximum lanes
    m_config.laneCount = (DPLaneCount)m_sinkCaps.maxLaneCount;
    m_config.enhancedFraming = m_sinkCaps.enhancedFraming;
}

bool IntelDPLinkTraining::fallbackLinkConfig() {
    // Try reducing link rate first
    if (m_config.linkRate > DP_LINK_RATE_1_62) {
        switch (m_config.linkRate) {
            case DP_LINK_RATE_8_1:
                m_config.linkRate = DP_LINK_RATE_5_4;
                break;
            case DP_LINK_RATE_5_4:
                m_config.linkRate = DP_LINK_RATE_2_7;
                break;
            case DP_LINK_RATE_2_7:
                m_config.linkRate = DP_LINK_RATE_1_62;
                break;
            default:
                break;
        }
        return true;
    }
    
    // Try reducing lane count
    if (m_config.laneCount > DP_LANES_1) {
        m_config.linkRate = DP_LINK_RATE_5_4; // Reset to high rate
        m_config.laneCount = (DPLaneCount)(m_config.laneCount / 2);
        return true;
    }
    
    // No more fallback options
    return false;
}
