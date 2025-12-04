/**
 * @file main.c
 * @brief Application entry point with proper initialization sequence
 * @version 2.0
 * 
 * TODO:
 * CHANGE phase : load -> init hardware 
 * 
 * Initialization Sequence:
 * 1. Hardware initialization (fast, non-blocking)
 * 2. Configuration system (load from NVS)
 * 3. Task system (create queues, events)
 * 4. Start all application tasks (independent execution)
 * 5. WiFi connection (async)
 * 6. MQTT connection (async)
 * 7. Tasks synchronize via events
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "app_common.h"
#include "app_output.h"
#include "sensor_dht.h"
#include "app_mqtt.h"
#include "app_wifi.h"
#include "system_task.h"

static const char *TAG = "MAIN";

/* =======================================================================
   FORWARD DECLARATIONS
   ========================================================================= */
void on_wifi_connected(void);
void on_wifi_disconnected(void);
void on_mqtt_connected(void);
void on_mqtt_disconnected(void);
void on_mqtt_command_received(const char *topic, const char *payload, int payload_len);
void print_memory_info(void);

/* =========================================================================
   UTILITY FUNCTION IMPLEMENTATIONS
   ========================================================================= */

/**
 * @brief Convert error code to human-readable string
 * 
 * @param err Error code
 * @return Corresponding string
 */
const char* app_err_to_string(app_err_t err)
{
    switch (err) {
        case APP_OK: return "OK";
        case APP_ERR_INVALID_PARAM: return "INVALID_PARAM";
        case APP_ERR_TIMEOUT: return "TIMEOUT";
        case APP_ERR_SENSOR_READ: return "SENSOR_READ";
        case APP_ERR_MQTT_PUBLISH: return "MQTT_PUBLISH";
        case APP_ERR_WIFI_CONNECT: return "WIFI_CONNECT";
        case APP_ERR_MQTT_CONNECT: return "MQTT_CONNECT";
        case APP_ERR_NO_MEMORY: return "NO_MEMORY";
        case APP_ERR_INVALID_VALUE: return "INVALID_VALUE";
        case APP_ERR_UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN_CODE";
    }
}

/**
 * @brief Convert system state to human-readable string
 * 
 * @param state System state
 * @return Corresponding string
 */
const char* system_state_to_string(system_state_t state)
{
    switch (state) {
        case SYSTEM_STATE_INIT: return "INIT";
        case SYSTEM_STATE_HARDWARE_READY: return "HARDWARE_READY";
        case SYSTEM_STATE_WIFI_CONNECTING: return "WIFI_CONNECTING";
        case SYSTEM_STATE_WIFI_CONNECTED: return "WIFI_CONNECTED";
        case SYSTEM_STATE_MQTT_CONNECTING: return "MQTT_CONNECTING";
        case SYSTEM_STATE_MQTT_CONNECTED: return "MQTT_CONNECTED";
        case SYSTEM_STATE_OPERATIONAL: return "OPERATIONAL";
        case SYSTEM_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN_STATE";
    }
}

/* =========================================================================
   PHASE 1: CONFIGURATION LOAD
   ========================================================================= */
/**
 * @brief Initialize configuration system
 * 
 * @param config_out Pointer to store loaded configuration
 * @return `APP_OK` on success, error code otherwise
 */
static app_err_t config_init(app_config_t **config_out) {
    APP_LOG_INFO(TAG, "=== PHASE 1: CONFIGURATION SYSTEM ===");

    // Initialize NVS
    app_err_t ret = app_config_init_nvs();
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "NVS init failed: %s", app_err_to_string(ret));
        return ret;
    }
    APP_LOG_INFO(TAG, ":))) NVS initialized successfully.");

    // Load configuration (defaults + NVS overrides)
    ret = app_config_load();
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "Configuration load failed: %s", app_err_to_string(ret));
        return ret;
    }
    APP_LOG_INFO(TAG, ":))) Configuration loaded successfully.");

    app_config_t *config = app_config_get();
    *config_out = config;

    // Validate critical config
    if (strlen(config->wifi_ssid) == 0) {
        APP_LOG_WARN(TAG, "WiFi SSID not configured (empty)");
        APP_LOG_WARN(TAG, "Configure via app_config_save_wifi() before WiFi connection");
    }
    
    print_memory_info();
    return APP_OK;
}

/* =========================================================================
   PHASE 2: HARDWARE INITIALIZATION
   ========================================================================= */
/**
 * @brief Initialize hardware components
 * 
 * @param config Pointer to application configuration
 * @return `APP_OK` on success, error code otherwise
 */
static app_err_t hardware_init(const app_config_t *config) {
    APP_LOG_INFO(TAG, "=== PHASE 2: HARDWARE INITIALIZATION ===");

    // Initialization output module (relay, fan)
    app_err_t ret = app_output_init(config->relay_pin, config->fan_pin);
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "Output module init failed: %s", app_err_to_string(ret));
        return ret;
    }
    APP_LOG_INFO(TAG, ":))) Output module initialized (Relay GPIO%d, Fan GPIO%d)",
        config->relay_pin, config->fan_pin);

    // Initialize DHT sensor
    ret = sensor_dht_init(config->dht_pin);
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "DHT sensor init failed: %s", app_err_to_string(ret));
        return ret;
    }
    APP_LOG_INFO(TAG, ":))) DHT sensor intialized on GPIO%d", config->dht_pin);

    // Do a quick test read
    sensor_data_t test_reading = {0};
    ret = sensor_dht_read(&test_reading);
    if (ret != APP_OK) {
        APP_LOG_WARN(TAG, "Initial DHT sensor read failed: %s", app_err_to_string(ret));
    } else if (test_reading.is_valid) {
        APP_LOG_INFO(TAG, "Initial DHT reading: Temp=%.1f C, Hum=%.1f %%", 
            test_reading.temperature, test_reading.humidity);
    }

    return APP_OK;
}

/* =========================================================================
   PHASE 3: TASK SYSTEM INITIALIZATION
   ========================================================================= */
/**
 * @brief Initialize task system
 * 
 * @param config Pointer to application configuration
 * @return `APP_OK` on success, error code otherwise
 */
static app_err_t task_system_init(const app_config_t *config)
{
    APP_LOG_INFO(TAG, "=== PHASE 3: TASK SYSTEM INITIALIZATION ===");

    // Create queues and event groups
    app_err_t ret = system_task_init();
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "Task system init failed: %s", app_err_to_string(ret));
        return ret;
    }
    APP_LOG_INFO(TAG, ":))) Task system initialized successfully.");

    // Start all application tasks
    ret = system_task_start_all(config);
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "Starting application tasks failed: %s", app_err_to_string(ret));
        return ret;
    }
    APP_LOG_INFO(TAG, ":))) Application tasks started successfully.");

    return APP_OK;
}

/* =========================================================================
   PHASE 4: WIFI CONNECTION
   ========================================================================= */
/**
 * @brief Initialize WiFi connection
 * 
 * @param config Pointer to application configuration
 * @return `APP_OK` on success, error code otherwise
 */
static app_err_t wifi_connection_init(const app_config_t *config)
{
    APP_LOG_INFO(TAG, "=== PHASE 4: WIFI CONNECTION ===");
    // Check if WiFi credentials are configured
    if (strlen(config->wifi_ssid) == 0) {
        APP_LOG_WARN(TAG, "WiFi not configured, skipping connection");
        APP_LOG_WARN(TAG, "Use app_config_save_wifi() to configure credentials");
        return APP_OK;
    }

    app_wifi_config_t wifi_cfg = {
        .ssid = config->wifi_ssid,
        .password = config->wifi_pass,
        .max_retries = 5,
        .timeout_ms = 10000,
        .on_connected = on_wifi_connected,
        .on_disconnected = on_wifi_disconnected,
        .on_connect_failed = NULL
    };

    // Initialize WiFi (non-blocking)
    app_err_t ret = app_wifi_init(&wifi_cfg);
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "WiFi init failed: %s", app_err_to_string(ret));
        return ret;
    }

    APP_LOG_INFO(TAG, ":))) WiFi initialization started (async)");
    APP_LOG_INFO(TAG, "Connecting to: %s", config->wifi_ssid);

    // WiFi connection happens asynchronously in background
    // Tasks can still run while WiFi is connecting

    return APP_OK;
}

/* =========================================================================
   PHASE 5: MQTT CONNECTION
   ========================================================================= */
/**
 * @brief Initialize MQTT connection
 * 
 * @param config Pointer to application configuration
 * @return `APP_OK` on success, error code otherwise
 */
static app_err_t mqtt_connection_init(const app_config_t *config)
{
    APP_LOG_INFO(TAG, "=== PHASE 5: MQTT CONNECTION ===");

    mqtt_config_t mqtt_cfg = {
        .broker_uri = config->mqtt_broker_uri,
        .username = config->mqtt_username,
        .password = config->mqtt_password,
        .keepalive_sec = 60,
        .reconnect_timeout_ms = 5000,
        .on_message = on_mqtt_command_received,
        .on_connected = on_mqtt_connected,
        .on_disconnected = on_mqtt_disconnected,
        .on_publish_failed = NULL
    };

    // Initialize MQTT (non-blocking)
    app_err_t ret = app_mqtt_init(&mqtt_cfg);
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "MQTT init failed: %s", app_err_to_string(ret));
        return ret;
    }

    APP_LOG_INFO(TAG, ":))) MQTT initialization started (async)");
    APP_LOG_INFO(TAG, "Connecting to broker: %s", config->mqtt_broker_uri);

    // MQTT connection happens asynchronously in background
    // Tasks can still run while MQTT is connecting
    
    return APP_OK;
}

/* =========================================================================
   PHASE 6: MONITOR CALLBACK
   ========================================================================= */
/**
 * @brief Callback when WiFi connects successfully
 * 
 */
void on_wifi_connected(void)
{
    APP_LOG_INFO(TAG, ":))) WiFi connected!");
    system_task_signal_wifi_connected();
}

/**
 * @brief Callback when WiFi disconnects
 */
void on_wifi_disconnected(void)
{
    APP_LOG_WARN(TAG, ":((( WiFi disconnected!");
}

/**
 * @brief Callback when MQTT connects successfully
 * 
 */
void on_mqtt_connected(void)
{
    APP_LOG_INFO(TAG, ":))) MQTT connected!");
    system_task_signal_mqtt_connected();
}

/**
 * @brief Callback when MQTT disconnects
 * 
 */
void on_mqtt_disconnected(void)
{
    APP_LOG_WARN(TAG, ":((( MQTT disconnected!");
}

/**
 * @brief Callback when MQTT command is received
 * 
 * @param topic Pointer to topic string
 * @param payload Pointer to payload data
 * @param payload_len Length of payload data
 * 
 * This will be processed in the mqtt_rx_task() via queue
 * There will have no processing here.
 */
void on_mqtt_command_received(const char *topic, const char *payload, int payload_len)
{
    APP_LOG_DEBUG(TAG, "MQTT command received on %s: %.*s", topic, payload_len, payload);
}

/* =========================================================================
   APPLICATION ENTRY POINT
   ========================================================================= */
void app_main(void)
{
    APP_LOG_INFO(TAG, "=== APPLICATION START ===");
    
    app_err_t ret = APP_OK;
    app_config_t *config = NULL;

    // ========================================================================
    // PHASE 1: LOAD CONFIGURATION
    // ========================================================================
    ret = config_init(&config);
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "Configuration init failed, aborting: %s", app_err_to_string(ret));
        return;
    }

    if (!config) {
        APP_LOG_ERROR(TAG, "Configuration is NULL, aborting");
        return;
    }

    // ========================================================================
    // PHASE 2: INITIALIZATION HARDWARE
    // ========================================================================
    ret = hardware_init(config);
    if (ret != APP_OK)
    {
        APP_LOG_ERROR(TAG, "Hardware init failed, aborting: %s", app_err_to_string(ret));
        return;
    }

    // ========================================================================
    // PHASE 3: INITIALIZE TASK SYSTEM
    // ========================================================================
    ret = task_system_init(config);
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "Task system init failed, aborting: %s", app_err_to_string(ret));
        return;
    }

    // ========================================================================
    // PHASE 4: INITIALIZE WIFI (async)
    // ========================================================================
    APP_LOG_INFO(TAG, "");
    APP_LOG_INFO(TAG, "Starting network initialization...");

    ret = wifi_connection_init(config);
    if (ret != APP_OK) {
        APP_LOG_ERROR(TAG, "WiFi init failed: %s", app_err_to_string(ret));
        // Can operate without WiFi
    }

    // ========================================================================
    // PHASE 5: INITIALIZE MQTT (async)
    // ========================================================================
    ret = mqtt_connection_init(config);
    if (ret != APP_OK)
    {
        APP_LOG_ERROR(TAG, "MQTT init failed: %s", app_err_to_string(ret));
        // Can operate without MQTT
    }

    // ========================================================================
    // STARTUP COMPLETE
    // ========================================================================
    APP_LOG_INFO(TAG, "=== APPLICATION INITIALIZATION COMPLETE ===");
    APP_LOG_INFO(TAG, "HARDWARE: READY");
    APP_LOG_INFO(TAG, "TASKS: RUNNING");
    APP_LOG_INFO(TAG, "WIFI: CONNECTING...");
    APP_LOG_INFO(TAG, "MQTT: CONNECTING...");
    APP_LOG_INFO(TAG, "===========================================");

    // ========================================================================
    // MONITOR MAIN TASK
    // ========================================================================
    while (1) {
        // Check system health periodically
        system_status_t status = {0};
        system_task_get_status(&status);

        // Log major status changes
        static system_state_t last_state = SYSTEM_STATE_INIT;
        if (status.state != last_state) {
            APP_LOG_INFO(TAG, "System state: %s -> %s",
                system_state_to_string(last_state),
                system_state_to_string(status.state));
            last_state = status.state;

            // Signal system ready when MQTT connected
            if (status.state == SYSTEM_STATE_MQTT_CONNECTED) {
                system_task_signal_ready();
            }
        }

        // Sleep 30 seconds between checks
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

// =========================================================================
// MEMORY REPORT (FOR DEBUGGING)
// =========================================================================
void print_memory_info(void)
{
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    APP_LOG_INFO(TAG, "=== MEMORY STATUS ===");
    APP_LOG_INFO(TAG, "Free heap: %d bytes", free_heap);
    APP_LOG_INFO(TAG, "Min free heap: %d bytes", min_free_heap);
    APP_LOG_INFO(TAG, "Largest block: %d bytes", largest_free_block);
    APP_LOG_INFO(TAG, "");

    if (free_heap < 10000) {
        APP_LOG_WARN(TAG, "!!! LOW MEMORY DETECTED!");
    }
}
