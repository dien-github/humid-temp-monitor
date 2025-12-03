/**
 * @file app_output.c
 * @brief Output module implementation - Relay and Fan PWM control
 * @version 2.0
 */

#include "app_output.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "OUTPUT";

/* ============================================================================
   PRIVATE STATE
   ============================================================================ */

typedef struct {
    // Pins
    uint8_t relay_pin;
    uint8_t fan_pin;
    
    // State
    relay_state_t relay_state;
    uint8_t fan_speed;
    bool is_enabled;
    bool initialized;
    
    // Statistics
    uint32_t error_count;
    uint64_t total_operations;
    uint32_t relay_toggle_count;
    uint32_t fan_changes;
    
    // Fan ramp
    bool ramp_active;
    uint8_t ramp_target_speed;
    uint32_t ramp_duration_ms;
    uint32_t ramp_start_ms;
    TaskHandle_t ramp_task;
} output_context_t;

static output_context_t g_output_ctx = {0};

/* ============================================================================
   LEDC (PWM) CONFIGURATION
   ============================================================================ */

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          18      // Will be overridden by fan_pin
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT  // 8-bit resolution (0-255)
#define LEDC_FREQUENCY          5000    // 5kHz

/* ============================================================================
   PRIVATE HELPER FUNCTIONS
   ============================================================================ */

/**
 * @brief Initialize LEDC PWM timer for fan control
 */
static app_err_t output_init_ledc(uint8_t fan_pin)
{
    APP_LOG_INFO(TAG, "Initializing LEDC PWM on GPIO%d", fan_pin);
    
    // Configure LEDC timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
        .decimate = 0,
    };
    
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "LEDC timer config failed: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = fan_pin,
        .duty = 0,              // Initial duty = 0%
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "LEDC channel config failed: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    APP_LOG_INFO(TAG, "LEDC PWM initialized: freq=%dHz, resolution=%d-bit",
                LEDC_FREQUENCY, LEDC_DUTY_RES);
    
    return APP_OK;
}

/**
 * @brief Initialize GPIO for relay
 */
static app_err_t output_init_relay(uint8_t relay_pin)
{
    APP_LOG_INFO(TAG, "Initializing relay on GPIO%d", relay_pin);
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << relay_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "GPIO config failed: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    // Set initial state: OFF
    ret = gpio_set_level(relay_pin, RELAY_OFF);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "GPIO set level failed: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    APP_LOG_INFO(TAG, "Relay initialized (initial state: OFF)");
    return APP_OK;
}

/**
 * @brief Fan ramp task (smooth speed transition)
 */
static void task_fan_ramp(void *pvParameter)
{
    APP_LOG_INFO(TAG, "Fan ramp task started");
    
    while (g_output_ctx.ramp_active) {
        uint32_t elapsed_ms = (esp_timer_get_time() / 1000) - g_output_ctx.ramp_start_ms;
        
        if (elapsed_ms >= g_output_ctx.ramp_duration_ms) {
            // Ramp complete
            g_output_ctx.fan_speed = g_output_ctx.ramp_target_speed;
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, g_output_ctx.fan_speed);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            
            APP_LOG_DEBUG(TAG, "Fan ramp complete: %d%%", 
                         (g_output_ctx.fan_speed * 100) / 255);
            
            g_output_ctx.ramp_active = false;
            break;
        }
        
        // Linear interpolation
        uint8_t current_speed = g_output_ctx.fan_speed;
        int delta = g_output_ctx.ramp_target_speed - current_speed;
        uint8_t new_speed = current_speed + (delta * elapsed_ms) / g_output_ctx.ramp_duration_ms;
        
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, new_speed);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        
        vTaskDelay(pdMS_TO_TICKS(50));  // Update every 50ms
    }
    
    APP_LOG_DEBUG(TAG, "Fan ramp task ending");
    g_output_ctx.ramp_task = NULL;
    vTaskDelete(NULL);
}

/* ============================================================================
   PUBLIC API IMPLEMENTATION
   ============================================================================ */

app_err_t app_output_init(uint8_t relay_pin, uint8_t fan_pin)
{
    if (relay_pin > 39 || fan_pin > 39) {
        APP_LOG_ERROR(TAG, "Invalid GPIO pin: relay=%d, fan=%d", relay_pin, fan_pin);
        return APP_ERR_INVALID_PARAM;
    }
    
    if (g_output_ctx.initialized) {
        APP_LOG_WARN(TAG, "Output module already initialized");
        return APP_OK;
    }
    
    APP_LOG_INFO(TAG, "=== OUTPUT MODULE INITIALIZATION ===");
    APP_LOG_INFO(TAG, "Relay pin: GPIO%d", relay_pin);
    APP_LOG_INFO(TAG, "Fan pin: GPIO%d", fan_pin);
    
    // Initialize relay
    app_err_t ret = output_init_relay(relay_pin);
    if (ret != APP_OK) {
        return ret;
    }
    
    // Initialize fan PWM
    ret = output_init_ledc(fan_pin);
    if (ret != APP_OK) {
        return ret;
    }
    
    // Initialize context
    g_output_ctx.relay_pin = relay_pin;
    g_output_ctx.fan_pin = fan_pin;
    g_output_ctx.relay_state = RELAY_OFF;
    g_output_ctx.fan_speed = 0;
    g_output_ctx.is_enabled = true;
    g_output_ctx.initialized = true;
    g_output_ctx.error_count = 0;
    g_output_ctx.total_operations = 0;
    g_output_ctx.ramp_active = false;
    g_output_ctx.ramp_task = NULL;
    
    APP_LOG_INFO(TAG, "✓ Output module initialized successfully");
    return APP_OK;
}

/* ============================================================================
   RELAY CONTROL
   ============================================================================ */

app_err_t app_output_set_relay(int state)
{
    if (!g_output_ctx.initialized) {
        APP_LOG_ERROR(TAG, "Output module not initialized");
        return APP_ERR_UNKNOWN;
    }
    
    if (!g_output_ctx.is_enabled) {
        APP_LOG_WARN(TAG, "Output module disabled, rejecting relay command");
        return APP_ERR_UNKNOWN;
    }
    
    // Validate state value
    if (state != RELAY_OFF && state != RELAY_ON) {
        APP_LOG_ERROR(TAG, "Invalid relay state: %d (must be 0 or 1)", state);
        g_output_ctx.error_count++;
        return APP_ERR_INVALID_VALUE;
    }
    
    // Set GPIO level
    esp_err_t ret = gpio_set_level(g_output_ctx.relay_pin, state);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "GPIO set level failed: %d", ret);
        g_output_ctx.error_count++;
        return APP_ERR_UNKNOWN;
    }
    
    // Update state
    relay_state_t old_state = g_output_ctx.relay_state;
    g_output_ctx.relay_state = (relay_state_t)state;
    g_output_ctx.relay_toggle_count++;
    g_output_ctx.total_operations++;
    
    APP_LOG_INFO(TAG, "Relay: %s → %s", 
                old_state ? "ON" : "OFF",
                state ? "ON" : "OFF");
    
    return APP_OK;
}

relay_state_t app_output_get_relay(void)
{
    if (!g_output_ctx.initialized) {
        return RELAY_OFF;
    }
    return g_output_ctx.relay_state;
}

app_err_t app_output_toggle_relay(void)
{
    relay_state_t new_state = g_output_ctx.relay_state ? RELAY_OFF : RELAY_ON;
    return app_output_set_relay(new_state);
}

/* ============================================================================
   FAN CONTROL
   ============================================================================ */

app_err_t app_output_set_fan_speed(int speed)
{
    if (!g_output_ctx.initialized) {
        APP_LOG_ERROR(TAG, "Output module not initialized");
        return APP_ERR_UNKNOWN;
    }
    
    if (!g_output_ctx.is_enabled) {
        APP_LOG_WARN(TAG, "Output module disabled, rejecting fan command");
        return APP_ERR_UNKNOWN;
    }
    
    // Clamp speed to valid range
    if (speed < FAN_SPEED_MIN) {
        APP_LOG_WARN(TAG, "Fan speed %d clamped to minimum (%d)", 
                    speed, FAN_SPEED_MIN);
        speed = FAN_SPEED_MIN;
    } else if (speed > FAN_SPEED_MAX) {
        APP_LOG_WARN(TAG, "Fan speed %d clamped to maximum (%d)", 
                    speed, FAN_SPEED_MAX);
        speed = FAN_SPEED_MAX;
    }
    
    // Cancel any active ramp
    if (g_output_ctx.ramp_active) {
        g_output_ctx.ramp_active = false;
        APP_LOG_DEBUG(TAG, "Cancelling active fan ramp");
    }
    
    // Set PWM duty
    esp_err_t ret = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, speed);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "LEDC set duty failed: %d", ret);
        g_output_ctx.error_count++;
        return APP_ERR_UNKNOWN;
    }
    
    // Update duty value
    ret = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "LEDC update duty failed: %d", ret);
        g_output_ctx.error_count++;
        return APP_ERR_UNKNOWN;
    }
    
    uint8_t old_speed = g_output_ctx.fan_speed;
    g_output_ctx.fan_speed = (uint8_t)speed;
    g_output_ctx.fan_changes++;
    g_output_ctx.total_operations++;
    
    // Log speed change
    if (old_speed != g_output_ctx.fan_speed) {
        int old_percent = (old_speed * 100) / 255;
        int new_percent = (speed * 100) / 255;
        APP_LOG_INFO(TAG, "Fan speed: %d%% → %d%% (PWM: %d → %d)", 
                    old_percent, new_percent, old_speed, speed);
    }
    
    return APP_OK;
}

uint8_t app_output_get_fan_speed(void)
{
    if (!g_output_ctx.initialized) {
        return 0;
    }
    return g_output_ctx.fan_speed;
}

app_err_t app_output_ramp_fan_speed(uint8_t target_speed, uint32_t duration_ms)
{
    if (!g_output_ctx.initialized) {
        return APP_ERR_UNKNOWN;
    }
    
    if (!g_output_ctx.is_enabled) {
        return APP_ERR_UNKNOWN;
    }
    
    // Clamp target
    if (target_speed > FAN_SPEED_MAX) {
        target_speed = FAN_SPEED_MAX;
    }
    
    // Validate duration
    if (duration_ms == 0) {
        return app_output_set_fan_speed(target_speed);
    }
    
    if (duration_ms < 100 || duration_ms > 60000) {
        APP_LOG_ERROR(TAG, "Invalid ramp duration: %ld ms (100-60000)", duration_ms);
        return APP_ERR_INVALID_VALUE;
    }
    
    // Cancel existing ramp
    if (g_output_ctx.ramp_active) {
        g_output_ctx.ramp_active = false;
    }
    
    APP_LOG_INFO(TAG, "Starting fan ramp: %d%% → %d%% over %ld ms",
                (g_output_ctx.fan_speed * 100) / 255,
                (target_speed * 100) / 255,
                duration_ms);
    
    // Set ramp parameters
    g_output_ctx.ramp_active = true;
    g_output_ctx.ramp_target_speed = target_speed;
    g_output_ctx.ramp_duration_ms = duration_ms;
    g_output_ctx.ramp_start_ms = esp_timer_get_time() / 1000;
    
    // Create ramp task
    BaseType_t ret = xTaskCreate(
        task_fan_ramp,
        "fan_ramp",
        2048,
        NULL,
        5,
        &g_output_ctx.ramp_task
    );
    
    if (ret != pdPASS) {
        APP_LOG_ERROR(TAG, "Failed to create fan ramp task");
        g_output_ctx.ramp_active = false;
        return APP_ERR_NO_MEMORY;
    }
    
    return APP_OK;
}

/* ============================================================================
   STATUS & DIAGNOSTICS
   ============================================================================ */

app_err_t app_output_get_status(output_status_t *status)
{
    if (!status) {
        return APP_ERR_INVALID_PARAM;
    }
    
    if (!g_output_ctx.initialized) {
        return APP_ERR_UNKNOWN;
    }
    
    status->relay = g_output_ctx.relay_state;
    status->fan.speed = g_output_ctx.fan_speed;
    status->fan.is_active = (g_output_ctx.fan_speed > 0);
    status->fan.last_update_ms = esp_timer_get_time() / 1000;
    status->error_count = g_output_ctx.error_count;
    status->total_operations = g_output_ctx.total_operations;
    
    return APP_OK;
}

app_err_t app_output_set_enabled(bool enabled)
{
    g_output_ctx.is_enabled = enabled;
    
    if (!enabled) {
        // Disable all outputs
        APP_LOG_WARN(TAG, "Output module disabled!");
        app_output_set_relay(RELAY_OFF);
        app_output_set_fan_speed(FAN_SPEED_OFF);
    } else {
        APP_LOG_INFO(TAG, "Output module enabled");
    }
    
    return APP_OK;
}

bool app_output_is_enabled(void)
{
    return g_output_ctx.is_enabled;
}

app_err_t app_output_emergency_stop(void)
{
    APP_LOG_ERROR(TAG, "🚨 EMERGENCY STOP TRIGGERED!");
    
    // Force all outputs OFF
    gpio_set_level(g_output_ctx.relay_pin, RELAY_OFF);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    
    // Reset state
    g_output_ctx.relay_state = RELAY_OFF;
    g_output_ctx.fan_speed = 0;
    g_output_ctx.is_enabled = false;
    g_output_ctx.ramp_active = false;
    
    return APP_OK;
}