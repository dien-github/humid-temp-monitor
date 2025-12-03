/**
 * @file app_config.c
 * @brief Configuration management with NVS storage and defaults
 * @version 2.0
 * @author grace
 * 
 * @details
 * Handles loading, saving, and managing application configuration
 * including WiFi credentials, MQTT settings, and sensor parameters.
 * Uses NVS for persistent storage with sensible defaults.
 */

#include "app_config.h"
#include "app_common.h"
#include "sensor_dht.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "CONFIG";

/* =========================================================================
   DEFAULT CONFIGURATION
   ========================================================================= */
static const app_config_t default_config = {
    .dht_pin = 4,
    .relay_pin = 5,
    .fan_pin = 18,
    .dht_type = DHT_TYPE_DHT11,

    .wifi_ssid = {0},
    .wifi_pass = {0},

    .mqtt_broker_uri = "mqtt://192.168.1.40:8883",
    .mqtt_username = "esp32_device",
    .mqtt_password = {0},
    .mqtt_topic_sensor = "room_1/sensors",
    .mqtt_topic_command = "room_1/commands",
    .mqtt_qos = 1,

    .sensor_task_stack = 3072, // 3 KB
    .mqtt_task_stack = 4096,   // 4 KB
    .sensor_task_priority = 5,
    .mqtt_task_priority = 10,

    .sensor_read_interval_ms = 5000, // 5 seconds
    .mqtt_publish_timeout_ms = 5000, // 5 seconds
    .dht_read_timeout_ms = 3000      // 3 seconds
};

/* =========================================================================
   CONFIGURATION INSTANCE
   ========================================================================= */
static app_config_t g_app_config;

/* =========================================================================
   NVS HELPER FUNCTIONS
   ========================================================================= */


/**
 * @brief Load string from NVS with fallback to default
 * @param handle NVS handle
 * @param key Key name in NVS
 * @param dest Destination buffer
 * @param dest_size Destination buffer size
 * @param default_value Default value if not found
 * @return APP_OK on success
 */
static app_err_t config_nvs_load_string(
    nvs_handle_t handle,
    const char *key,
    char *dest,
    size_t dest_size,
    const char *default_value)
{
    if (!key || !dest || !dest_size) {
        return APP_ERR_INVALID_PARAM;
    }

    esp_err_t ret = nvs_get_str(handle, key, dest, &dest_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(dest, default_value ? default_value : "", dest_size - 1);
        dest[dest_size - 1] = '\0';
        APP_LOG_DEBUG(TAG, "Using default for key: %s", key);
        return APP_OK;
    } else if (ret == ESP_OK) {
        APP_LOG_DEBUG(TAG, "Loaded form NVS: %s", key);
        return APP_OK;
    } else {
        APP_LOG_ERROR(TAG, "NVS load failed for key  %s: %d", key, ret);
        return APP_ERR_UNKNOWN;
    }
}

/**
 * @brief Load uint8_t from NVS with fallback
 * @param handle NVS handle
 * @param key Key name in NVS
 * @param dest Destination pointer
 * @param default_value Default value if not found
 * @return APP_OK on success
 */
static app_err_t config_nvs_load_u8(
    nvs_handle_t handle,
    const char *key,
    uint8_t *dest,
    uint8_t default_value)
{
    if (!key || !dest) {
        return APP_ERR_INVALID_PARAM;
    }

    esp_err_t ret = nvs_get_u8(handle, key, dest);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *dest = default_value;
        APP_LOG_DEBUG(TAG, "Using default for key: %s = %d", key, *dest);
        return APP_OK;
    } else if (ret == ESP_OK) {
        APP_LOG_DEBUG(TAG, "Loaded form NVS: %s = %d", key, *dest);
        return APP_OK;
    } else {
        APP_LOG_ERROR(TAG, "NVS load failed for key  %s: %d", key, ret);
        return APP_ERR_UNKNOWN;
    }
}
/* =========================================================================
   PUBLIC CONFIG API
   ========================================================================= */
/**
 * @brief Initialize NVS storage
 * @return APP_OK on success
 */
app_err_t app_config_init_nvs(void) {
    APP_LOG_INFO(TAG, "Initializing NVS...");
    
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        APP_LOG_WARN(TAG, "NVS partition invalid, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "NVS initialization failed: %d", ret);
        return APP_ERR_UNKNOWN;
    }

    APP_LOG_INFO(TAG, "NVS initialized successfully.");
    return APP_OK;
}

/**
 * @brief Load configuration from NVS and apply defaults
 * @return APP_OK on success
 */
app_err_t app_config_load(void) {
    APP_LOG_INFO(TAG, "Loading configuration from NVS...");

    memcpy(&g_app_config, &default_config, sizeof(app_config_t));

    nvs_handle_t handle;
    esp_err_t ret = nvs_open("smarthome", NVS_READONLY, &handle);

    if (ret != ESP_OK) {
        APP_LOG_WARN(TAG, "Could not open NVS namespace, using all defaults: %d", ret);
        return APP_OK;
    }

    // Load string configs
    config_nvs_load_string(handle, "wifi_ssid", 
        g_app_config.wifi_ssid,
        sizeof(g_app_config.wifi_ssid),
        "");
    config_nvs_load_string(handle, "wifi_pass",
        g_app_config.wifi_pass,
        sizeof(g_app_config.wifi_pass),
        "");
    config_nvs_load_string(handle, "mqtt_broker_uri",
        g_app_config.mqtt_broker_uri,
        sizeof(g_app_config.mqtt_broker_uri),
        "");
    
    // Load numeric configs
    config_nvs_load_u8(handle, "dht_pin",
        &g_app_config.dht_pin,
        default_config.dht_pin);
    config_nvs_load_u8(handle, "relay_pin",
        &g_app_config.relay_pin,
        default_config.relay_pin);
    config_nvs_load_u8(handle, "fan_pin",
        &g_app_config.fan_pin,
        default_config.fan_pin);
    config_nvs_load_u8(handle, "mqtt_qos",
        &g_app_config.mqtt_qos,
        default_config.mqtt_qos);
    
    nvs_close(handle);
    
    APP_LOG_INFO(TAG, "Configuration loaded successfully.");
    app_config_print();

    return APP_OK;
}

/**
 * @brief Save WiFi credentials to NVS
 * @param ssid SSID (max 31 chars)
 * @param password Password (max 63 chars)
 * @return APP_OK on success
 */
app_err_t app_config_save_wifi(const char *ssid, const char *password) {
    if (!ssid || !password) {
        return APP_ERR_INVALID_PARAM;
    }

    if (strlen(ssid) >= 32 || strlen(password) >= 64) {
        APP_LOG_ERROR(TAG, "SSID or password too long");
        return APP_ERR_INVALID_VALUE;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open("smarthome", NVS_READWRITE, &handle);
    
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "Could not open NVS namespace for writing: %d", ret);
        return APP_ERR_UNKNOWN;
    }

    nvs_set_str(handle, "wifi_ssid", ssid);
    nvs_set_str(handle, "wifi_pass", password);
    nvs_commit(handle);
    nvs_close(handle);

    strncpy(g_app_config.wifi_ssid, ssid, sizeof(g_app_config.wifi_ssid) - 1);
    strncpy(g_app_config.wifi_pass, password, sizeof(g_app_config.wifi_pass) - 1);

    APP_LOG_INFO(TAG, "WiFi credentials saved to NVS.");
    return APP_OK;
}

/**
 * @brief Get current configuration
 * @return Pointer to global config structure
 */
app_config_t* app_config_get(void) {
    return &g_app_config;
}

/**
 * @brief Print current configuration (for debugging)
 */
void app_config_print(void) {
    APP_LOG_INFO(TAG, "=== CURRENT CONFIGURATION ===");
    APP_LOG_INFO(TAG, "DHT Pin: %d", g_app_config.dht_pin);
    APP_LOG_INFO(TAG, "Relay Pin: %d", g_app_config.relay_pin);
    APP_LOG_INFO(TAG, "Fan Pin: %d", g_app_config.fan_pin);
    APP_LOG_INFO(TAG, "DHT Type: %d", g_app_config.dht_type);
    APP_LOG_INFO(TAG, "WiFi SSID: %s", strlen(g_app_config.wifi_ssid) ? g_app_config.wifi_ssid : "(not set)");
    APP_LOG_INFO(TAG, "MQTT Broker URI: %s", g_app_config.mqtt_broker_uri);
    APP_LOG_INFO(TAG, "MQTT QoS: %d", g_app_config.mqtt_qos);
    APP_LOG_INFO(TAG, "Sensor interval (ms): %d ms", g_app_config.sensor_read_interval_ms);
    APP_LOG_INFO(TAG, "Sensor task stack: %d bytes", g_app_config.sensor_task_stack);
    APP_LOG_INFO(TAG, "MQTT task stack: %d bytes", g_app_config.mqtt_task_stack);
    APP_LOG_INFO(TAG, "=============================");
}

/**
 * @brief Reset configuration to default values
 * @return APP_OK on success
 */
app_err_t app_config_reset_to_defaults(void) {
    memcpy(&g_app_config, &default_config, sizeof(app_config_t));
    APP_LOG_INFO(TAG, "Configuration reset to defaults.");
    return APP_OK;
}