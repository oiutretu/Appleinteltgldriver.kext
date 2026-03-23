/*
 * intel_display_types.h
 * 
 * Display subsystem type definitions
 * Minimal stub for compilation
 */

#ifndef INTEL_DISPLAY_TYPES_H
#define INTEL_DISPLAY_TYPES_H

#include "linux_types.h"

/* Forward declarations */
struct intel_display;
struct intel_crtc;
struct intel_encoder;
struct intel_connector;

/* Display power domain types */
enum intel_display_power_domain {
    POWER_DOMAIN_PIPE_A = 0,
    POWER_DOMAIN_PIPE_B,
    POWER_DOMAIN_PIPE_C,
    POWER_DOMAIN_TRANSCODER_A,
    POWER_DOMAIN_TRANSCODER_B,
    POWER_DOMAIN_TRANSCODER_C,
    POWER_DOMAIN_PORT_DDI_A_LANES,
    POWER_DOMAIN_PORT_DDI_B_LANES,
    POWER_DOMAIN_PORT_DDI_C_LANES,
    POWER_DOMAIN_PORT_DDI_D_LANES,
    POWER_DOMAIN_AUX_A,
    POWER_DOMAIN_AUX_B,
    POWER_DOMAIN_AUX_C,
    POWER_DOMAIN_AUX_D,
    POWER_DOMAIN_INIT,
    POWER_DOMAIN_NUM,
};

#endif /* INTEL_DISPLAY_TYPES_H */
