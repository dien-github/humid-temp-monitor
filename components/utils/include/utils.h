/**
 * @file utils.h
 * @brief Utility functions and helpers
 * @version 2.0
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* ============================================================================
   TIME UTILITIES
   ============================================================================ */

/**
 * @brief Get current system time in milliseconds
 * @return Current time since boot in milliseconds
 */
uint64_t utils_get_time_ms(void);

/**
 * @brief Get current system time in microseconds
 * @return Current time since boot in microseconds
 */
uint64_t utils_get_time_us(void);

/**
 * @brief Calculate elapsed time in milliseconds
 * @param start_time_ms Starting time (from utils_get_time_ms)
 * @return Elapsed time in milliseconds
 */
uint32_t utils_elapsed_time_ms(uint64_t start_time_ms);

/**
 * @brief Sleep for specified milliseconds (task-aware)
 * @param ms Milliseconds to sleep
 */
void utils_sleep_ms(uint32_t ms);

/**
 * @brief Get human-readable timestamp (ISO 8601 format)
 * @param buf Buffer to store timestamp
 * @param buf_len Buffer length
 * @return Pointer to buf
 */
char* utils_get_timestamp(char *buf, size_t buf_len);

/* ============================================================================
   STRING UTILITIES
   ============================================================================ */

/**
 * @brief Safe string copy with bounds checking
 * @param dest Destination buffer
 * @param src Source string
 * @param max_len Maximum characters to copy (including null terminator)
 * @return Pointer to dest
 */
char* utils_strlcpy(char *dest, const char *src, size_t max_len);

/**
 * @brief Safe string append with bounds checking
 * @param dest Destination buffer
 * @param src Source string to append
 * @param max_len Total buffer length
 * @return Pointer to dest
 */
char* utils_strlcat(char *dest, const char *src, size_t max_len);

/**
 * @brief Convert value to percentage string
 * @param value Value (0-255)
 * @param buf Buffer for percentage string
 * @param buf_len Buffer length
 * @return Pointer to buf (e.g., "50%")
 */
char* utils_value_to_percent_str(uint8_t value, char *buf, size_t buf_len);

/* ============================================================================
   MEMORY UTILITIES
   ============================================================================ */

/**
 * @brief Get free heap size
 * @return Free heap in bytes
 */
uint32_t utils_get_free_heap(void);

/**
 * @brief Get minimum free heap ever reached
 * @return Minimum free heap in bytes
 */
uint32_t utils_get_min_free_heap(void);

/**
 * @brief Check if memory is critical (low)
 * @param threshold_bytes Threshold in bytes (default: 5000)
 * @return true if free heap < threshold
 */
bool utils_is_memory_critical(uint32_t threshold_bytes);

/**
 * @brief Print memory information to log
 */
void utils_print_memory_info(void);

/* ============================================================================
   MATH UTILITIES
   ============================================================================ */

/**
 * @brief Clamp value between min and max
 * @param value Value to clamp
 * @param min Minimum value
 * @param max Maximum value
 * @return Clamped value
 */
int utils_clamp_int(int value, int min, int max);

/**
 * @brief Linear interpolation
 * @param start Start value
 * @param end End value
 * @param progress Progress (0.0 to 1.0)
 * @return Interpolated value
 */
float utils_lerp(float start, float end, float progress);

/**
 * @brief Exponential moving average
 * @param current Current value
 * @param previous Previous average
 * @param alpha Smoothing factor (0.0 to 1.0)
 * @return New average
 */
float utils_exponential_average(float current, float previous, float alpha);

/* ============================================================================
   VALIDATION UTILITIES
   ============================================================================ */

/**
 * @brief Check if temperature value is valid
 * @param temp Temperature in Celsius
 * @return true if valid (-50 to 125°C)
 */
bool utils_is_valid_temperature(float temp);

/**
 * @brief Check if humidity value is valid
 * @param humidity Humidity percentage
 * @return true if valid (0 to 100%)
 */
bool utils_is_valid_humidity(float humidity);

/**
 * @brief Check if PWM duty cycle is valid
 * @param duty PWM duty (0-255)
 * @return true if valid
 */
bool utils_is_valid_pwm_duty(int duty);

/**
 * @brief Check if GPIO pin number is valid for ESP32
 * @param pin GPIO pin number
 * @return true if valid (0-39 for ESP32)
 */
bool utils_is_valid_gpio_pin(int pin);

/* ============================================================================
   CHECKSUM UTILITIES
   ============================================================================ */

/**
 * @brief Calculate simple checksum (XOR)
 * @param data Data buffer
 * @param len Data length
 * @return XOR checksum
 */
uint8_t utils_checksum_xor(const uint8_t *data, size_t len);

/**
 * @brief Calculate CRC32
 * @param data Data buffer
 * @param len Data length
 * @return CRC32 value
 */
uint32_t utils_crc32(const uint8_t *data, size_t len);

/* ============================================================================
   STATISTICS UTILITIES
   ============================================================================ */

/**
 * @brief Simple moving average calculator
 */
typedef struct {
    float *values;
    int window_size;
    int current_idx;
    int count;
} utils_moving_average_t;

/**
 * @brief Create moving average calculator
 * @param window_size Number of values to average
 * @return Allocated calculator (must be freed with utils_moving_average_free)
 */
utils_moving_average_t* utils_moving_average_create(int window_size);

/**
 * @brief Add value to moving average
 * @param avg Calculator
 * @param value Value to add
 */
void utils_moving_average_add(utils_moving_average_t *avg, float value);

/**
 * @brief Get current moving average
 * @param avg Calculator
 * @return Current average (0 if no values yet)
 */
float utils_moving_average_get(const utils_moving_average_t *avg);

/**
 * @brief Reset moving average
 * @param avg Calculator
 */
void utils_moving_average_reset(utils_moving_average_t *avg);

/**
 * @brief Free moving average calculator
 * @param avg Calculator to free
 */
void utils_moving_average_free(utils_moving_average_t *avg);

#endif /* UTILS_H */