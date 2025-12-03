/**
 * @file app_common.h
 * @brief Common definitions, errors codes, and utilities
 * @version 2.0
 */

#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"

/* =========================================================================
   ERROR CODES
   ========================================================================= */
typedef enum {
    APP_OK = 0,
    APP_ERR_INVALID_PARAM = -1,
    APP_ERR_TIMEOUT = -2,
    APP_ERR_SENSOR_READ = -3,
    APP_ERR_MQTT_PUBLISH = -4,
    APP_ERR_WIFI_CONNECT = -5,
    APP_ERR_MQTT_CONNECT = -6,
    APP_ERR_NO_MEMORY = -7,
    APP_ERR_INVALID_VALUE = -8,
    APP_ERR_UNKNOWN = -99
} app_err_t;

/* =========================================================================
   LOGGING MACROS
   ========================================================================= */
#define APP_LOG_ERROR(tag, fmt, ...) ESP_LOGE(tag, "[ERROR] " fmt, ##__VA_ARGS__)
#define APP_LOG_WARN(tag, fmt, ...) ESP_LOGW(tag, "[WARN] " fmt, ##__VA_ARGS__)
#define APP_LOG_INFO(tag, fmt, ...) ESP_LOGI(tag, "[INFO] " fmt, ##__VA_ARGS__)
#define APP_LOG_DEBUG(tag, fmt, ...) ESP_LOGD(tag, "[DEBUG] " fmt, ##__VA_ARGS__)

#define APP_LOG_ERR_CODE(tag, err) do { \
    if ((err) != APP_OK) { \
        APP_LOG_ERROR(tag, "Error code: %d", (err)); \
    } \
} while(0)

/* =========================================================================
   CONFIGURATION STRUCTURE
   ========================================================================= */
typedef struct {
    // Hardware pins
    uint8_t dht_pin;
    uint8_t relay_pin;
    uint8_t fan_pin;

    // DHT sensor type
    uint8_t dht_type;

    // WiFi credentials (loaded from NVS)
    char wifi_ssid[32];
    char wifi_pass[64];

    // MQTT settings
    char mqtt_broker_uri[128];
    char mqtt_username[32];
    char mqtt_password[64];
    char mqtt_topic_sensor[64];
    char mqtt_topic_command[64];
    uint8_t mqtt_qos;

    // Task stack sizes
    uint16_t sensor_task_stack;
    uint16_t mqtt_task_stack;
    uint8_t sensor_task_priority;
    uint8_t mqtt_task_priority;

    // Sensor interval (ms)
    uint32_t sensor_read_interval_ms;

    // Timeouts (ms)
    uint32_t mqtt_publish_timeout_ms;
    uint32_t dht_read_timeout_ms;
} app_config_t;

/* =========================================================================
   SYSTEM STATUS
   ========================================================================= */
typedef enum {
    SYSTEM_STATE_INIT = 0,
    SYSTEM_STATE_HARDWARE_READY = 1,
    SYSTEM_STATE_WIFI_CONNECTING = 2,
    SYSTEM_STATE_WIFI_CONNECTED = 3,
    SYSTEM_STATE_MQTT_CONNECTING = 4,
    SYSTEM_STATE_MQTT_CONNECTED = 5,
    SYSTEM_STATE_OPERATIONAL = 6,
    SYSTEM_STATE_ERROR = 7
} system_state_t;

typedef struct {
    system_state_t state;
    uint32_t last_error;
    uint32_t error_count;
    uint32_t wifi_reconnect_count;
    uint32_t mqtt_reconnect_count;
    uint32_t sensor_read_count;
    uint32_t sensor_error_count;
    uint64_t uptime_ms;
} system_status_t;

/* =========================================================================
   SENSOR DATA STRUCTURE
   ========================================================================= */
typedef struct {
    float temperature;
    float humidity;
    uint64_t timestamp_ms;
    bool is_valid;
    app_err_t last_error;
} sensor_data_t;

/* =========================================================================
   UTILITY FUNCTIONS
   ========================================================================= */
const char* app_err_to_string(app_err_t err);
const char* system_state_to_string(system_state_t state);

#endif // APP_COMMON_H