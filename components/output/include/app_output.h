/**
 * @file app_output.h
 * @brief Output control module - Relay and Fan PWM control
 * @version 2.0
 */

#ifndef APP_OUTPUT_H
#define APP_OUTPUT_H

#include "app_common.h"

/* ============================================================================
   OUTPUT CONTROL - RELAY & FAN
   ============================================================================ */

/**
 * @brief Relay states
 */
typedef enum {
    RELAY_OFF = 0,
    RELAY_ON = 1,
} relay_state_t;

/**
 * @brief Fan speed range (0-255, 8-bit PWM)
 */
typedef struct {
    uint8_t speed;          // 0-255
    bool is_active;
    uint32_t last_update_ms;
} fan_state_t;

/**
 * @brief Output device status
 */
typedef struct {
    relay_state_t relay;
    fan_state_t fan;
    uint32_t error_count;
    uint64_t total_operations;
} output_status_t;

/* ============================================================================
   PUBLIC API - INITIALIZATION
   ============================================================================ */

/**
 * @brief Initialize output module with configured pins
 * @param relay_pin GPIO pin for relay
 * @param fan_pin GPIO pin for fan PWM
 * @return APP_OK on success
 */
app_err_t app_output_init(uint8_t relay_pin, uint8_t fan_pin);

/* ============================================================================
   PUBLIC API - RELAY CONTROL
   ============================================================================ */

/**
 * @brief Set relay state (ON/OFF)
 * @param state RELAY_ON (1) or RELAY_OFF (0)
 * @return APP_OK on success, error code otherwise
 * 
 * @note
 * - Value must be exactly 0 or 1
 * - Relay has mechanical delay (~50-200ms)
 * - Error code does NOT indicate relay actually moved
 */
app_err_t app_output_set_relay(int state);

/**
 * @brief Get current relay state
 * @return RELAY_ON or RELAY_OFF
 */
relay_state_t app_output_get_relay(void);

/**
 * @brief Toggle relay state
 * @return APP_OK on success
 */
app_err_t app_output_toggle_relay(void);

/* ============================================================================
   PUBLIC API - FAN CONTROL
   ============================================================================ */

/**
 * @brief Set fan speed via PWM
 * @param speed PWM duty cycle (0-255)
 *              0   = Off
 *              128 = ~50% speed
 *              255 = Maximum speed
 * @return APP_OK on success, error code otherwise
 * 
 * @note
 * - Uses LEDC PWM at 5kHz frequency
 * - Linearly maps 0-255 to 0-100% duty
 * - Changes take effect immediately
 * - Values outside 0-255 are clamped
 */
app_err_t app_output_set_fan_speed(int speed);

/**
 * @brief Get current fan speed
 * @return Current PWM duty (0-255)
 */
uint8_t app_output_get_fan_speed(void);

/**
 * @brief Ramp fan speed (smooth acceleration)
 * @param target_speed Target PWM duty (0-255)
 * @param duration_ms Duration of ramp in milliseconds
 * @return APP_OK on success
 * 
 * @note
 * This is non-blocking and runs in background task
 */
app_err_t app_output_ramp_fan_speed(uint8_t target_speed, uint32_t duration_ms);

/* ============================================================================
   PUBLIC API - STATUS & DIAGNOSTICS
   ============================================================================ */

/**
 * @brief Get output module status
 * @param status Pointer to output_status_t structure
 * @return APP_OK on success
 */
app_err_t app_output_get_status(output_status_t *status);

/**
 * @brief Enable/disable output (safety feature)
 * @param enabled true = enable, false = disable all outputs
 * @return APP_OK on success
 * 
 * @note
 * When disabled:
 * - Relay goes to OFF
 * - Fan goes to 0% speed
 * - All commands are rejected
 */
app_err_t app_output_set_enabled(bool enabled);

/**
 * @brief Check if output module is enabled
 * @return true if enabled
 */
bool app_output_is_enabled(void);

/**
 * @brief Emergency stop (immediate shutdown)
 * @return APP_OK on success
 * 
 * @note
 * - Forces relay OFF
 * - Forces fan OFF
 * - Resets module
 */
app_err_t app_output_emergency_stop(void);

/* ============================================================================
   CONSTANTS
   ============================================================================ */

#define FAN_SPEED_MIN         0
#define FAN_SPEED_MAX         255
#define FAN_SPEED_OFF         0
#define FAN_SPEED_HALF        128
#define FAN_SPEED_FULL        255

#define RELAY_STATE_OFF       0
#define RELAY_STATE_ON        1

#endif /* APP_OUTPUT_H */