/*
 * intel_power_regs.h
 * Power Management Register Definitions
 * 
 * Panel Power Control and Backlight PWM registers for Tiger Lake
 */

#ifndef INTEL_POWER_REGS_H
#define INTEL_POWER_REGS_H

#include <IOKit/IOTypes.h>

/* Helper macro for MMIO register definition */
#define _MMIO(offset) (offset)

/* Panel Power Status Register */
#define PCH_PP_STATUS           _MMIO(0xc7200)
#define   PP_ON                 (1 << 31)  // Panel is powered on
#define   PP_SEQUENCE_NONE      (0 << 28)
#define   PP_SEQUENCE_POWER_UP  (1 << 28)
#define   PP_SEQUENCE_POWER_DOWN (2 << 28)
#define   PP_SEQUENCE_MASK      (3 << 28)
#define   PP_SEQUENCE_SHIFT     28
#define   PP_CYCLE_DELAY_ACTIVE (1 << 27)  // Power cycle delay in progress
#define   PP_SEQUENCE_STATE_MASK 0x0000000f
#define   PP_SEQUENCE_STATE_OFF_IDLE (0x0)
#define   PP_SEQUENCE_STATE_OFF_S0_1 (0x1)
#define   PP_SEQUENCE_STATE_OFF_S0_2 (0x2)
#define   PP_SEQUENCE_STATE_OFF_S0_3 (0x3)
#define   PP_SEQUENCE_STATE_ON_IDLE  (0x8)
#define   PP_SEQUENCE_STATE_ON_S1_1  (0x9)
#define   PP_SEQUENCE_STATE_ON_S1_2  (0xa)
#define   PP_SEQUENCE_STATE_ON_S1_3  (0xb)
#define   PP_SEQUENCE_STATE_RESET    (0xf)

/* Panel Power Control Register */
#define PCH_PP_CONTROL          _MMIO(0xc7204)
#define   PANEL_UNLOCK_MASK     (0xabcd << 16)  // Must write 0xabcd to unlock
#define   PANEL_UNLOCK_REGS     (0xabcd << 16)
#define   BXT_POWER_CYCLE_DELAY_MASK (0x1f0)
#define   BXT_POWER_CYCLE_DELAY_SHIFT 4
#define   EDP_FORCE_VDD         (1 << 3)        // Force VDD on
#define   EDP_BLC_ENABLE        (1 << 2)        // Enable backlight
#define   PANEL_POWER_RESET     (1 << 1)        // Reset panel power
#define   PANEL_POWER_ON        (1 << 0)        // Turn panel power on

/* Panel Power On Delays Register */
#define PCH_PP_ON_DELAYS        _MMIO(0xc7208)
#define   PANEL_PORT_SELECT_MASK (3 << 30)
#define   PANEL_PORT_SELECT_LVDS (0 << 30)
#define   PANEL_PORT_SELECT_DPA  (1 << 30)
#define   PANEL_PORT_SELECT_DPC  (2 << 30)
#define   PANEL_PORT_SELECT_DPD  (3 << 30)
#define   PANEL_POWER_UP_DELAY_MASK (0x1fff0000)
#define   PANEL_POWER_UP_DELAY_SHIFT 16         // T1+T2 in 100us units
#define   PANEL_LIGHT_ON_DELAY_MASK (0x1fff)
#define   PANEL_LIGHT_ON_DELAY_SHIFT 0          // T4 in 100us units

/* Panel Power Off Delays Register */
#define PCH_PP_OFF_DELAYS       _MMIO(0xc720c)
#define   PANEL_POWER_DOWN_DELAY_MASK (0x1fff0000)
#define   PANEL_POWER_DOWN_DELAY_SHIFT 16       // T3 in 100us units
#define   PANEL_LIGHT_OFF_DELAY_MASK (0x1fff)
#define   PANEL_LIGHT_OFF_DELAY_SHIFT 0         // T5 in 100us units

/* Panel Power Cycle Delay Register */
#define PCH_PP_DIVISOR          _MMIO(0xc7210)
#define   PP_REFERENCE_DIVIDER_MASK (0xffffff00)
#define   PP_REFERENCE_DIVIDER_SHIFT 8
#define   PANEL_POWER_CYCLE_DELAY_MASK (0x1f)
#define   PANEL_POWER_CYCLE_DELAY_SHIFT 0       // T6 in 100ms units

/* CPU Backlight PWM Control 2 */
#define BLC_PWM_CPU_CTL2        _MMIO(0x48250)
#define   BLM_PWM_ENABLE        (1 << 31)       // Enable PWM
#define   BLM_COMBINATION_MODE  (1 << 30)       // Combination mode
#define   BLM_PIPE_SELECT       (1 << 29)       // Pipe select (legacy)
#define   BLM_PIPE_SELECT_IVB(pipe) ((pipe) << 29)  // Pipe select IVB+
#define   BLM_PIPE_A            (0 << 29)
#define   BLM_PIPE_B            (1 << 29)
#define   BLM_PIPE_C            (2 << 29)
#define   BLM_TRANSCODER_A      BLM_PIPE_A
#define   BLM_TRANSCODER_B      BLM_PIPE_B
#define   BLM_TRANSCODER_C      BLM_PIPE_C
#define   BLM_TRANSCODER_EDP    (3 << 29)
#define   BLM_POLARITY_I965     (1 << 28)       // Polarity
#define   BLM_PHASE_IN_INTERUPT_STATUS (1 << 26)
#define   BLM_PHASE_IN_ENABLE   (1 << 25)
#define   BLM_PHASE_IN_INTERUPT_ENABL (1 << 24)
#define   BLM_PHASE_IN_TIME_BASE_SHIFT (16)
#define   BLM_PHASE_IN_TIME_BASE_MASK (0xff << 16)
#define   BLM_PHASE_IN_COUNT_SHIFT (8)
#define   BLM_PHASE_IN_COUNT_MASK (0xff << 8)
#define   BLM_PHASE_IN_INCR_SHIFT (0)
#define   BLM_PHASE_IN_INCR_MASK (0xff << 0)

/* CPU Backlight PWM Control (Duty Cycle) */
#define BLC_PWM_CPU_CTL         _MMIO(0x48254)
#define   BLM_DUTY_CYCLE_SHIFT  0
#define   BLM_DUTY_CYCLE_MASK   0xffff          // 16-bit duty cycle

/* PCH Backlight PWM Control 1 */
#define BLC_PWM_PCH_CTL1        _MMIO(0xc8250)
#define   BLM_PCH_PWM_ENABLE    (1 << 31)       // Enable PCH PWM
#define   BLM_PCH_OVERRIDE_ENABLE (1 << 30)     // Override CPU PWM
#define   BLM_PCH_POLARITY      (1 << 29)       // Polarity

/* PCH Backlight PWM Control 2 (Duty Cycle) */
#define BLC_PWM_PCH_CTL2        _MMIO(0xc8254)
#define   BLM_PCH_DUTY_CYCLE_SHIFT 0
#define   BLM_PCH_DUTY_CYCLE_MASK 0xffff        // 16-bit duty cycle

/* Helper functions for timing calculations */
static inline uint32_t panel_power_up_delay_to_reg(uint32_t microseconds) {
    // Convert microseconds to 100us units
    return (microseconds / 100) & 0x1fff;
}

static inline uint32_t panel_light_on_delay_to_reg(uint32_t microseconds) {
    // Convert microseconds to 100us units
    return (microseconds / 100) & 0x1fff;
}

static inline uint32_t panel_power_down_delay_to_reg(uint32_t microseconds) {
    // Convert microseconds to 100us units
    return (microseconds / 100) & 0x1fff;
}

static inline uint32_t panel_light_off_delay_to_reg(uint32_t microseconds) {
    // Convert microseconds to 100us units
    return (microseconds / 100) & 0x1fff;
}

static inline uint32_t panel_power_cycle_delay_to_reg(uint32_t microseconds) {
    // Convert microseconds to 100ms units
    return (microseconds / 100000) & 0x1f;
}

static inline uint32_t reg_to_panel_power_up_delay(uint32_t reg_value) {
    // Convert 100us units to microseconds
    return ((reg_value >> PANEL_POWER_UP_DELAY_SHIFT) & 0x1fff) * 100;
}

static inline uint32_t reg_to_panel_light_on_delay(uint32_t reg_value) {
    // Convert 100us units to microseconds
    return (reg_value & 0x1fff) * 100;
}

static inline uint32_t reg_to_panel_power_down_delay(uint32_t reg_value) {
    // Convert 100us units to microseconds
    return ((reg_value >> PANEL_POWER_DOWN_DELAY_SHIFT) & 0x1fff) * 100;
}

static inline uint32_t reg_to_panel_light_off_delay(uint32_t reg_value) {
    // Convert 100us units to microseconds
    return (reg_value & 0x1fff) * 100;
}

static inline uint32_t reg_to_panel_power_cycle_delay(uint32_t reg_value) {
    // Convert 100ms units to microseconds
    return (reg_value & 0x1f) * 100000;
}

#endif /* INTEL_POWER_REGS_H */
