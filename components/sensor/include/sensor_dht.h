/**
 * @file sensor_dht.h
 * @author grace
 * @brief DHT11 Temperature and Humidity Sensor Driver - Public API
 * @version 2.0
 * @date 2025-12-02
 * 
 * Non-blocking DHT11 sensor driver with error handling,
 * timeout protection, and data validation.
 * 
 * Usage:
    @code
    ```c
    // Initialize on GPIO4
    sensor_dht_init(4);

    // Read sensor data (blocks ~40ms for read)
    sensor_data_t raeding = {0};
    app_err_t ret = sensor_dht_read(&reading);

    if (ret == APP_OK && reading.is_valid) {
        printf("Temperature: %.1f C\n", reading.temperature);
        printf("Humidity: %.1f %%\n", reading.humidity);
    } else {
        printf("Sensor read failed: %s\n", app_err_to_string(ret));
    }

    // Get cached reading (fast, no IO)
    sensor_ht_get_last_reading(&reading);
    
    // Check sensor health
    if (sensor_dht_is_healthy()) {
        printf("Sensor is healthy\n");
    }
    ```
    @endcode
 */

#ifndef SENSOR_DHT_H
#define SENSOR_DHT_H

#include <stdint.h>
#include <stdbool.h>
#include "app_common.h"

/* =========================================================================
   DHT SENSOR TYPES
   ========================================================================= */
/** @defgroup DHT_TYPES Supported DHT Sensor Types
 * @{ 
 */
#define DHT_TYPE_DHT11 0x01 /**< DHT11 Sensor Type */
#define DHT_TYPE_DHT22 0x02 /**< DHT22 Sensor Type */
#define DHT_TYPE_DHT21 0x03 /**< DHT21 Sensor Type */
/** @} */

/** @defgroup DHT_SPECS DHT Sensor Specifications
 * @{
 */
#define DHT_MIN_READ_INTERVAL_MS 1000 /**< Minimum 1 second between reads */
#define DHT_MAX_READ_TIME_MS 3000 /**< Max time for complete read */
#define DHT_SENSOR_CACHE_TIMEOUT_MS 30000 /**< Cache timeout (30 seconds) */
/** @} */

/* =========================================================================
   PUBLIC API - Sensor functions
   ========================================================================= */
/**
 * @brief Initialize DHT sensor on specified GPIO pin
 * 
 * Configures GPIO as open-drain with pull-up resistor.
 * Should be called once at startup before reading.
 * 
 * @param pin GPIO pin number (0-39 on ESP32)
 * @return `APP_OK` on success, error code on failure
 * 
 * @retval APP_OK Initialization successful
 * @retval APP_ERR_INVALID_PARAM Invalid GPIO pin (>39)
 * @retval APP_ERR_UNKNOWN GPIO configuration failed
 * 
 * @note Pin must have external pull-up resistor (~10k).
 * Typical ESP32 boards have built-in pull-ups.
 * 
    @code
    ```c
    app_err_t ret = sensor_dht_init(4); // GPIO4
    if (ret != APP_OK) {
         printf("DHT init failed: %s\n", app_err_to_string(ret));
    }
    ```
    @endcode
 *
 * @see sensor_dht_read
 */
app_err_t sensor_dht_init(uint8_t pin);

/**
 * @brief Read current sensor data
 * 
 * Performs a complete sensor read operation:
 * 1. Send start signal to DHT
 * 2. Wait for sensor response
 * 3. Read 40 bits of data
 * 4. Validate checksum
 * 5. Extract temperature and humidity
 * 
 * @param sensor_data Pointer to sensor_data_t structure for output
 * @return `APP_OK` on success, error code on failure
 * 
 * @retval APP_OK Read successful, data valid
 * @retval APP_ERR_INVALID_PARAM `sensor_data` pointer is NULL
 * @retval APP_ERR_UNKNOWN Sensor not initialized
 * @retval APP_ERR_TIMEOUT Sensor response timeout
 * @retval APP_ERR_SENSOR_READ Checksum validate failed
 * 
 * @note This function is BLOCKING for ~40ms while reading.
 * Should be called from a task, not from ISR context.
 * 
 * @note DHT11 requires minimum 1 second between reads.
 * Shorter intervals will return cached data.
 * 
 * @note On error, `sensor_data->is_valid` will be false,
 * and `sensor_data->last_error` will contain error code.
 * 
    @code
    ```c
    sensor_data_t reading = {0};
    app_err_t ret = sensor_dht_read(&reading);

    if (ret == APP_OK && reading.is_valid) {
        printf("Temperature: %.1f C\n", reading.temperature);
        printf("Humidity: %.1f %%\n", reading.humidity);
        printf("Timestamp: %llu ms\n", reading.timestamp_ms);
    } else {
        printf("Read failed: %s\n", app_err_to_string(ret));
        printf("Last error in data: %d\n", reading.last_error);
    }
    ```
    @endcode
 *
 * @see sensor_dht_get_last_reading
 */
app_err_t sensor_dht_read(sensor_data_t *sensor_data);

/**
 * @brief Get last cached sensor reading (non-blocking)
 * 
 * Returns the most recent valid sensor reading without performing
 * a new I/O operation. Useful for fast data access.
 * 
 * @param sensor_data Pointer to `sensor_data_t` structure for output
 * @return `APP_OK` on success, error code on failure
 * 
 * @retval `APP_OK` Successfully retrieved cached reading
 * @retval `APP_ERR_INVALID_PARAM` `sensor_data` pointer is NULL
 * @retval `APP_ERR_UNKNOWN` Sensor not initialized
 * 
 * @note This is non-blocking and fast (~1-2 us).
 * @note May return stale data if sensor hasn't been read recently.
 * 
    @code
    ```c
    sensor_data_t reading = {0};
    sensor_dht_get_last_reading(&reading);

    if (reading.is_valid) {
        printf("Cached Temperature: %.1f C\n", reading.temperature);
        printf("Cached Humidity: %.1f %%\n", reading.humidity);
    }
    ```
    @endcode
 *
 * @see sensor_dht_read
 */
app_err_t sensor_dht_get_last_reading(sensor_data_t *sensor_data);


/**
 * @brief Check if sensor is healthy
 * 
 * Return true if sensor has valid data within the last 30 seconds.
 * useful for monitoring sensor status.
 * 
 * @return true if healthy, false otherwise
 * 
 * @note A healthy sensor means:
 *      - Sensor has been initialized
 *      - Last reading is valid (checksum OK)
 *      - Last reading is recent (< 30 seconds old)
 * 
   @code
    ```c
    if (sensor_dht_is_healthy()) {
        ESP_LOGI(TAG, "DHT sensor is healthy");
    } else {
        ESP_LOGW(TAG, "DHT sensor health check failed");
    }
    ```
    @endcode
 */
bool sensor_dht_is_healthy(void);

/**
 * @brief Get GPIO pin number used by sensor
 * 
 * Returns the GPIO pin that the sensor is initialized on.
 * 
 * @return GPIO pin number (0-39), or `0xFF` if not initialized
 * 
   @code
    ```c
    uint8_t pin = sensor_dht_get_pin();
    if (pin != 0xFF) {
        ESP_LOGI(TAG, "DHT sensor on GPIO%d", pin);
    }
    ```
    @endcode
 */
uint8_t sensor_dht_get_pin(void);

#endif // SENSOR_DHT_H