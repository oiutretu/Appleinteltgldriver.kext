//
//  IntelDPLinkTraining.h
//
//
//  DisplayPort link training state machine
//  Week 13-14: DisplayPort training implementation
//

#ifndef IntelDPLinkTraining_h
#define IntelDPLinkTraining_h

#include <IOKit/IOService.h>
#include "linux_compat.h"

// Forward declarations
class IntelUncore;
class IntelPort;
class IntelDPAux;

// Link training state
enum DPTrainingState {
    DP_TRAINING_STATE_IDLE = 0,
    DP_TRAINING_STATE_CLOCK_RECOVERY,
    DP_TRAINING_STATE_EQUALIZATION,
    DP_TRAINING_STATE_COMPLETE,
    DP_TRAINING_STATE_FAILED
};

// Link rate options
enum DPLinkRate {
    DP_LINK_RATE_1_62 = 162000,   // 1.62 Gbps (RBR)
    DP_LINK_RATE_2_7  = 270000,   // 2.7 Gbps (HBR)
    DP_LINK_RATE_5_4  = 540000,   // 5.4 Gbps (HBR2)
    DP_LINK_RATE_8_1  = 810000    // 8.1 Gbps (HBR3)
};

// Lane count options
enum DPLaneCount {
    DP_LANES_1 = 1,
    DP_LANES_2 = 2,
    DP_LANES_4 = 4
};

// Voltage swing levels (0-3)
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_0  0
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_1  1
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_2  2
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_3  3

// Pre-emphasis levels (0-3)
#define DP_TRAIN_PRE_EMPH_LEVEL_0       0
#define DP_TRAIN_PRE_EMPH_LEVEL_1       1
#define DP_TRAIN_PRE_EMPH_LEVEL_2       2
#define DP_TRAIN_PRE_EMPH_LEVEL_3       3

// Tiger Lake DP register offsets
#define DDI_BUF_CTL_A           0x64000
#define DDI_BUF_CTL(port)       (DDI_BUF_CTL_A + ((port) * 0x100))

// DDI buffer control bits
#define DDI_BUF_CTL_ENABLE      (1 << 31)
#define DDI_BUF_CTL_IDLE_STATUS (1 << 7)
#define DDI_PORT_WIDTH_SHIFT    1
#define DDI_PORT_WIDTH_MASK     (7 << DDI_PORT_WIDTH_SHIFT)

// DP transport control
#define DP_TP_CTL_A             0x64040
#define DP_TP_CTL(port)         (DP_TP_CTL_A + ((port) * 0x100))

// DP transport control bits
#define DP_TP_CTL_ENABLE        (1 << 31)
#define DP_TP_CTL_ENHANCED_FRAME_ENABLE (1 << 18)
#define DP_TP_CTL_LINK_TRAIN_MASK (7 << 8)
#define DP_TP_CTL_LINK_TRAIN_PAT1 (0 << 8)
#define DP_TP_CTL_LINK_TRAIN_PAT2 (1 << 8)
#define DP_TP_CTL_LINK_TRAIN_PAT3 (4 << 8)
#define DP_TP_CTL_LINK_TRAIN_IDLE (2 << 8)
#define DP_TP_CTL_LINK_TRAIN_NORMAL (3 << 8)

// DP status
#define DP_TP_STATUS_A          0x64044
#define DP_TP_STATUS(port)      (DP_TP_STATUS_A + ((port) * 0x100))

/*!
 * @struct DPLinkConfig
 * @abstract DisplayPort link configuration
 */
struct DPLinkConfig {
    DPLinkRate      linkRate;           // Link rate in KHz
    DPLaneCount     laneCount;          // Number of lanes (1, 2, or 4)
    bool            enhancedFraming;    // Enhanced framing mode
    bool            sscEnabled;         // Spread spectrum clocking
    
    // Voltage swing and pre-emphasis per lane
    uint8_t         voltageSwing[4];    // 0-3 per lane
    uint8_t         preEmphasis[4];     // 0-3 per lane
};

/*!
 * @struct DPSinkCaps
 * @abstract DisplayPort sink (monitor) capabilities
 */
struct DPSinkCaps {
    uint8_t         dpcdRev;            // DPCD revision
    uint8_t         maxLinkRate;        // Maximum link rate
    uint8_t         maxLaneCount;       // Maximum lanes
    bool            enhancedFraming;    // Supports enhanced framing
    bool            downspreadSupport;  // Supports SSC
    bool            fastLinkTraining;   // Supports fast link training
};

/*!
 * @class IntelDPLinkTraining
 * @abstract DisplayPort link training state machine
 * @discussion Implements the full DP link training sequence (CR + EQ phases)
 */
class IntelDPLinkTraining : public OSObject {
    OSDeclareDefaultStructors(IntelDPLinkTraining)
    
public:
    /*!
     * @function create
     * @abstract Factory method to create and initialize link training instance
     */
    static IntelDPLinkTraining* create(IntelUncore* uncore, IntelPort* port, IntelDPAux* aux);
    
    /*!
     * @function init
     * @abstract Initialize the link training handler
     */
    virtual bool init(IntelUncore* uncore, IntelPort* port, IntelDPAux* aux);
    
    /*!
     * @function free
     * @abstract Clean up resources
     */
    virtual void free() override;
    
    // Link training operations
    
    /*!
     * @function trainLink
     * @abstract Perform complete link training sequence
     * @param config Desired link configuration
     * @return true on success, false on failure
     */
    bool trainLink(const DPLinkConfig* config);
    
    /*!
     * @function autoTrainLink
     * @abstract Automatically negotiate and train link
     * @discussion Reads sink capabilities and tries highest rate first
     * @return true on success, false on failure
     */
    bool autoTrainLink();
    
    /*!
     * @function retrainLink
     * @abstract Retrain link with current configuration
     * @return true on success, false on failure
     */
    bool retrainLink();
    
    /*!
     * @function disableLink
     * @abstract Disable DisplayPort link
     */
    void disableLink();
    
    // Capability detection
    
    /*!
     * @function readSinkCapabilities
     * @abstract Read DisplayPort sink capabilities from DPCD
     * @param caps Output structure for capabilities
     * @return true on success, false on failure
     */
    bool readSinkCapabilities(DPSinkCaps* caps);
    
    // Status
    
    /*!
     * @function getState
     * @abstract Get current training state
     */
    DPTrainingState getState() const { return m_state; }
    
    /*!
     * @function getCurrentConfig
     * @abstract Get current link configuration
     */
    void getCurrentConfig(DPLinkConfig* config);
    
    /*!
     * @function getStatistics
     * @abstract Get link training statistics
     */
    void getStatistics(uint32_t* attempts, uint32_t* successes, uint32_t* failures);
    
private:
    IntelUncore*        m_uncore;       // Register access
    IntelPort*          m_port;         // Associated port
    IntelDPAux*         m_aux;          // AUX channel
    uint32_t            m_portIndex;    // Port index (0-4)
    
    DPTrainingState     m_state;        // Current training state
    DPLinkConfig        m_config;       // Current link configuration
    DPSinkCaps          m_sinkCaps;     // Sink capabilities
    
    IOLock*             m_lock;         // Training lock
    
    // Statistics
    uint32_t            m_trainingAttempts;
    uint32_t            m_trainingSuccesses;
    uint32_t            m_trainingFailures;
    
    // Training phases
    
    /*!
     * @function clockRecovery
     * @abstract Clock recovery phase (training pattern 1)
     * @return true if CR achieved on all lanes
     */
    bool clockRecovery();
    
    /*!
     * @function channelEqualization
     * @abstract Channel equalization phase (training pattern 2/3)
     * @return true if EQ achieved on all lanes
     */
    bool channelEqualization();
    
    /*!
     * @function checkClockRecovery
     * @abstract Check if clock recovery is done on all lanes
     */
    bool checkClockRecovery();
    
    /*!
     * @function checkChannelEqualization
     * @abstract Check if channel EQ is done on all lanes
     */
    bool checkChannelEqualization();
    
    // Hardware control
    
    /*!
     * @function enableDDIBuffer
     * @abstract Enable DDI buffer for the port
     */
    bool enableDDIBuffer();
    
    /*!
     * @function disableDDIBuffer
     * @abstract Disable DDI buffer for the port
     */
    void disableDDIBuffer();
    
    /*!
     * @function setTrainingPattern
     * @abstract Set link training pattern (1, 2, 3, or idle)
     */
    void setTrainingPattern(uint32_t pattern);
    
    /*!
     * @function setVoltageSwing
     * @abstract Set voltage swing levels for all lanes
     */
    void setVoltageSwing();
    
    /*!
     * @function setPreEmphasis
     * @abstract Set pre-emphasis levels for all lanes
     */
    void setPreEmphasis();
    
    /*!
     * @function updateTrainingValues
     * @abstract Read and apply training adjust requests from sink
     * @return true if values changed
     */
    bool updateTrainingValues();
    
    // Utilities
    
    /*!
     * @function selectLinkConfig
     * @abstract Select optimal link configuration based on sink caps
     */
    void selectLinkConfig();
    
    /*!
     * @function fallbackLinkConfig
     * @abstract Fall back to lower link rate/lane count
     * @return true if fallback available, false if exhausted
     */
    bool fallbackLinkConfig();
};

#endif /* IntelDPLinkTraining_h */
