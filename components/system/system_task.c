/**
 * @file system_task.c
 * @author grace
 * @brief Multi-task system with proper synchronization and communication
 * @version 2.0
 * @date 2025-12-03
 * 
 * Task Architecture:
 * - Main Task: Initialize system, manage startup sequence
 * - Sensor Task: Read DHT sensor at fixed interval (non-blocking)
 * - MQTT Rx Task: Handle incoming commands
 * - Output Task: Control relay and fan (separate from sensor)
 * - Monitor Task: Health check and diagnostics
 */

#include "system_task.h"
#include "app_common.h"
#include "sensor_dht.h"
#include "app_output.h"
#include "app_mqtt.h"
#include "app_wifi.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "SYSTEM_TASK";

/* ============================================================================
   TASK HANDLES & SYNCHRONIZATION
   ============================================================================ */

   static TaskHandle_t g_task_sensor = NULL;
static TaskHandle_t g_task_mqtt_rx = NULL;
static TaskHandle_t g_task_output = NULL;
static TaskHandle_t g_task_monitor = NULL;

// Event group for system synchronization
static EventGroupHandle_t g_system_events = NULL;

#define EVENT_WIFI_CONNECTED    (1 << 0)
#define EVENT_MQTT_CONNECTED    (1 << 1)
#define EVENT_SYSTEM_READY      (1 << 2)
#define EVENT_ERROR             (1 << 3)

// Queue for sensor data
static QueueHandle_t g_sensor_queue = NULL;

// Queue for control commands
static QueueHandle_t g_command_queue = NULL;

// System status (protected by mutex)
static system_status_t g_system_status = {0};
static portMUX_TYPE g_status_mutex = portMUX_INITIALIZER_UNLOCKED;

/* ============================================================================
   MESSAGE STRUCTURES
   ============================================================================ */

typedef struct {
    sensor_data_t data;
    uint32_t sequence;
} sensor_message_t;

typedef struct {
    char type[16];      // "relay" or "fan"
    int value;          // 0-1 for relay, 0-255 for fan
} control_message_t;

/* ============================================================================
   SYSTEM STATUS MANAGEMENT
   ============================================================================ */

static void system_status_update_state(system_state_t new_state)
{
    portENTER_CRITICAL(&g_status_mutex);
    g_system_status.state = new_state;
    g_system_status.uptime_ms = esp_timer_get_time() / 1000;
    portEXIT_CRITICAL(&g_status_mutex);
    
    APP_LOG_INFO(TAG, "System state changed to: %s", 
                system_state_to_string(new_state));
}

static void system_status_record_error(uint32_t error_code)
{
    portENTER_CRITICAL(&g_status_mutex);
    g_system_status.last_error = error_code;
    g_system_status.error_count++;
    portEXIT_CRITICAL(&g_status_mutex);
}

static void system_status_increment_sensor_reads(void)
{
    portENTER_CRITICAL(&g_status_mutex);
    g_system_status.sensor_read_count++;
    portEXIT_CRITICAL(&g_status_mutex);
}

static void system_status_increment_sensor_errors(void)
{
    portENTER_CRITICAL(&g_status_mutex);
    g_system_status.sensor_error_count++;
    portEXIT_CRITICAL(&g_status_mutex);
}

/* ============================================================================
   TASK FUNCTIONS
   ============================================================================ */

/**
 * @brief Sensor Task - Read DHT at fixed interval
 * 
 * Priority: Medium (5)
 * Stack: 3KB
 * Interval: 5 seconds
 */
static void task_sensor_read(void *pvParameter)
{
    const app_config_t *config = (const app_config_t *)pvParameter;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(config->sensor_read_interval_ms);
    
    uint32_t read_sequence = 0;
    
    APP_LOG_INFO(TAG, "Sensor task started (interval: %ld ms)", 
                config->sensor_read_interval_ms);
    
    while (1) {
        // Wait until next read time
        vTaskDelayUntil(&last_wake_time, period);
        
        // Read sensor
        sensor_data_t reading = {0};
        app_err_t ret = sensor_dht_read(&reading);
        
        if (ret == APP_OK && reading.is_valid) {
            system_status_increment_sensor_reads();
            APP_LOG_DEBUG(TAG, "Sensor read #%ld: T=%.1f°C H=%.1f%%",
                         read_sequence, reading.temperature, reading.humidity);
            
            // Send to queue
            sensor_message_t msg = {
                .data = reading,
                .sequence = read_sequence++
            };
            
            if (xQueueSend(g_sensor_queue, &msg, 0) != pdTRUE) {
                APP_LOG_WARN(TAG, "Sensor queue full, dropping reading");
            }
        } else {
            system_status_increment_sensor_errors();
            APP_LOG_ERROR(TAG, "Sensor read failed: %d", ret);
        }
        
        // Check stack usage (debug)
        UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(g_task_sensor);
        if (stack_high_water < (config->sensor_task_stack / 4)) {
            APP_LOG_WARN(TAG, "Sensor task stack low: %d bytes remaining", 
                        stack_high_water * 4);
        }
    }
}

/**
 * @brief MQTT Receive Task - Process incoming commands
 * 
 * Priority: High (10)
 * Stack: 4KB
 */
static void task_mqtt_receive(void *pvParameter)
{
    const app_config_t *config = (const app_config_t *)pvParameter;
    
    // Wait for MQTT to be ready
    xEventGroupWaitBits(g_system_events, EVENT_MQTT_CONNECTED, 
                       pdFALSE, pdTRUE, portMAX_DELAY);
    
    APP_LOG_INFO(TAG, "MQTT RX task started");
    
    control_message_t cmd;
    while (1) {
        memset(&cmd, 0, sizeof(cmd));
        // Wait for MQTT messages
        if (app_mqtt_receive_command(cmd.type, &cmd.value, pdMS_TO_TICKS(1000)) == APP_OK) {
            APP_LOG_INFO(TAG, "Received command: type=%s value=%d", cmd.type, cmd.value);
            
            // Validate command
            app_err_t ret = APP_OK;
            
            if (strcmp(cmd.type, "relay") == 0) {
                if (cmd.value < 0 || cmd.value > 1) {
                    APP_LOG_WARN(TAG, "Invalid relay value: %d", cmd.value);
                    ret = APP_ERR_INVALID_VALUE;
                } else {
                    ret = app_output_set_relay(cmd.value);
                    APP_LOG_INFO(TAG, "Relay set to %d", cmd.value);
                }
            }
            else if (strcmp(cmd.type, "fan") == 0) {
                if (cmd.value < 0 || cmd.value > 255) {
                    APP_LOG_WARN(TAG, "Invalid fan value: %d (0-255)", cmd.value);
                    ret = APP_ERR_INVALID_VALUE;
                } else {
                    ret = app_output_set_fan_speed(cmd.value);
                    APP_LOG_INFO(TAG, "Fan speed set to %d", cmd.value);
                }
            }
            else {
                APP_LOG_WARN(TAG, "Unknown command type: %s", cmd.type);
                ret = APP_ERR_INVALID_PARAM;
            }
            
            if (ret != APP_OK) {
                system_status_record_error(ret);
            }
        }
    }
}

/**
 * @brief Output Task - Control relay and fan
 * 
 * Priority: Medium (6)
 * Stack: 2KB
 */
static void task_output_control(void *pvParameter)
{
    APP_LOG_INFO(TAG, "Output control task started");
    
    control_message_t cmd = {0};
    
    while (1) {
        // Wait for commands from queue
        if (xQueueReceive(g_command_queue, &cmd, pdMS_TO_TICKS(500)) == pdTRUE) {
            APP_LOG_DEBUG(TAG, "Output command: %s = %d", cmd.type, cmd.value);
            
            if (strcmp(cmd.type, "relay") == 0) {
                app_output_set_relay(cmd.value);
            }
            else if (strcmp(cmd.type, "fan") == 0) {
                app_output_set_fan_speed(cmd.value);
            }
        }
    }
}

/**
 * @brief System Monitor Task - Health check
 * 
 * Priority: Low (3)
 * Stack: 3KB
 * Interval: 10 seconds
 */
static void task_system_monitor(void *pvParameter)
{
    const app_config_t *config = (const app_config_t *)pvParameter;
    
    APP_LOG_INFO(TAG, "System monitor task started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10000);  // 10 seconds
    
    while (1) {
        vTaskDelayUntil(&last_wake_time, period);
        
        // Log system status
        portENTER_CRITICAL(&g_status_mutex);
        APP_LOG_INFO(TAG, "=== System Status ===");
        APP_LOG_INFO(TAG, "State: %s", system_state_to_string(g_system_status.state));
        APP_LOG_INFO(TAG, "Uptime: %ld ms", g_system_status.uptime_ms);
        APP_LOG_INFO(TAG, "Sensor reads: %ld, errors: %ld",
                    g_system_status.sensor_read_count,
                    g_system_status.sensor_error_count);
        APP_LOG_INFO(TAG, "WiFi reconnects: %ld, MQTT reconnects: %ld",
                    g_system_status.wifi_reconnect_count,
                    g_system_status.mqtt_reconnect_count);
        portEXIT_CRITICAL(&g_status_mutex);
        
        // Check sensor health
        if (!sensor_dht_is_healthy()) {
            APP_LOG_WARN(TAG, "Sensor health check failed");
            system_status_record_error(APP_ERR_SENSOR_READ);
        }
        
        // Check heap
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = esp_get_minimum_free_heap_size();
        APP_LOG_DEBUG(TAG, "Heap: free=%zu bytes, min_free=%zu bytes", 
                     free_heap, min_free_heap);
        
        if (free_heap < 5000) {
            APP_LOG_ERROR(TAG, "Critical: Low heap memory!");
        }
    }
}

/* ============================================================================
   PUBLIC SYSTEM TASK API
   ============================================================================ */

/**
 * @brief Initialize task system (create queues, event groups)
 * @return APP_OK on success
 */
app_err_t system_task_init(void)
{
    APP_LOG_INFO(TAG, "Initializing task system...");
    
    // Create event group for system synchronization
    g_system_events = xEventGroupCreate();
    if (!g_system_events) {
        APP_LOG_ERROR(TAG, "Failed to create event group");
        return APP_ERR_NO_MEMORY;
    }
    
    // Create queue for sensor readings
    g_sensor_queue = xQueueCreate(5, sizeof(sensor_message_t));
    if (!g_sensor_queue) {
        APP_LOG_ERROR(TAG, "Failed to create sensor queue");
        return APP_ERR_NO_MEMORY;
    }
    
    // Create queue for control commands
    g_command_queue = xQueueCreate(10, sizeof(control_message_t));
    if (!g_command_queue) {
        APP_LOG_ERROR(TAG, "Failed to create command queue");
        return APP_ERR_NO_MEMORY;
    }
    
    // Initialize system status
    memset(&g_system_status, 0, sizeof(system_status_t));
    g_system_status.state = SYSTEM_STATE_INIT;
    
    APP_LOG_INFO(TAG, "Task system initialized");
    return APP_OK;
}

/**
 * @brief Start all application tasks
 * @param config Pointer to application configuration
 * @return APP_OK on success
 */
app_err_t system_task_start_all(const app_config_t *config)
{
    if (!config) {
        return APP_ERR_INVALID_PARAM;
    }
    
    APP_LOG_INFO(TAG, "Starting all tasks...");
    system_status_update_state(SYSTEM_STATE_HARDWARE_READY);
    
    // Create sensor read task
    BaseType_t ret = xTaskCreate(
        task_sensor_read,
        "sensor_task",
        config->sensor_task_stack,
        (void *)config,
        config->sensor_task_priority,
        &g_task_sensor
    );
    
    if (ret != pdPASS) {
        APP_LOG_ERROR(TAG, "Failed to create sensor task");
        return APP_ERR_NO_MEMORY;
    }
    
    // Create MQTT RX task
    ret = xTaskCreate(
        task_mqtt_receive,
        "mqtt_rx_task",
        config->mqtt_task_stack,
        (void *)config,
        config->mqtt_task_priority,
        &g_task_mqtt_rx
    );
    
    if (ret != pdPASS) {
        APP_LOG_ERROR(TAG, "Failed to create MQTT RX task");
        return APP_ERR_NO_MEMORY;
    }
    
    // Create output control task
    ret = xTaskCreate(
        task_output_control,
        "output_task",
        2048,
        NULL,
        4,
        &g_task_output
    );
    
    if (ret != pdPASS) {
        APP_LOG_ERROR(TAG, "Failed to create output task");
        return APP_ERR_NO_MEMORY;
    }
    
    // Create monitor task
    ret = xTaskCreate(
        task_system_monitor,
        "monitor_task",
        3072,
        (void *)config,
        2,
        &g_task_monitor
    );
    
    if (ret != pdPASS) {
        APP_LOG_ERROR(TAG, "Failed to create monitor task");
        return APP_ERR_NO_MEMORY;
    }
    
    APP_LOG_INFO(TAG, "All tasks started successfully");
    return APP_OK;
}

/**
 * @brief Signal WiFi connected event
 */
void system_task_signal_wifi_connected(void)
{
    xEventGroupSetBits(g_system_events, EVENT_WIFI_CONNECTED);
    system_status_update_state(SYSTEM_STATE_WIFI_CONNECTED);
    APP_LOG_INFO(TAG, "WiFi connected signal sent");
}

/**
 * @brief Signal MQTT connected event
 */
void system_task_signal_mqtt_connected(void)
{
    xEventGroupSetBits(g_system_events, EVENT_MQTT_CONNECTED);
    system_status_update_state(SYSTEM_STATE_MQTT_CONNECTED);
    APP_LOG_INFO(TAG, "MQTT connected signal sent");
}

/**
 * @brief Signal system ready
 */
void system_task_signal_ready(void)
{
    xEventGroupSetBits(g_system_events, EVENT_SYSTEM_READY);
    system_status_update_state(SYSTEM_STATE_OPERATIONAL);
    APP_LOG_INFO(TAG, "System ready signal sent");
}

/**
 * @brief Send sensor data to application (from MQTT callback)
 * @param data Sensor data pointer
 * @return APP_OK if queued, error otherwise
 */
app_err_t system_task_queue_sensor_data(const sensor_data_t *data)
{
    if (!data) {
        return APP_ERR_INVALID_PARAM;
    }
    
    sensor_message_t msg = {
        .data = *data,
        .sequence = 0
    };
    
    if (xQueueSend(g_sensor_queue, &msg, 0) == pdTRUE) {
        return APP_OK;
    }
    
    return APP_ERR_UNKNOWN;
}

/**
 * @brief Get system status (thread-safe)
 * @param status Pointer to status structure
 * @return APP_OK on success
 */
app_err_t system_task_get_status(system_status_t *status)
{
    if (!status) {
        return APP_ERR_INVALID_PARAM;
    }
    
    portENTER_CRITICAL(&g_status_mutex);
    memcpy(status, &g_system_status, sizeof(system_status_t));
    portEXIT_CRITICAL(&g_status_mutex);
    
    return APP_OK;
}

/**
 * @brief Get sensor queue handle (for other modules)
 * @return Queue handle
 */
QueueHandle_t system_task_get_sensor_queue(void)
{
    return g_sensor_queue;
}

/**
 * @brief Get command queue handle (for other modules)
 * @return Queue handle
 */
QueueHandle_t system_task_get_command_queue(void)
{
    return g_command_queue;
}
