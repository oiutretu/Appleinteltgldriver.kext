//
//  intel_gt_power_regs.h
// Graphics Driver
//
//  GT Power Management Register Definitions
//  Tiger Lake (Gen12) Register Offsets and Bit Definitions
//
//  Week 17 Day 2 - Phase 3: Power Management
//

#ifndef intel_gt_power_regs_h
#define intel_gt_power_regs_h

#include <stdint.h>

// MARK: - RC6 Render Standby Registers

// RC6 Control Register
#define GEN6_RC_CONTROL                 0xA090
#define   GEN6_RC_CTL_RC6_ENABLE        (1 << 18)   // Enable RC6
#define   GEN6_RC_CTL_RC6p_ENABLE       (1 << 17)   // Enable RC6p (package C6)
#define   GEN6_RC_CTL_RC6pp_ENABLE      (1 << 16)   // Enable RC6pp (deeper C6)
#define   GEN6_RC_CTL_EI_MODE(x)        ((x) << 27) // Evaluation interval mode
#define   GEN6_RC_CTL_HW_ENABLE         (1 << 31)   // Hardware control enable

// RC6 State Register (read-only)
#define GEN6_RC_STATE                   0xA094
#define   GEN6_RC_STATE_RC0             (0 << 0)    // Fully powered
#define   GEN6_RC_STATE_RC1             (1 << 0)    // Light sleep
#define   GEN6_RC_STATE_RC6             (6 << 0)    // Deep sleep
#define   GEN6_RC_STATE_RC6p            (7 << 0)    // Package C6
#define   GEN6_RC_STATE_RC6pp           (8 << 0)    // Deeper package C6
#define   GEN6_RC_STATE_MASK            (0xF << 0)  // State mask

// RC6 Idle Threshold
#define GEN6_RC6_THRESHOLD              0xA0B8
#define   GEN6_RC6_THRESHOLD_VALUE(x)   ((x) & 0xFFFFF) // 20-bit threshold (us)

// RC6 Wake Rate Limit
#define GEN6_RC6_WAKE_RATE_LIMIT        0xA098
#define   GEN6_RC6_WAKE_RATE_LIMIT_MASK 0xFFFF      // 16-bit limit

// RC6 Residency Counter (cumulative time in RC6)
#define GEN6_GT_GFX_RC6                 0x138104    // RC6 residency
#define GEN6_GT_GFX_RC6p                0x13810C    // RC6p residency
#define GEN6_GT_GFX_RC6pp               0x138110    // RC6pp residency

// RC6 Context Base Address
#define GEN6_RC_CONTEXT_BASE            0xD48
#define   GEN6_RC_CONTEXT_ENABLE        (1 << 0)    // Context base valid

// MARK: - RP Frequency Scaling Registers

// Request P-State Register (frequency request)
#define GEN6_RPNSWREQ                   0xA008
#define   GEN6_TURBO_DISABLE            (1 << 31)   // Disable turbo
#define   GEN6_FREQUENCY(x)             ((x) << 25) // Frequency ratio (0-31)
#define   GEN6_OFFSET(x)                ((x) << 19) // Offset from ratio
#define   GEN6_AGGRESSIVE_TURBO         (1 << 15)   // Aggressive turbo mode

// RP Control Register
#define GEN6_RP_CONTROL                 0xA024
#define   GEN6_RP_MEDIA_TURBO           (1 << 11)   // Media turbo enable
#define   GEN6_RP_MEDIA_MODE_MASK       (3 << 9)    // Media mode
#define   GEN6_RP_MEDIA_HW_TURBO_MODE   (0 << 9)    // Hardware turbo
#define   GEN6_RP_MEDIA_HW_NORMAL_MODE  (1 << 9)    // Hardware normal
#define   GEN6_RP_MEDIA_HW_MODE         (2 << 9)    // Hardware mode
#define   GEN6_RP_ENABLE                (1 << 7)    // Enable RP
#define   GEN6_RP_UP_BUSY_AVG           (1 << 3)    // Up threshold: busy avg
#define   GEN6_RP_UP_BUSY_CONT          (1 << 4)    // Up threshold: busy cont
#define   GEN6_RP_DOWN_IDLE_AVG         (1 << 2)    // Down threshold: idle avg
#define   GEN6_RP_DOWN_IDLE_CONT        (1 << 1)    // Down threshold: idle cont

// RP State Capability Register (read-only)
#define GEN6_RP_STATE_CAP               0xD14
#define   GEN6_RP_STATE_CAP_RP0_SHIFT   0           // RP0 (max turbo)
#define   GEN6_RP_STATE_CAP_RP1_SHIFT   8           // RP1 (base)
#define   GEN6_RP_STATE_CAP_RPn_SHIFT   16          // RPn (min efficient)
#define   GEN6_RP_STATE_CAP_MASK        0xFF        // Ratio mask

// RP Status Register 1 (current state)
#define GEN6_RPSTAT1                    0xA01C
#define   GEN6_CAGF_MASK                (0x7F << 8) // Current actual frequency
#define   GEN6_CAGF_SHIFT               8

// RP Down/Up Threshold Registers
#define GEN6_RP_DOWN_THRESHOLD          0xA020
#define GEN6_RP_UP_THRESHOLD            0xA02C
#define   GEN6_RP_THRESHOLD_VALUE(x)    ((x) & 0xFFFFFF) // 24-bit threshold

// RP Up/Down Evaluation Interval
#define GEN6_RP_UP_EI                   0xA068
#define GEN6_RP_DOWN_EI                 0xA06C
#define   GEN6_RP_EI_VALUE(x)           ((x) & 0xFFFFFF) // Interval in us

// RP Interrupt Limits
#define GEN6_RP_INTERRUPT_LIMITS        0xA014
#define   GEN6_RP_INTERRUPT_MASK        0xFF        // Interrupt frequency mask

// Render Performance Status Register
#define GEN6_PERF_LIMIT_REASONS         0x1F818
#define   RATL_MASK                     (1 << 5)    // Thermal limit
#define   VR_THERMALERT_MASK            (1 << 4)    // VR thermal alert
#define   PROCHOT_MASK                  (1 << 0)    // Processor hot

// MARK: - Clock Gating Registers

// Unit Level Clock Gating Control 1
#define GEN6_UCGCTL1                    0x9400
#define   GEN6_BLBUNIT_CLOCK_GATE_DISABLE   (1 << 5)    // BLB unit
#define   GEN6_CSUNIT_CLOCK_GATE_DISABLE    (1 << 7)    // Command streamer

// Unit Level Clock Gating Control 2
#define GEN6_UCGCTL2                    0x9404
#define   GEN6_RCCUNIT_CLOCK_GATE_DISABLE   (1 << 11)   // RCC unit
#define   GEN6_RCPBUNIT_CLOCK_GATE_DISABLE  (1 << 12)   // RCPB unit

// Unit Level Clock Gating Control 3
#define GEN7_UCGCTL3                    0x9408
#define GEN8_UCGCTL6                    0x9430
#define   GEN8_SDEUNIT_CLOCK_GATE_DISABLE   (1 << 14)   // SDE unit

// Render Clock Gating Control
#define GEN6_RCGCTL1                    0x9410
#define   GEN6_RCGCTL1_RC_OP_FLUSH_ENABLE   (1 << 0)    // RC op flush

// Display Clock Gating Disable
#define DSPCLK_GATE_D                   0x42020
#define   DPFUNIT_CLOCK_GATE_DISABLE    (1 << 16)   // Display fetch
#define   DPCUNIT_CLOCK_GATE_DISABLE    (1 << 24)   // Display cursor

// Media Clock Gating (Gen8+)
#define GEN8_UCGCTL4                    0x9424
#define   GEN8_HDCUNIT_CLOCK_GATE_DISABLE_HD0   (1 << 0)

// Power Context Clock Gating
#define GEN6_RPDEUC                     0xA084
#define   GEN6_RPDEUC_CLOCK_GATE_ENABLE (1 << 0)

// MARK: - Thermal Management Registers

// Package Thermal Status
#define PACKAGE_THERM_STATUS            0x1A0
#define   THERM_STATUS_VALID            (1 << 31)   // Reading valid
#define   THERM_STATUS_POWER_LIMIT      (1 << 10)   // Power limit exceeded
#define   THERM_STATUS_THERMAL_LIMIT    (1 << 1)    // Thermal limit exceeded
#define   THERM_STATUS_TEMP_MASK        (0x7F << 16) // Temperature (127-temp)
#define   THERM_STATUS_TEMP_SHIFT       16
#define   THERM_STATUS_RESOLUTION       1           // 1degC resolution

// Package Thermal Interrupt
#define PACKAGE_THERM_INTERRUPT         0x1A8
#define   THERM_INT_HIGH_ENABLE         (1 << 0)    // High temp interrupt
#define   THERM_INT_LOW_ENABLE          (1 << 1)    // Low temp interrupt
#define   THERM_INT_THRESHOLD1_ENABLE   (1 << 15)   // Threshold 1 enable

// Graphics Thermal Status
#define GT_PERF_STATUS                  0x1381B4
#define   GT_PERF_TEMP_MASK             (0xFF << 0)  // GPU temperature
#define   GT_PERF_TEMP_SHIFT            0

// MARK: - Power Context Save/Restore

// Render Power Context Base
#define GEN8_RING_PDP_UDW(base, n)      ((base) + 0x270 + (n) * 8 + 4)
#define GEN8_RING_PDP_LDW(base, n)      ((base) + 0x270 + (n) * 8)

// Power Context Size
#define POWER_CONTEXT_SIZE              (4 * 1024 * 1024)  // 4MB

// MARK: - Forcewake Domains for Power Registers

#define FORCEWAKE_GT                    (1 << 0)    // GT domain
#define FORCEWAKE_RENDER                (1 << 1)    // Render domain
#define FORCEWAKE_MEDIA                 (1 << 2)    // Media domain

// MARK: - Helper Functions

// Convert frequency ratio to MHz (Tiger Lake: ratio * 50 MHz)
static inline uint32_t ratio_to_frequency_mhz(uint32_t ratio) {
    return ratio * 50;
}

// Convert MHz to frequency ratio
static inline uint32_t frequency_mhz_to_ratio(uint32_t mhz) {
    return (mhz + 25) / 50;  // Round to nearest
}

// Extract frequency ratio from register value
static inline uint32_t extract_frequency_ratio(uint32_t reg_value) {
    return (reg_value >> 25) & 0x1F;  // Bits 25-29 (5 bits = 0-31)
}

// Build frequency request register value
static inline uint32_t build_frequency_request(uint32_t ratio, bool turbo_enable) {
    uint32_t value = GEN6_FREQUENCY(ratio);
    if (!turbo_enable) {
        value |= GEN6_TURBO_DISABLE;
    }
    return value;
}

// Extract RC6 state from register
static inline uint32_t extract_rc6_state(uint32_t reg_value) {
    return (reg_value & GEN6_RC_STATE_MASK);
}

// Extract temperature from thermal status (Tj_max - reading)
static inline uint32_t extract_temperature_celsius(uint32_t reg_value, uint32_t tj_max) {
    if (!(reg_value & THERM_STATUS_VALID)) {
        return 0;  // Invalid reading
    }
    uint32_t temp_offset = (reg_value & THERM_STATUS_TEMP_MASK) >> THERM_STATUS_TEMP_SHIFT;
    return tj_max - temp_offset;
}

// Extract current frequency from RPSTAT1
static inline uint32_t extract_current_frequency(uint32_t reg_value) {
    uint32_t cagf = (reg_value & GEN6_CAGF_MASK) >> GEN6_CAGF_SHIFT;
    return ratio_to_frequency_mhz(cagf);
}

// Extract RP0/RP1/RPn from capability register
static inline void extract_rp_capabilities(uint32_t reg_value, 
                                          uint32_t* rp0, 
                                          uint32_t* rp1, 
                                          uint32_t* rpn) {
    if (rp0) {
        *rp0 = ((reg_value >> GEN6_RP_STATE_CAP_RP0_SHIFT) & GEN6_RP_STATE_CAP_MASK);
    }
    if (rp1) {
        *rp1 = ((reg_value >> GEN6_RP_STATE_CAP_RP1_SHIFT) & GEN6_RP_STATE_CAP_MASK);
    }
    if (rpn) {
        *rpn = ((reg_value >> GEN6_RP_STATE_CAP_RPn_SHIFT) & GEN6_RP_STATE_CAP_MASK);
    }
}

// Build RC6 control register value
static inline uint32_t build_rc6_control(bool rc6_enable, bool rc6p_enable, bool rc6pp_enable) {
    uint32_t value = GEN6_RC_CTL_HW_ENABLE;  // Always enable hardware control
    
    if (rc6_enable) {
        value |= GEN6_RC_CTL_RC6_ENABLE;
    }
    if (rc6p_enable) {
        value |= GEN6_RC_CTL_RC6p_ENABLE;
    }
    if (rc6pp_enable) {
        value |= GEN6_RC_CTL_RC6pp_ENABLE;
    }
    
    // Evaluation interval mode (0 = normal)
    value |= GEN6_RC_CTL_EI_MODE(0);
    
    return value;
}

// Convert microseconds to RC6 threshold units
static inline uint32_t us_to_rc6_threshold(uint32_t microseconds) {
    return GEN6_RC6_THRESHOLD_VALUE(microseconds);
}

// Convert RC6 residency counter to microseconds
// Counter increments at 1.28 MHz (Tiger Lake)
static inline uint64_t rc6_residency_to_us(uint32_t counter) {
    // counter / 1.28 = counter * 1000000 / 1280000 = counter * 100 / 128
    return ((uint64_t)counter * 100) / 128;
}

// Check if thermal throttling is active
static inline bool is_thermal_throttling(uint32_t perf_limit_reasons) {
    return (perf_limit_reasons & (RATL_MASK | VR_THERMALERT_MASK | PROCHOT_MASK)) != 0;
}

// MARK: - Register Access Macros

// Read RC6 control
#define READ_RC6_CONTROL(uncore)        ((uncore)->readRegister32(GEN6_RC_CONTROL))

// Write RC6 control
#define WRITE_RC6_CONTROL(uncore, val)  ((uncore)->writeRegister32(GEN6_RC_CONTROL, (val)))

// Read RC6 state
#define READ_RC6_STATE(uncore)          ((uncore)->readRegister32(GEN6_RC_STATE))

// Read RC6 residency
#define READ_RC6_RESIDENCY(uncore)      ((uncore)->readRegister32(GEN6_GT_GFX_RC6))
#define READ_RC6p_RESIDENCY(uncore)     ((uncore)->readRegister32(GEN6_GT_GFX_RC6p))
#define READ_RC6pp_RESIDENCY(uncore)    ((uncore)->readRegister32(GEN6_GT_GFX_RC6pp))

// Write frequency request
#define WRITE_FREQUENCY_REQUEST(uncore, val) ((uncore)->writeRegister32(GEN6_RPNSWREQ, (val)))

// Read current frequency
#define READ_RPSTAT1(uncore)            ((uncore)->readRegister32(GEN6_RPSTAT1))

// Read RP capabilities
#define READ_RP_STATE_CAP(uncore)       ((uncore)->readRegister32(GEN6_RP_STATE_CAP))

// Read/Write RP control
#define READ_RP_CONTROL(uncore)         ((uncore)->readRegister32(GEN6_RP_CONTROL))
#define WRITE_RP_CONTROL(uncore, val)   ((uncore)->writeRegister32(GEN6_RP_CONTROL, (val)))

// Read thermal status
#define READ_PACKAGE_THERM_STATUS(uncore) ((uncore)->readRegister32(PACKAGE_THERM_STATUS))
#define READ_GT_PERF_STATUS(uncore)       ((uncore)->readRegister32(GT_PERF_STATUS))

// Read performance limit reasons
#define READ_PERF_LIMIT_REASONS(uncore)   ((uncore)->readRegister32(GEN6_PERF_LIMIT_REASONS))

// MARK: - Constants

// Tiger Lake Tj_max (junction temperature max)
#define TIGER_LAKE_TJ_MAX               100         // 100degC

// Default RC6 idle threshold
#define DEFAULT_RC6_THRESHOLD_US        100000      // 100ms

// Default frequency ratios (Tiger Lake typical)
#define DEFAULT_RPn_RATIO               6           // 300 MHz
#define DEFAULT_RP1_RATIO               22          // 1100 MHz
#define DEFAULT_RP0_RATIO               26          // 1300 MHz

// Frequency limits
#define MIN_FREQUENCY_MHZ               300
#define MAX_FREQUENCY_MHZ               1300

#endif /* intel_gt_power_regs_h */
