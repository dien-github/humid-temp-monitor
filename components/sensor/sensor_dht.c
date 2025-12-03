/**
 * @file sensor_dht.c
 * @author grace
 * @brief DHT11 sensor driver with non-blocking capability
 * @version 2.0
 * @date 2025-12-02
 * 
 * Improvements:
 * - Error codes instaed of magic numbers
 * - Configurable GPIO pin
 * - Timeout protection
 * - Better error logging
 * - Input validation
 */

#include "sensor_dht.h"
#include "app_common.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include <string.h>

static const char *TAG = "DHT_SENSOR";

/* =========================================================================
   DHT SENSOR PRIVATE STATE
   ========================================================================= */
typedef struct {
    uint8_t pin;
    bool initialized;
    uint32_t last_read_ms;
    sensor_data_t last_reading;
} dht_context_t;

static dht_context_t g_dht_context = {0};

/* =========================================================================
   HELPER FUNCTIONS 
   ========================================================================= */
/**
 * @brief Wait for GPIO level change with timeout
 * 
 * @param target_level Level to wait for (0 or 1)
 * @param timeout_us Maximum time to wait (us)
 * @return `APP_OK` if level detected, `APP_ERR_TIMEOUT` if timeout
 */
static app_err_t dht_wait_for_level(int target_level, uint32_t timeout_us)
{
    uint64_t start_time = esp_timer_get_time();
    int current_level;

    while (1) {
        current_level = gpio_get_level(g_dht_context.pin);

        if (current_level == target_level) {
            return APP_OK;
        }

        // Check timeout
        if ((esp_timer_get_time() - start_time) > timeout_us) {
            APP_LOG_DEBUG(TAG, "Timeout waiting for level %d after %lu us",
                            target_level, timeout_us);
            return APP_ERR_TIMEOUT;
        }

        // Yield to other tasks (busy waiting)
        // TODO: Need to consider
        ets_delay_us(1);
    }
}

/**
 * @brief Send DHT start signal
 * 
 * 1. Pull data LOW for 18ms - lets DHT11 detect the signal
 * 2. Pull data HIGH for 20-40us - wait for DHT11 response
 * 3. Switch pin to input mode to read data
 * 
 * @return `APP_OK` on success
 */
static app_err_t dht_send_start_signal(void) {
    // Set pin as output
    gpio_set_direction(g_dht_context.pin, GPIO_MODE_OUTPUT);

    // Pull low for 18ms (initialization)
    gpio_set_level(g_dht_context.pin, 0);
    ets_delay_us(18000); // 18ms

    // Release pin (pull HIGH)
    gpio_set_level(g_dht_context.pin, 1);
    ets_delay_us(30); // 30us (20-40us)
    
    // Switch to input mode for sensor response
    gpio_set_direction(g_dht_context.pin, GPIO_MODE_INPUT);

    return APP_OK;
}

/**
 * @brief Read raw 40-bit data from DHT sensor
 * 
 * @param data Buffer to store 5 bytes of data
 * @return `APP_OK` on success, error code on failure
 */
static app_err_t dht_read_raw_data(uint8_t *data) {
    if (!data) {
        return APP_ERR_INVALID_PARAM;
    }

    // Send start signal
    if (dht_send_start_signal() != APP_OK) {
        APP_LOG_ERROR(TAG, "Failed to send start signal");
        return APP_ERR_SENSOR_READ;
    }

    // Wait for sensor response: LOW in 80us
    if (dht_wait_for_level(0, 1000) != APP_OK) {
        APP_LOG_ERROR(TAG, "No sensor response (LOW pulse)");
        return APP_ERR_SENSOR_READ;
    }

    // Wait for sensor response: HIGH ~80us
    if (dht_wait_for_level(1, 1000) != APP_OK) {
        APP_LOG_ERROR(TAG, "No sensor response (HIGH pulse)");
        return APP_ERR_SENSOR_READ;
    }

    // Wait for data start: LOW
    if (dht_wait_for_level(0, 1000) != APP_OK) {
        APP_LOG_ERROR(TAG, "Data phase timeout");
        return APP_ERR_SENSOR_READ;
    }

    // Clear data buffer
    memset(data, 0, 5);

    // Read 40 bits (5 bytes)
    for (int i = 0; i < 40; i++) {
        // Wait for bit to start (HIGH)
        if (dht_wait_for_level(1, 1000) != APP_OK) {
            APP_LOG_ERROR(TAG, "Timeout waiting for bit %d (HIGH)", i);
            return APP_ERR_SENSOR_READ;
        }
        
        // Measure HIGH pulse duration
        uint64_t pulse_start = esp_timer_get_time();

        // Wait for bit to end (LOW)
        if (dht_wait_for_level(0, 1000) != APP_OK) {
            APP_LOG_ERROR(TAG, "Timeout waiting for bit %d (LOW)", i);
            return APP_ERR_SENSOR_READ;
        }

        // Calculate pulse duration
        uint32_t pulse_duration_us = (uint32_t)(esp_timer_get_time() - pulse_start);

        // If pulse > 50us, bit is 1; otherwise 0
        int bit_value = (pulse_duration_us > 50) ? 1 : 0;

        // Pack bits into bytes (MSB first)
        int byte_index = i / 8;
        data[byte_index] = (data[byte_index] << 1) | (bit_value & 0x1);
    }

    // Do I need to pull HIGH again? => current mode is input with pull-up
    return APP_OK;
}

/**
 * @brief Validate DHT data and check checksum
 * 
 * @param data 5-byte data from sensor
 * @return true if checksum valid, false otherwise
 */
static bool dht_validate_data(const uint8_t *data) {
    if (!data) {
        return false;
    }

    // Calculate checksum: sum of first 4 bytes mod 256
    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        APP_LOG_DEBUG(TAG, "Checksum mismatch: calculated %d, received %d",
                        checksum, data[4]);
        return false;
    }
    return true;
}

/* =========================================================================
   PUBLIC SENSOR API
   ========================================================================= */
/**
 * @brief Initialize DHT sensor on specified pin
 * 
 * @param pin GPIO pin number
 * @return `APP_OK` on success
 */
app_err_t sensor_dht_init(uint8_t pin) {
    if (pin > 39) {
        APP_LOG_ERROR(TAG, "Invalid GPIO pin: %d", pin);
        return APP_ERR_INVALID_PARAM;
    }

    if (g_dht_context.initialized) {
        APP_LOG_WARN(TAG, "DHT sensor already initialized on GPIO%d", g_dht_context.pin);
        return APP_OK;
    }

    g_dht_context.pin = pin;

    // Configure GPIO as open-drain with pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "GPIO configuration failed for pin %d: %d", pin, ret);
        return APP_ERR_UNKNOWN;
    }

    // Initial state: HIGH
    gpio_set_level(g_dht_context.pin, 1);

    // Initialize last reading
    g_dht_context.last_reading.is_valid = false;
    g_dht_context.last_reading.last_error = APP_OK;
    g_dht_context.last_reading.temperature = 0.0f;
    g_dht_context.last_reading.humidity = 0.0f;

    g_dht_context.initialized = true;
    g_dht_context.last_read_ms = 0;

    APP_LOG_INFO(TAG, "DHT sensor initialized on GPIO%d", pin);
    return APP_OK;
}

/**
 * @brief Read sensor data with error handling
 * 
 * @param sensor_data Pointer to `sensor_data_t` structure
 * @return `APP_OK` on success, error code on failure
 * 
 * Note: DHT11 requires minimum 1 second between reads.
 */
app_err_t sensor_dht_read(sensor_data_t *sensor_data) {
    if (!sensor_data) {
        return APP_ERR_INVALID_PARAM;
    }

    if (!g_dht_context.initialized) {
        APP_LOG_ERROR(TAG, "DHT sensor not initialized");
        return APP_ERR_UNKNOWN;
    }

    // Check read interval (DHT11 needs >= 1 second between reads)
    uint32_t current_ms = esp_timer_get_time() / 1000;
    if ((current_ms - g_dht_context.last_read_ms) < 1000) {
        APP_LOG_DEBUG(TAG, "DHT read too fast, using cached data");
        memcpy(sensor_data, &g_dht_context.last_reading, sizeof(sensor_data_t));
        return APP_OK;
    }

    // Read raw data
    uint8_t raw_data[5] = {0};
    app_err_t ret = dht_read_raw_data(raw_data);

    if (ret != APP_OK) {
        sensor_data->is_valid = false;
        sensor_data->last_error = ret;
        g_dht_context.last_reading.last_error = ret;
        APP_LOG_ERROR(TAG, "Failed to read sensor: %d", ret);
        return ret;
    }

    // Validate checksum
    if (!dht_validate_data(raw_data)) {
        sensor_data->is_valid = false;
        sensor_data->last_error = APP_ERR_SENSOR_READ;
        g_dht_context.last_reading.last_error = APP_ERR_SENSOR_READ;
        APP_LOG_ERROR(TAG, "DHT data checksum invalid");
        return APP_ERR_SENSOR_READ;
    }

    // Extract temperature and humidity
    // DHT11: data[2] = temperature integer, data[0] = humidity integer
    sensor_data->humidity = ((float)raw_data[0] + (float)raw_data[1] * 0.1f);
    sensor_data->temperature = ((float)raw_data[2] + (float)raw_data[3] * 0.1f);
    sensor_data->timestamp_ms = esp_timer_get_time() / 1000;
    sensor_data->is_valid = true;
    sensor_data->last_error = APP_OK;

    // Update last reading
    g_dht_context.last_read_ms = current_ms;
    memcpy(&g_dht_context.last_reading, sensor_data, sizeof(sensor_data_t));

    APP_LOG_DEBUG(TAG, "Sensor read successful: Temp=%.1f C, Hum=%.1f %%", 
                    sensor_data->temperature, sensor_data->humidity);
    return APP_OK;
}

/**
 * @brief Get last cached sensor reading (fast, no I/O)
 * 
 * @param sensor_data Pointer to `sensor_data_t` structure
 * @return `APP_OK` on success
 */
app_err_t sensor_dht_get_last_reading(sensor_data_t *sensor_data) {
    if (!sensor_data) {
        return APP_ERR_INVALID_PARAM;
    }

    if (!g_dht_context.initialized) {
        APP_LOG_ERROR(TAG, "DHT sensor not initialized");
        return APP_ERR_UNKNOWN;
    }

    memcpy(sensor_data, &g_dht_context.last_reading, sizeof(sensor_data_t));
    return APP_OK;
}

/**
 * @brief Get sensor health/status
 * 
 * @return true if sensor is healthy, false otherwise
 */
bool sensor_dht_is_healthy(void) {
    if (!g_dht_context.initialized) {
        return false;
    }

    // Check if last reading is valid and recent (< 30 seconds)
    uint32_t current_ms = esp_timer_get_time() / 1000;
    return (g_dht_context.last_reading.is_valid &&
            ((current_ms - g_dht_context.last_read_ms) < 30000));
}

/**
 * @brief Get pin number (for diagnostics)
 * 
 * @return GPIO pin number, or 0xFF if not initialized
 */
uint8_t sensor_dht_get_pin(void) {
    return g_dht_context.initialized ? g_dht_context.pin : 0xFF;
}