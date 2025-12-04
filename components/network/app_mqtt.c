/**
 * @file app_mqtt.c
 * @brief MQTT client implementation - Message broker communication
 * @version 2.0
 * 
 * Features:
 * - Async non-blocking connection
 * - Automatic reconnection with exponential backoff
 * - TLS/SSL support
 * - Queue-based message handling
 * - Error tracking and statistics
 */

#include "app_mqtt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include <string.h>
#include <cJSON.h>

static const char *TAG = "MQTT";

/* ============================================================================
   PRIVATE STATE
   ============================================================================ */

/**
 * @brief Runtime context for the MQTT subsystem.
 *
 * @details Encapsulates the MQTT client handle, configuration, connection state,
 * runtime statistics, and a command queue used by the network/app_mqtt
 * component to manage MQTT operations and reconnection logic.
 *
 * Members:
 *  - client: Handle to the underlying ESP MQTT client instance used to
 *            publish/subscribe and receive events.
 *  - config: MQTT configuration (broker address, credentials, topics, QoS,
 *            etc.) used to initialize the client.
 *  - connected: Boolean flag indicating whether the client is currently
 *               connected to the broker.
 *  - initialized: Boolean flag indicating whether the context and client
 *                 have been successfully initialized.
 *
 *  - messages_published: Counter of successfully published messages.
 *  - messages_received: Counter of messages received from the broker.
 *  - publish_failures: Counter of failed publish attempts.
 *  - reconnect_count: Total number of reconnect attempts (used for logging
 *                     and backoff strategies).
 *
 *  - command_queue: RTOS queue handle used to receive commands or control
 *                   messages from other tasks (e.g., publish requests,
 *                   subscription changes, or shutdown signals).
 *
 *  - last_connect_time: Timestamp in milliseconds of the last successful
 *                       connection to the broker. Used for diagnostics and
 *                       backoff calculations.
 *  - reconnect_delay_ms: Current reconnect delay (milliseconds) used by the
 *                        backoff/reconnect logic. Updated on reconnect attempts.
 *
 * @note - Access to this context may occur from multiple tasks and from MQTT
 *    callbacks. Proper synchronization (mutexes or atomic operations) should
 *    be used when multiple writers/readers access mutable fields.
 * @note - Counters are monotonically increasing and may wrap; treat them as
 *    non-atomic snapshots unless synchronized externally.
 * @note - Timestamps are expected to be in the same clock domain as the rest of
 *    the system (e.g., millis since boot) for correct interval calculations.
 */
typedef struct {
    esp_mqtt_client_handle_t client;
    mqtt_config_t config;
    bool connected;
    bool initialized;
    
    // Statistics
    uint32_t messages_published;
    uint32_t messages_received;
    uint32_t publish_failures;
    uint32_t reconnect_count;
    
    // Message queue for commands
    QueueHandle_t command_queue;
    
    // Status
    uint64_t last_connect_time;
    uint32_t reconnect_delay_ms;
} mqtt_context_t;

typedef struct {
    char type[32];
    int value;
} mqtt_command_t;

static mqtt_context_t g_mqtt_ctx = {0};

/* ============================================================================
   MQTT EVENT HANDLER
   ============================================================================ */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        APP_LOG_INFO(TAG, "✓ MQTT connected!");
        g_mqtt_ctx.connected = true;
        g_mqtt_ctx.reconnect_delay_ms = 1000;  // Reset backoff
        g_mqtt_ctx.last_connect_time = esp_timer_get_time() / 1000;
        
        // Invoke connected callback
        if (g_mqtt_ctx.config.on_connected) {
            g_mqtt_ctx.config.on_connected();
        }
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        APP_LOG_WARN(TAG, "MQTT disconnected");
        g_mqtt_ctx.connected = false;
        g_mqtt_ctx.reconnect_count++;
        
        // Exponential backoff: max 60 seconds
        if (g_mqtt_ctx.reconnect_delay_ms < 60000) {
            g_mqtt_ctx.reconnect_delay_ms *= 2;
        }
        
        if (g_mqtt_ctx.config.on_disconnected) {
            g_mqtt_ctx.config.on_disconnected();
        }
        break;
        
    case MQTT_EVENT_SUBSCRIBED:
        APP_LOG_DEBUG(TAG, "Subscribed, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_UNSUBSCRIBED:
        APP_LOG_DEBUG(TAG, "Unsubscribed, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_PUBLISHED:
        APP_LOG_DEBUG(TAG, "Published, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_DATA:
        APP_LOG_DEBUG(TAG, "Received data on topic: %.*s", 
                     event->topic_len, event->topic);
        
        // Parse incoming message
        if (event->data_len > 0) {
            // Create null-terminated copy
            char *data = malloc(event->data_len + 1);
            if (data) {
                memcpy(data, event->data, event->data_len);
                data[event->data_len] = '\0';
                
                // Invoke message callback
                if (g_mqtt_ctx.config.on_message) {
                    g_mqtt_ctx.config.on_message(event->topic, data, event->data_len);
                }
                
                // Parse command if it's a JSON message
                mqtt_parse_and_queue_command(data, event->data_len);
                
                free(data);
                g_mqtt_ctx.messages_received++;
            }
        }
        break;
        
    case MQTT_EVENT_ERROR:
        APP_LOG_ERROR(TAG, "MQTT error event");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            APP_LOG_ERROR(TAG, "TCP transport error");
        }
        break;
        
    default:
        APP_LOG_DEBUG(TAG, "MQTT event id: %d", event->event_id);
        break;
    }
}

/* ============================================================================
   HELPER FUNCTIONS
   ============================================================================ */

/**
 * @brief Parse JSON command and queue it
 */
void mqtt_parse_and_queue_command(const char *data, int data_len)
{
    if (!g_mqtt_ctx.command_queue) {
        return;
    }
    
    // Parse JSON
    cJSON *json = cJSON_ParseWithLength(data, data_len);
    if (!json) {
        APP_LOG_WARN(TAG, "Failed to parse JSON command");
        return;
    }
    
    // Extract type and value
    cJSON *type_item = cJSON_GetObjectItem(json, "type");
    cJSON *value_item = cJSON_GetObjectItem(json, "value");
    
    if (cJSON_IsString(type_item) && cJSON_IsNumber(value_item)) {
        mqtt_command_t cmd = {0};
        strncpy(cmd.type, type_item->valuestring, sizeof(cmd.type) - 1);
        cmd.value = value_item->valueint;
        
        // Queue command
        if (xQueueSend(g_mqtt_ctx.command_queue, &cmd, 0) == pdTRUE) {
            APP_LOG_DEBUG(TAG, "Command queued: type=%s value=%d", 
                         cmd.type, cmd.value);
        } else {
            APP_LOG_WARN(TAG, "Command queue full, dropping command");
        }
    } else {
        APP_LOG_WARN(TAG, "Invalid JSON structure for command");
    }
    
    cJSON_Delete(json);
}

/**
 * @brief Format MQTT config from app config
 */
void mqtt_prepare_config(esp_mqtt_client_config_t *mqtt_cfg, 
                               const mqtt_config_t *app_cfg)
{
    memset(mqtt_cfg, 0, sizeof(esp_mqtt_client_config_t));
    
    mqtt_cfg->broker.address.uri = app_cfg->broker_uri;
    mqtt_cfg->credentials.username = app_cfg->username;
    mqtt_cfg->credentials.authentication.password = app_cfg->password;
    mqtt_cfg->session.keepalive = app_cfg->keepalive_sec;
    mqtt_cfg->network.reconnect_timeout_ms = app_cfg->reconnect_timeout_ms;
    
    // Enable protocol version 3.1.1
    mqtt_cfg->session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
}

/* ============================================================================
   PUBLIC API IMPLEMENTATION
   ============================================================================ */

app_err_t app_mqtt_init(const mqtt_config_t *config)
{
    if (!config) {
        return APP_ERR_INVALID_PARAM;
    }
    
    if (g_mqtt_ctx.initialized) {
        APP_LOG_WARN(TAG, "MQTT already initialized");
        return APP_OK;
    }
    
    APP_LOG_INFO(TAG, "=== MQTT INITIALIZATION ===");
    APP_LOG_INFO(TAG, "Broker: %s", config->broker_uri);
    APP_LOG_INFO(TAG, "Username: %s", config->username ? config->username : "(none)");
    APP_LOG_INFO(TAG, "Keep-alive: %ld seconds", config->keepalive_sec);
    
    // Create command queue
    g_mqtt_ctx.command_queue = xQueueCreate(10, sizeof(mqtt_command_t));
    if (!g_mqtt_ctx.command_queue) {
        APP_LOG_ERROR(TAG, "Failed to create command queue");
        return APP_ERR_NO_MEMORY;
    }
    
    // Copy config
    memcpy(&g_mqtt_ctx.config, config, sizeof(mqtt_config_t));
    
    // Prepare MQTT config
    esp_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_prepare_config(&mqtt_cfg, config);
    
    // Create MQTT client
    g_mqtt_ctx.client = esp_mqtt_client_init(&mqtt_cfg);
    if (!g_mqtt_ctx.client) {
        APP_LOG_ERROR(TAG, "Failed to create MQTT client");
        return APP_ERR_UNKNOWN;
    }
    
    // Register event handler
    esp_mqtt_client_register_event(g_mqtt_ctx.client, ESP_EVENT_ANY_ID, 
                                  mqtt_event_handler, NULL);
    
    // Start MQTT client
    esp_err_t ret = esp_mqtt_client_start(g_mqtt_ctx.client);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "Failed to start MQTT client: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    g_mqtt_ctx.initialized = true;
    g_mqtt_ctx.reconnect_delay_ms = 1000;  // Initial backoff: 1 second
    g_mqtt_ctx.reconnect_count = 0;
    
    APP_LOG_INFO(TAG, "✓ MQTT client initialized (async connection)");
    return APP_OK;
}

bool app_mqtt_is_connected(void)
{
    return g_mqtt_ctx.initialized && g_mqtt_ctx.connected;
}

app_err_t app_mqtt_publish(const char *topic, const char *data, int data_len,
                           int qos, bool retain)
{
    if (!topic || !data || data_len == 0) {
        return APP_ERR_INVALID_PARAM;
    }
    
    if (!g_mqtt_ctx.initialized || !g_mqtt_ctx.connected) {
        APP_LOG_WARN(TAG, "MQTT not connected, cannot publish");
        g_mqtt_ctx.publish_failures++;
        return APP_ERR_MQTT_PUBLISH;
    }
    
    // Validate QoS
    if (qos < 0 || qos > 2) {
        qos = 1;  // Default to QoS 1
    }
    
    // Publish message
    int msg_id = esp_mqtt_client_publish(g_mqtt_ctx.client, topic, data, 
                                         data_len, qos, retain ? 1 : 0);
    
    if (msg_id < 0) {
        APP_LOG_ERROR(TAG, "Failed to publish to %s", topic);
        g_mqtt_ctx.publish_failures++;
        return APP_ERR_MQTT_PUBLISH;
    }
    
    APP_LOG_DEBUG(TAG, "Published to %s (msg_id=%d)", topic, msg_id);
    g_mqtt_ctx.messages_published++;
    
    return APP_OK;
}

app_err_t app_mqtt_subscribe(const char *topic, int qos)
{
    if (!topic) {
        return APP_ERR_INVALID_PARAM;
    }
    
    if (!g_mqtt_ctx.initialized) {
        return APP_ERR_UNKNOWN;
    }
    
    if (qos < 0 || qos > 2) {
        qos = 1;
    }
    
    int msg_id = esp_mqtt_client_subscribe(g_mqtt_ctx.client, topic, qos);
    if (msg_id < 0) {
        APP_LOG_ERROR(TAG, "Failed to subscribe to %s", topic);
        return APP_ERR_UNKNOWN;
    }
    
    APP_LOG_INFO(TAG, "Subscribed to: %s (QoS %d)", topic, qos);
    return APP_OK;
}

app_err_t app_mqtt_unsubscribe(const char *topic)
{
    if (!topic) {
        return APP_ERR_INVALID_PARAM;
    }
    
    if (!g_mqtt_ctx.initialized) {
        return APP_ERR_UNKNOWN;
    }
    
    int msg_id = esp_mqtt_client_unsubscribe(g_mqtt_ctx.client, topic);
    if (msg_id < 0) {
        APP_LOG_ERROR(TAG, "Failed to unsubscribe from %s", topic);
        return APP_ERR_UNKNOWN;
    }
    
    APP_LOG_INFO(TAG, "Unsubscribed from: %s", topic);
    return APP_OK;
}

app_err_t app_mqtt_receive_command(char *type, int *value, uint32_t timeout_ms)
{
    if (!type || !value) {
        return APP_ERR_INVALID_PARAM;
    }
    
    if (!g_mqtt_ctx.command_queue) {
        return APP_ERR_UNKNOWN;
    }
    
    mqtt_command_t cmd = {0};
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    
    if (xQueueReceive(g_mqtt_ctx.command_queue, &cmd, ticks) == pdTRUE) {
        strncpy(type, cmd.type, 31);
        type[31] = '\0';
        *value = cmd.value;
        return APP_OK;
    }
    
    return APP_ERR_TIMEOUT;
}

const char* app_mqtt_get_status_string(void)
{
    if (!g_mqtt_ctx.initialized) {
        return "NOT_INITIALIZED";
    }
    
    if (g_mqtt_ctx.connected) {
        return "CONNECTED";
    }
    
    return "DISCONNECTED";
}

app_err_t app_mqtt_disconnect(void)
{
    if (!g_mqtt_ctx.initialized || !g_mqtt_ctx.client) {
        return APP_ERR_UNKNOWN;
    }
    
    esp_mqtt_client_stop(g_mqtt_ctx.client);
    APP_LOG_INFO(TAG, "MQTT disconnected");
    g_mqtt_ctx.connected = false;
    
    return APP_OK;
}

app_err_t app_mqtt_get_stats(uint32_t *published, uint32_t *received, 
                             uint32_t *failed)
{
    if (!published || !received || !failed) {
        return APP_ERR_INVALID_PARAM;
    }
    
    *published = g_mqtt_ctx.messages_published;
    *received = g_mqtt_ctx.messages_received;
    *failed = g_mqtt_ctx.publish_failures;
    
    return APP_OK;
}
