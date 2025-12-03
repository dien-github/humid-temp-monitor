/**
 * @file app_config.h
 * @brief Application configuration management - Public API
 * @version 2.0
 * @author grace
 * 
 * This header provides the public interface for application configuration.
 * Configuration can be loaded from:
 * 1. Compile-time defaults (defined in this file)
 * 2. Non-volatile storage (NVS - survives reboots)
 * 3. Runtime updates via API
 * 
 * Usage:
 * @code
 * app_config_inti_nvs(); // Initialize NVS Storage
 * app_config_load(); // Load configuration
 * app_config_t *config = app_config_get(); // Get config pointer
 * @endcode
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include "app_common.h"

/* =========================================================================
   DEFAULT PIN CONFIGURATION
   ========================================================================= */
/** @defgroup GPIO_PINS GPIO Pin Assignments
 * These are the default GPIO pins if not configured in NVS
 * @{
 */
#define DEFAULT_DHT_PIN 4 /**< DHT11 data pin (GPIO4) */
#define DEFAULT_RELAY_PIN 5 /**< Relay control pin (GPIO5) */
#define DEFAULT_FAN_PIN 18 /**< Fan PWM control pin (GPIO18) */
#define DEFAULT_DHT_TYPE DHT_TYPE_DHT11 /**< Sensor type */
/** @} */

/* =========================================================================
   DEFAULT NETWORK CONFIGURATION
   ========================================================================= */
/** @defgroup NETWORK_CONFIG Network Defaults
 * These are defaults when not stored in NVS
 * @{
 */
#define DEFAULT_MQTT_BROKER_URI "mqtt://192.168.1.40:8883" /**< Default MQTT Broker URI */
#define DEFAULT_MQTT_USERNAME "esp32_device" /**< Default MQTT Username */
#define DEFAULT_MQTT_QOS 1 /**< Default MQTT QoS */
#define DEFAULT_MQTT_RETAIN 0 /**< Default MQTT Retain Flag */
/** @} */

/* =========================================================================
   DEFAULT MQTT TOPICS
   ========================================================================= */
/** @defgroup MQTT_TOPICS MQTT Topics
 * Topic names for pub/sub
 * @{
 */
#define DEFAULT_MQTT_TOPIC_SENSOR "room_1/sensors" /**< ESP32 publishes sensor data */
#define DEFAULT_MQTT_TOPIC_COMMAND "room_1/commands" /**< Server publishes commands */
/** @} */

/* =========================================================================
   DEFAULT TASK CONFIGURATION
   ========================================================================= */
/** @defgroup TASK_CONFIG Task Configuration
 * FreeRTOS task settings
 * @{
 */
/** Sensor task - reads DHT every 5 seconds */
#define DEFAULT_SENSOR_TASK_STACK 3072 /**< Sensor task stack size in bytes */
#define DEFAULT_SENSOR_TASK_PRIORITY 5 /**< Sensor task priority */
#define DEFAULT_SENSOR_READ_INTERVAL_MS 5000 /**< Sensor read interval in milliseconds */

/** MQTT RX task - processes incoming commands */
#define DEFAULT_MQTT_TASK_STACK 4096 /**< MQTT task stack size in bytes */
#define DEFAULT_MQTT_TASK_PRIORITY 10 /**< MQTT task priority */

/** Output task - controls relay and fan */
#define DEFAULT_OUTPUT_TASK_STACK 2048 /**< Output task stack size in bytes */
#define DEFAULT_OUTPUT_TASK_PRIORITY 6 /**< Output task priority */

/** Monitor task - health check */
#define DEFAULT_MONITOR_TASK_STACK 3072 /**< Monitor task stack size in bytes */
#define DEFAULT_MONITOR_TASK_PRIORITY 2 /**< Monitor task priority */
/** @} */

/* =========================================================================
   DEFAULT TIMEOUTS
   ========================================================================= */
/** @defgroup TIMEOUTS Tiemout Values
 * Maximum time for operations before timeout 
 * @{
 */
#define DEFAULT_MQTT_PUBLISH_TIMEOUT_MS 5000 /**< MQTT publish timeout in milliseconds */
#define DEFAULT_DHT_READ_TIMEOUT_MS 3000 /**< DHT read timeout in milliseconds */
#define DEFAULT_WIFI_CONNECT_TIMEOUT_MS 30000 /**< WiFi connect timeout in milliseconds */
/** @} */

/* =========================================================================
   NVS Keys
   ========================================================================= */
/** @defgroup NVS_KEYS NVS Key Names
 * Key used to store/retrieve in NVS
 * @{
 */
#define NVS_NAMESPACE "smarthome" /**< NVS namespace */
#define NVS_KEY_WIFI_SSID "wifi_ssid" /**< WiFi SSID */
#define NVS_KEY_WIFI_PASS "wifi_pass" /**< WiFi Password */
#define NVS_KEY_MQTT_BROKER_URI "mqtt_broker_uri" /**< MQTT Broker URI */
#define NVS_KEY_MQTT_USERNAME "mqtt_username" /**< MQTT Username */
#define NVS_KEY_MQTT_PASSWORD "mqtt_password" /**< MQTT Password */
#define NVS_KEY_DHT_PIN "dht_pin" /**< DHT GPIO Pin */
#define NVS_KEY_RELAY_PIN "relay_pin" /**< Relay GPIO Pin */
#define NVS_KEY_FAN_PIN "fan_pin" /**< Fan GPIO Pin */
#define NVS_KEY_SENSOR_INTERVAL "sensor_interval" /**< Sensor read interval */
/** @} */

/* =========================================================================
   MAX STRING LENGTHS
   ========================================================================= */
/** @defgroup STRING_LENGTHS String Length Limits
 * Maximum buffer sizes for string fields
 * @{
 */
#define MAX_SSID_LEN 32 /**< WiFi SSID max length */
#define MAX_PASSWORD_LEN 64 /**< WiFi Password max length */
#define MAX_MQTT_BROKER_URI_LEN 128 /**< MQTT Broker URI max length */
#define MAX_MQTT_USERNAME_LEN 32 /**< MQTT Username max length */
#define MAX_MQTT_TOPIC_LEN 64 /**< MQTT Topic max length */
/** @} */

/* =========================================================================
   PUBLIC API - Configuration Functions
   ========================================================================= */

/**
 * @brief Initialize NVS system
 * 
 * Must be called before `app_config_load()`.
 * Initializes flash storage for persistent configuration.
 * 
 * @return `APP_OK` on success, error code on failure
 * 
 * @see app_config_load
 */
app_err_t app_config_init_nvs(void);

/**
 * @brief Load application configuration
 * 
 * Loads configuration from:
 * 1. NVS (if available)
 * 2. Fallback to compile-time defaults
 * 
 * Must call app_config_init_nvs() first.
 * 
 * @return `APP_OK` on success, error code on failure
 * 
 * @code
 * app_config_init_nvs();
 * app_config_load();
 * app_config_print(); // For debugging
 * @endcode
 */
app_err_t app_config_load(void);

/**
 * @brief Get pointer to global configuration structure
 * 
 * @return Pointer to `app_config_t` structure (never NULL after load)
 * 
 * @note Configuration is thread-safe for reads.
 * For writes, use app_config_save_* functions.
 * 
 * @code
 * app_config_t *cfg = app_config_get();
 * uint8_t dht_pin = cfg->dht_pin;
 * @endcode
 */
app_config_t* app_config_get(void);

/**
 * @brief Save WiFi credentials to NVS
 * 
 * Persists WiFi credentials to NVS storage.
 * Credentials survive power cycles.
 * 
 * @param ssid WiFi SSID (max 31 characters)
 * @param password WiFi password (max 63 characters)
 * @return `APP_OK` on success, error code on failure
 * 
 * @retval `APP_OK` Successfully saved
 * @retval `APP_ERR_INVALID_PARAM` SSID/password pointer NULL
 * @retval `APP_ERR_INVALID_VALUE` SSID/password too long
 * @retval `APP_ERR_UNKNOWN` NVS operation failed
 * 
   @code
   ```
    app_err_t ret = app_config_save_wifi("MyNetwork", "MyPassword123");
    if (ret == APP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved.");
    }
   ```
   @endcode
 */
app_err_t app_config_save_wifi(const char *ssid, const char *password);

/**
 * @brief Save MQTT Broker URI to NVS
 * 
 * Persists MQTT Broker URI to NVS storage.
 * 
 * @param mqtt_uri MQTT Broker URI (max 127 characters, e.g: "mqtt://broker.hivemq.com:1883")
 * @return `APP_OK` on success, error code on failure
 * 
   @code
   ```
    app_config_save_mqtt_uri("mqtt://broker.hivemq.com:1883");
   ```
   @endcode
 */
app_err_t app_config_save_mqtt_uri(const char *mqtt_uri);

/**
 * @brief Save MQTT credentials to NVS
 * 
 * Persists MQTT username and password to NVS storage.
 * 
 * @param username MQTT username (max 31 characters)
 * @param password MQTT password (max 63 characters)
 * @return `APP_OK` on success, error code on failure
 * 
   @code
   ```
   app_config_save_mqtt_credentials("user123", "pass123");
   ```
   @endcode
 */
app_err_t app_config_save_mqtt_credentials(const char *username, const char *password);

/**
 * @brief Save GPIO pin configuration to NVS
 * 
 * Persists GPIO pin assignments for DHT sensor, relay, and fan to NVS storage.
 * 
 * @param dht_pin GPIO pin for DHT sensor data
 * @param relay_pin GPIO pin for relay control
 * @param fan_pin GPIO pin for fan control
 * @return `APP_OK` on success, error code on failure
 * 
   @code
   ```c
   app_config_save_gpio_pins(4, 5, 18);
   ```
   @endcode
 */
app_err_t app_config_save_gpio_pins(uint8_t dht_pin, uint8_t relay_pin, uint8_t fan_pin);

/**
 * @brief Save sensor read interval to NVS
 * 
 * Persists the sensor read interval (in milliseconds) to NVS storage.
 * 
 * @param interval_ms Sensor read interval in milliseconds
 * @return `APP_OK` on success, error code on failure
 * 
 * @note Minimum interval is 1000 ms.
 * 
   @code
   ```c
   app_config_save_sensor_interval(5000); // 5 seconds
   ```
   @endcode
 */
app_err_t app_config_save_sensor_interval(uint32_t interval_ms);

/**
 * @brief Reset configuration to factory defaults
 * 
 * Clears NVS settings and reverts to compile-time defaults.
 * 
 * @return `APP_OK` on success
 * 
   @code
   ```c
   app_config_reset_to_defaults();
   ```
   @endcode
 */
app_err_t app_config_reset_to_defaults(void);

/**
 * @brief Print current configuration (for debugging)
 * 
 * Logs all configuration values at INFO level.
 * Useful for diagnostics.
 * 
   @code
   ```c
   app_config_print();
   ```
   @endcode
 */
void app_config_print(void);

/**
 * @brief Validate configuration values
 * 
 * Checks that all configuration values are within acceptable ranges.
 * 
 * @return `APP_OK` if valid, error code if invalid
 * 
 * @retval `APP_OK` Configuration is valid
 * @retval `APP_ERR_INVALID_VALUE` One or more configuration values are invalid
 * 
   @code
   ```c
   if (app_config_validate() != APP_OK) {
       ESP_LOGE(TAG, "Configuration is invalid!");
   }
   ```
   @endcode
 */
app_err_t app_config_validate(void);

/**
 * @brief Get configuration parameter by name (string-based)
 * 
 * Advanced: Access configuration values by string key.
 * Useful for dynamic configuration loading.
 * 
 * @param key Parameter name (e.g., "wifi_ssid", "dht_pin")
 * @param value Output buffer to store parameter value as string
 * @param max_len Maximum length of output buffer
 * @return `APP_OK` on success, error code on failure
 * 
   @code
   ```c
   char broker[128];
   app_config_get_param("mqtt_broker_uri", broker, sizeof(broker));
   ```
   @endcode
 */
app_err_t app_config_get_param(const char *key, char *value, size_t max_len);

/**
 * @brief Set configuration parameter by name (string-based)
 * 
 * Advanced: Modify configuration values by string key.
 * Useful for dynamic configuration updates.
 * 
 * @param key Parameter name (e.g., "wifi_ssid", "dht_pin")
 * @param value New value as string
 * @return `APP_OK` on success, error code on failure
 * 
   @code
   ```c
   app_config_set_param("wifi_ssid", "NewNetwork");
   ```
   @endcode
 */
app_err_t app_config_set_param(const char *key, const char *value);

/* =========================================================================
   CONFIGURATION STRUCTURE
   ========================================================================= */
/**
 * @struct app_config_t
 * @brief Complete Application configuration
 * 
 * Contains all configuraable parameters for the application.
 * See app_common.h for structure definition.
 * 
 * @see `app_common.h`
 */

#endif // APP_CONFIG_H