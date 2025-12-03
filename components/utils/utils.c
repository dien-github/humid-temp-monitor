/**
 * @file utils.c
 * @brief Utility functions implementation
 * @version 2.0
 */

#include "utils.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "UTILS";

/* ============================================================================
   TIME UTILITIES
   ============================================================================ */

uint64_t utils_get_time_ms(void)
{
    return esp_timer_get_time() / 1000;
}

uint64_t utils_get_time_us(void)
{
    return esp_timer_get_time();
}

uint32_t utils_elapsed_time_ms(uint64_t start_time_ms)
{
    return (uint32_t)(utils_get_time_ms() - start_time_ms);
}

void utils_sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

char* utils_get_timestamp(char *buf, size_t buf_len)
{
    if (!buf || buf_len < 20) {
        return NULL;
    }
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%S", tm_info);
    
    return buf;
}

/* ============================================================================
   STRING UTILITIES
   ============================================================================ */

char* utils_strlcpy(char *dest, const char *src, size_t max_len)
{
    if (!dest || !src || max_len == 0) {
        return dest;
    }
    
    strncpy(dest, src, max_len - 1);
    dest[max_len - 1] = '\0';
    
    return dest;
}

char* utils_strlcat(char *dest, const char *src, size_t max_len)
{
    if (!dest || !src || max_len == 0) {
        return dest;
    }
    
    size_t dest_len = strlen(dest);
    if (dest_len >= max_len) {
        return dest;
    }
    
    strncat(dest, src, max_len - dest_len - 1);
    dest[max_len - 1] = '\0';
    
    return dest;
}

char* utils_value_to_percent_str(uint8_t value, char *buf, size_t buf_len)
{
    if (!buf || buf_len < 5) {
        return buf;
    }
    
    int percent = (value * 100) / 255;
    snprintf(buf, buf_len, "%d%%", percent);
    
    return buf;
}

/* ============================================================================
   MEMORY UTILITIES
   ============================================================================ */

uint32_t utils_get_free_heap(void)
{
    return esp_get_free_heap_size();
}

uint32_t utils_get_min_free_heap(void)
{
    return esp_get_minimum_free_heap_size();
}

bool utils_is_memory_critical(uint32_t threshold_bytes)
{
    return esp_get_free_heap_size() < threshold_bytes;
}

void utils_print_memory_info(void)
{
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    
    ESP_LOGI(TAG, "Memory - Free: %d bytes, Min: %d bytes", free_heap, min_free_heap);
    
    if (free_heap < 5000) {
        ESP_LOGW(TAG, "⚠️  Low memory warning!");
    }
}

/* ============================================================================
   MATH UTILITIES
   ============================================================================ */

int utils_clamp_int(int value, int min, int max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

float utils_lerp(float start, float end, float progress)
{
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    
    return start + (end - start) * progress;
}

float utils_exponential_average(float current, float previous, float alpha)
{
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    
    return (alpha * current) + ((1.0f - alpha) * previous);
}

/* ============================================================================
   VALIDATION UTILITIES
   ============================================================================ */

bool utils_is_valid_temperature(float temp)
{
    return (temp >= -50.0f && temp <= 125.0f);
}

bool utils_is_valid_humidity(float humidity)
{
    return (humidity >= 0.0f && humidity <= 100.0f);
}

bool utils_is_valid_pwm_duty(int duty)
{
    return (duty >= 0 && duty <= 255);
}

bool utils_is_valid_gpio_pin(int pin)
{
    return (pin >= 0 && pin <= 39);
}

/* ============================================================================
   CHECKSUM UTILITIES
   ============================================================================ */

uint8_t utils_checksum_xor(const uint8_t *data, size_t len)
{
    uint8_t checksum = 0;
    
    for (size_t i = 0; i < len; i++) {
        checksum ^= data[i];
    }
    
    return checksum;
}

// Simple CRC32 implementation
uint32_t utils_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc ^ 0xFFFFFFFF;
}

/* ============================================================================
   STATISTICS UTILITIES
   ============================================================================ */

utils_moving_average_t* utils_moving_average_create(int window_size)
{
    if (window_size <= 0) {
        return NULL;
    }
    
    utils_moving_average_t *avg = malloc(sizeof(utils_moving_average_t));
    if (!avg) {
        return NULL;
    }
    
    avg->values = malloc(sizeof(float) * window_size);
    if (!avg->values) {
        free(avg);
        return NULL;
    }
    
    avg->window_size = window_size;
    avg->current_idx = 0;
    avg->count = 0;
    
    memset(avg->values, 0, sizeof(float) * window_size);
    
    return avg;
}

void utils_moving_average_add(utils_moving_average_t *avg, float value)
{
    if (!avg || !avg->values) {
        return;
    }
    
    avg->values[avg->current_idx] = value;
    avg->current_idx = (avg->current_idx + 1) % avg->window_size;
    
    if (avg->count < avg->window_size) {
        avg->count++;
    }
}

float utils_moving_average_get(const utils_moving_average_t *avg)
{
    if (!avg || !avg->values || avg->count == 0) {
        return 0.0f;
    }
    
    float sum = 0.0f;
    for (int i = 0; i < avg->count; i++) {
        sum += avg->values[i];
    }
    
    return sum / avg->count;
}

void utils_moving_average_reset(utils_moving_average_t *avg)
{
    if (!avg || !avg->values) {
        return;
    }
    
    memset(avg->values, 0, sizeof(float) * avg->window_size);
    avg->current_idx = 0;
    avg->count = 0;
}

void utils_moving_average_free(utils_moving_average_t *avg)
{
    if (!avg) {
        return;
    }
    
    if (avg->values) {
        free(avg->values);
    }
    
    free(avg);
}