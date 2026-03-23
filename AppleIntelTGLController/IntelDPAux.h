//
//  IntelDPAux.h
//
//
//  DisplayPort AUX channel communication
//  Week 13-14: DisplayPort training implementation
//

#ifndef IntelDPAux_h
#define IntelDPAux_h

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "linux_compat.h"

// Forward declarations
class IntelUncore;
class IntelPort;

// DisplayPort AUX channel registers (per port)
#define DP_AUX_CH_CTL(port)     (0x64010 + ((port) * 0x100))
#define DP_AUX_CH_DATA(port, i) (0x64014 + ((port) * 0x100) + ((i) * 4))

// AUX channel control bits
#define DP_AUX_CH_CTL_SEND_BUSY     (1 << 31)
#define DP_AUX_CH_CTL_DONE          (1 << 30)
#define DP_AUX_CH_CTL_INTERRUPT     (1 << 29)
#define DP_AUX_CH_CTL_TIME_OUT_ERROR (1 << 28)
#define DP_AUX_CH_CTL_RECEIVE_ERROR (1 << 27)
#define DP_AUX_CH_CTL_MESSAGE_SIZE_MASK (0x1f << 20)
#define DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT 20
#define DP_AUX_CH_CTL_PRECHARGE_2US_MASK (0xf << 16)
#define DP_AUX_CH_CTL_BIT_CLOCK_2X_MASK (0x7ff)

// AUX channel message request types
#define DP_AUX_NATIVE_WRITE         0x8
#define DP_AUX_NATIVE_READ          0x9
#define DP_AUX_I2C_WRITE            0x0
#define DP_AUX_I2C_READ             0x1
#define DP_AUX_I2C_WRITE_STATUS_UPDATE  0x2

// AUX channel reply types
#define DP_AUX_NATIVE_REPLY_ACK     0x0
#define DP_AUX_NATIVE_REPLY_NACK    0x1
#define DP_AUX_NATIVE_REPLY_DEFER   0x2
#define DP_AUX_I2C_REPLY_ACK        0x0
#define DP_AUX_I2C_REPLY_NACK       0x4
#define DP_AUX_I2C_REPLY_DEFER      0x8

// DPCD addresses (DisplayPort Configuration Data)
#define DP_DPCD_REV                 0x000
#define DP_MAX_LINK_RATE            0x001
#define DP_MAX_LANE_COUNT           0x002
#define DP_MAX_DOWNSPREAD           0x003
#define DP_SUPPORTED_LINK_RATES     0x010

// Link configuration
#define DP_LINK_BW_SET              0x100
#define DP_LANE_COUNT_SET           0x101
#define DP_TRAINING_PATTERN_SET     0x102
#define DP_TRAINING_LANE0_SET       0x103
#define DP_TRAINING_LANE1_SET       0x104
#define DP_TRAINING_LANE2_SET       0x105
#define DP_TRAINING_LANE3_SET       0x106

// Link status
#define DP_LANE0_1_STATUS           0x202
#define DP_LANE2_3_STATUS           0x203
#define DP_LANE_ALIGN_STATUS_UPDATED 0x204
#define DP_ADJUST_REQUEST_LANE0_1   0x206
#define DP_ADJUST_REQUEST_LANE2_3   0x207

// Link bandwidth values
#define DP_LINK_BW_1_62             0x06    // 1.62 Gbps
#define DP_LINK_BW_2_7              0x0a    // 2.7 Gbps
#define DP_LINK_BW_5_4              0x14    // 5.4 Gbps
#define DP_LINK_BW_8_1              0x1e    // 8.1 Gbps (HBR3)

// Lane count values
#define DP_LANE_COUNT_1             0x01
#define DP_LANE_COUNT_2             0x02
#define DP_LANE_COUNT_4             0x04
#define DP_LANE_COUNT_ENHANCED_FRAME_EN (1 << 7)

// Training patterns
#define DP_TRAINING_PATTERN_DISABLE 0
#define DP_TRAINING_PATTERN_1       1
#define DP_TRAINING_PATTERN_2       2
#define DP_TRAINING_PATTERN_3       3

// Voltage swing and pre-emphasis levels
#define DP_TRAIN_VOLTAGE_SWING_SHIFT 0
#define DP_TRAIN_PRE_EMPHASIS_SHIFT  3
#define DP_TRAIN_MAX_SWING_REACHED  (1 << 2)
#define DP_TRAIN_MAX_PRE_EMPHASIS_REACHED (1 << 5)

// Lane status bits
#define DP_LANE_CR_DONE             (1 << 0)
#define DP_LANE_CHANNEL_EQ_DONE     (1 << 1)
#define DP_LANE_SYMBOL_LOCKED       (1 << 2)

// Alignment status
#define DP_INTERLANE_ALIGN_DONE     (1 << 0)

/*!
 * @class IntelDPAux
 * @abstract DisplayPort AUX channel communication handler
 * @discussion Manages low-level AUX channel transactions for reading/writing DPCD registers
 */
class IntelDPAux : public OSObject {
    OSDeclareDefaultStructors(IntelDPAux)
    
public:
    /*!
     * @function create
     * @abstract Factory method to create and initialize an IntelDPAux instance
     */
    static IntelDPAux* create(IntelUncore* uncore, IntelPort* port);
    
    /*!
     * @function init
     * @abstract Initialize the AUX channel handler
     */
    virtual bool init(IntelUncore* uncore, IntelPort* port);
    
    /*!
     * @function free
     * @abstract Clean up resources
     */
    virtual void free() override;
    
    // AUX channel transactions
    
    /*!
     * @function readDPCD
     * @abstract Read from DisplayPort Configuration Data registers
     * @param address DPCD register address
     * @param buffer Buffer to store read data
     * @param size Number of bytes to read
     * @return true on success, false on failure
     */
    bool readDPCD(uint32_t address, uint8_t* buffer, size_t size);
    
    /*!
     * @function writeDPCD
     * @abstract Write to DisplayPort Configuration Data registers
     * @param address DPCD register address
     * @param buffer Buffer containing data to write
     * @param size Number of bytes to write
     * @return true on success, false on failure
     */
    bool writeDPCD(uint32_t address, const uint8_t* buffer, size_t size);
    
    /*!
     * @function readEDID
     * @abstract Read EDID data over I2C-over-AUX
     * @param buffer Buffer to store EDID data (at least 128 bytes)
     * @return true on success, false on failure
     */
    bool readEDID(uint8_t* buffer);
    
    // Status and diagnostics
    
    /*!
     * @function getStatistics
     * @abstract Get AUX channel transaction statistics
     */
    void getStatistics(uint32_t* total, uint32_t* success, uint32_t* timeouts, uint32_t* errors);
    
private:
    IntelUncore*    m_uncore;           // Register access
    IntelPort*      m_port;             // Associated port
    uint32_t        m_portIndex;        // Port index (0-4)
    IOLock*         m_lock;             // Transaction lock
    
    // Statistics
    uint32_t        m_totalTransactions;
    uint32_t        m_successfulTransactions;
    uint32_t        m_timeouts;
    uint32_t        m_errors;
    
    // Low-level AUX operations
    
    /*!
     * @function auxTransaction
     * @abstract Perform a low-level AUX channel transaction
     * @param request Request type (native read/write, I2C read/write)
     * @param address DPCD or I2C address
     * @param buffer Data buffer
     * @param size Transaction size
     * @param isWrite true for write, false for read
     * @return true on success, false on failure
     */
    bool auxTransaction(uint8_t request, uint32_t address, uint8_t* buffer, 
                       size_t size, bool isWrite);
    
    /*!
     * @function waitForIdle
     * @abstract Wait for AUX channel to become idle
     * @param timeoutMs Timeout in milliseconds
     * @return true if idle, false on timeout
     */
    bool waitForIdle(uint32_t timeoutMs);
    
    /*!
     * @function packAuxHeader
     * @abstract Pack AUX message header
     * @param request Request type
     * @param address DPCD address
     * @param size Message size
     * @return Packed header value
     */
    uint32_t packAuxHeader(uint8_t request, uint32_t address, uint8_t size);
    
    /*!
     * @function unpackAuxReply
     * @abstract Unpack AUX reply status
     * @param ctl Control register value
     * @return Reply type (ACK/NACK/DEFER)
     */
    uint8_t unpackAuxReply(uint32_t ctl);
};

#endif /* IntelDPAux_h */
