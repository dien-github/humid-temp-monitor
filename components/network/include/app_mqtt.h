/**
 * @file app_mqtt.h
 * @brief MQTT client module - Message broker communication
 * @version 2.0
 */

#ifndef APP_MQTT_H
#define APP_MQTT_H

#include "app_common.h"
#include "mqtt_client.h"

/* ============================================================================
   MQTT CALLBACKS
   ============================================================================ */

/**
 * @brief MQTT message callback function type
 */
typedef void (*mqtt_message_callback_t)(const char *topic, const char *data, int data_len);

/**
 * @brief MQTT event callback function type
 */
typedef void (*mqtt_event_callback_t)(void);

/**
 * @brief MQTT configuration and callbacks
 */
typedef struct {
    const char *broker_uri;         // MQTT broker URI (e.g., "mqtts://192.168.1.40:8883")
    const char *username;           // Username (optional)
    const char *password;           // Password (optional)
    uint32_t keepalive_sec;         // Keep-alive interval (seconds)
    uint32_t reconnect_timeout_ms;  // Reconnection timeout
    
    mqtt_message_callback_t on_message;        // Called when message received
    mqtt_event_callback_t on_connected;        // Called on successful connection
    mqtt_event_callback_t on_disconnected;     // Called on disconnection
    mqtt_event_callback_t on_publish_failed;   // Called on publish failure
} mqtt_config_t;

/* ============================================================================
   HELPERS FUNCTION 
   ============================================================================*/
/**
 * @brief Parse incoming MQTT command and queue it for processing
 * 
 * @param data Pointer to command data
 * @param data_len Length of command data
 */
void mqtt_parse_and_queue_command(const char *data, int data_len);

/**
 * @brief Prepare MQTT client configuration from application config
 * 
 * @param mqtt_cfg Pointer to ESP MQTT client configuration structure
 * @param app_cfg Pointer to application MQTT configuration
 */
void mqtt_prepare_config(esp_mqtt_client_config_t *mqtt_cfg, const mqtt_config_t *app_cfg);

/* ============================================================================
   PUBLIC API
   ============================================================================ */

/**
 * @brief Initialize MQTT module (non-blocking, async)
 * @param config MQTT configuration and callbacks
 * @return APP_OK on success
 * 
 * @note
 * This function returns immediately. MQTT connection happens in background.
 * Requires WiFi to be connected first.
 */
app_err_t app_mqtt_init(const mqtt_config_t *config);

/**
 * @brief Check if MQTT is connected
 * @return true if connected to broker
 */
bool app_mqtt_is_connected(void);

/**
 * @brief Publish message to MQTT topic
 * @param topic Topic name (e.g., "room_1/sensors")
 * @param data Message payload
 * @param data_len Payload length
 * @param qos QoS level (0, 1, or 2)
 * @param retain Retain flag (true/false)
 * @return APP_OK on success, error code otherwise
 */
app_err_t app_mqtt_publish(
    const char *topic,
    const char *data,
    int data_len,
    int qos,
    bool retain
);

/**
 * @brief Subscribe to MQTT topic
 * @param topic Topic name (supports wildcards: +, #)
 * @param qos QoS level (0, 1, or 2)
 * @return APP_OK on success
 */
app_err_t app_mqtt_subscribe(const char *topic, int qos);

/**
 * @brief Unsubscribe from MQTT topic
 * @param topic Topic name
 * @return APP_OK on success
 */
app_err_t app_mqtt_unsubscribe(const char *topic);

/**
 * @brief Receive command message from queue (blocking with timeout)
 * @param type Pointer to command type string
 * @param value Pointer to command value
 * @param timeout_ms Maximum wait time (0 = no wait)
 * @return APP_OK on success, APP_ERR_TIMEOUT on timeout
 */
app_err_t app_mqtt_receive_command(char *type, int *value, uint32_t timeout_ms);

/**
 * @brief Get MQTT connection status
 * @return Status string (e.g., "CONNECTED", "CONNECTING", "DISCONNECTED")
 */
const char* app_mqtt_get_status_string(void);

/**
 * @brief Disconnect MQTT
 * @return APP_OK on success
 */
app_err_t app_mqtt_disconnect(void);

/**
 * @brief Get MQTT statistics (for monitoring)
 * @param published Number of messages published
 * @param received Number of messages received
 * @param failed Number of failed operations
 * @return APP_OK on success
 */
app_err_t app_mqtt_get_stats(uint32_t *published, uint32_t *received, uint32_t *failed);

#endif /* APP_MQTT_H */
