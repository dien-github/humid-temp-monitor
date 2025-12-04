/**
 * @file system_task.h
 * @author grace
 * @brief 
 * @version 2.0
 * @date 2025-12-03
 * 
 * Manages multiple FreeRTOS tasks for different system functions:
 * - Sensor reading task (periodic, 5 senconds)
 * - MQTT receive task (event-driven)
 * - Output control task (command-driven)
 * - System monitor task (periodic, 10 seconds)
 * 
 * Tasks commmunicate via:
 * - Queues (message passing)
 * - Event groups (synchronization)
 * - Shared status (protected by mutex)
 * 
 * Usage:
    @code
    ```c
    // Initialize task system
    system_task_init();

    // Start all tasks
    system_task_start_all(config);

    // Signal major events
    system_task_signal_wifi_connected();
    system_task_signal_mqtt_connected();

    // Get system status
    system_status_t status;
    system_task_get_status(&status);
    printf("System state: %s\n", system_state_to_string(status.state));
    ```
    @endcode
 */

#ifndef SYSTEM_TASK_H
#define SYSTEM_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "app_common.h"
#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* =========================================================================
   PUBLIC API - Task Mangagement
   ========================================================================= */
/**
 * @brief Initialize task system (create queues, events)
 * 
 * Must be called once before starting tasks.
 * Creates FreeRTOS queues and event groups for inter-task communication.
 * 
 * @return `APP_OK` on success, error code otherwise.
 * 
 * @retval APP_OK Task system initialized
 * @retval APP_ERR_NO_MEMORY Failed to create queues/events
 * 
   @code
   ```c
   if (system_task_init() != APP_OK) {
      ESP_LOGE(TAG, "Task system init failed");
      return;
   }
   ```
   @endcode
 * 
 * @see system_task_start_all
 */
app_err_t system_task_init(void);

/**
 * @brief Start all application tasks
 * 
 * Create and starts:
 * 1. Sensor read task (priority 5, 3KB stack)
 * 2. MQTT RX task (priority 10, 4KB stack)
 * 3. Output control task (priority 6, 2KB stack)
 * 4. System monitor task (priority 2, 3KB stack)
 * 
 * @param config Pointer to application configuration
 * @return `APP_OK` on success, error code on failure.
 * 
 * @retval `APP_OK` All tasks started
 * @retval `APP_ERR_INVALID_PARAM` `config` is `NULL`
 * @retval `APP_ERR_NO_MEMORY` Failed to create tasks
 * 
 * @note Tasks start in idle state and wait for events.
 * @note Must call system_task_init() first.
 * 
 * @code
 ```c
   app_config_t *config = app_config_get();
   if (system_task_start_all(config) != APP_OK) {
      ESP_LOGE(TAG, "Failed to start tasks");
      return;
   }
 ```
 * @endcode
 * 
 * @see system_task_init
 */
app_err_t system_task_start_all(const app_config_t *config);

/**
 * @brief Signal WiFi connected event
 * 
 * Called by WiFi module when connection is successful.
 * Triggers MQTT connection and updates system state.
 * 
 * @code
   ```c
   void on_wifi_connected(void) {
       system_task_signal_wifi_connected();
   }
   ```
 * @endcode
 */
void system_task_signal_wifi_connected(void);

/**
 * @brief Signal MQTT connected event
 * 
 * Called by MQTT module when connection is successful.
 * Activates MQTT receive task and updates system state.
 * 
 * @code
   ```c
   void on_mqtt_connected(void) {
       system_task_signal_mqtt_connected();
   }
   ```
 * @endcode
 */
void system_task_signal_mqtt_connected(void);

/**
 * @brief Signal system is fully operational
 * 
 * Called when all subsystems are ready.
 * Sets system state to OPERATIONAL.
 * 
 * @code
   system_task_signal_ready();
 * @endcode
 */
void system_task_signal_ready(void);

/**
 * @brief Get system status (thread-safe)
 * 
 * Retrieves current system status information.
 * Protected by mutex, safe to call from any task/ISR.
 * 
 * @param status Pointer to system_status_t structure for output
 * @return APP_OK on success
 * 
 * @code
   ```c
   system_status_t status = {0};
   system_task_get_status(&status);
   
   printf("System state: %s\n", system_state_to_string(status.state));
   printf("Uptime: %lld ms\n", status.uptime_ms);
   printf("Sensor reads: %ld\n", status.sensor_read_count);
   printf("Sensor errors: %ld\n", status.sensor_error_count);
   ```
 * @endcode
 */
app_err_t system_task_get_status(system_status_t *status);

/**
 * @brief Get sensor data queue handle
 * 
 * Returns the FreeRTOS queue handle used for sensor data.
 * Advanced use: send sensor data from other modules.
 * 
 * @return Queue handle (never NULL after init)
 * 
 * @note Queue size: 5 sensor_message_t
 * @note Item size: sizeof(sensor_message_t)
 * 
 * @code
   ```c
   QueueHandle_t q = system_task_get_sensor_queue();
   // Can now xQueueSend() or xQueueReceive() directly
   ```
 * @endcode
 */
QueueHandle_t system_task_get_sensor_queue(void);

/**
 * @brief Get command queue handle
 * 
 * Returns the FreeRTOS queue handle used for control commands.
 * Advanced use: send commands from other modules.
 * 
 * @return Queue handle (never NULL after init)
 * 
 * @note Queue size: 10 control_message_t
 * @note Item size: sizeof(control_message_t)
 * 
 * @code
   ```c
   QueueHandle_t q = system_task_get_command_queue();
   // Can now xQueueSend() or xQueueReceive() directly
   ```
 * @endcode
 */
QueueHandle_t system_task_get_command_queue(void);

/**
 * @brief Queue sensor data from external source
 * 
 * Sends sensor data to the system for processing.
 * Useful when external sensors send data via other interfaces.
 * 
 * @param data Pointer to sensor_data_t
 * @return APP_OK if queued, error if queue full
 * 
 * @code
 * sensor_data_t reading = {0};
 * reading.temperature = 25.5;
 * reading.humidity = 60.0;
 * reading.is_valid = true;
 * 
 * if (system_task_queue_sensor_data(&reading) == APP_OK) {
 *     ESP_LOGI(TAG, "Sensor data queued");
 * }
 * @endcode
 */
app_err_t system_task_queue_sensor_data(const sensor_data_t *data);

#endif // SYSTEM_TASK_H
