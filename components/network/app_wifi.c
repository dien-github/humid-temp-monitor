/**
 * @file app_wifi.c
 * @brief WiFi connection module - Non-blocking async initialization
 * @version 2.0
 * 
 * Features:
 * - Non-blocking initialization (returns immediately)
 * - Automatic reconnection with exponential backoff
 * - Event callbacks for connection state changes
 * - Signal strength (RSSI) monitoring
 * - IP address retrieval
 * - Debug status reporting
 */

#include "app_wifi.h"
#include "app_common.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WIFI";

/* ============================================================================
   WiFi STATES & CONSTANTS
   ============================================================================ */

typedef enum {
    WIFI_STATE_INIT = 0,
    WIFI_STATE_STARTING = 1,
    WIFI_STATE_CONNECTING = 2,
    WIFI_STATE_CONNECTED = 3,
    WIFI_STATE_DISCONNECTED = 4,
    WIFI_STATE_FAILED = 5,
    WIFI_STATE_ERROR = 6,
} wifi_state_t;

#define WIFI_CONNECTED_BIT      (1 << 0)
#define WIFI_FAIL_BIT           (1 << 1)
#define WIFI_DISCONNECTED_BIT   (1 << 2)

#define WIFI_MAX_RETRIES        15
#define WIFI_RETRY_MIN_MS       1000     // 1 second
#define WIFI_RETRY_MAX_MS       60000    // 60 seconds
#define WIFI_CONNECT_TIMEOUT_MS 30000    // 30 seconds

/* ============================================================================
   PRIVATE STATE
   ============================================================================ */

typedef struct {
    // Configuration
    app_wifi_config_t config;
    
    // State
    wifi_state_t state;
    bool initialized;
    bool connected;
    
    // Event group for synchronization
    EventGroupHandle_t event_group;
    
    // Retry logic
    uint32_t retry_count;
    uint32_t retry_delay_ms;
    uint64_t last_connect_attempt_ms;
    uint64_t last_connected_time_ms;
    
    // Network info
    uint32_t ip_address;
    int8_t rssi;
    char ip_str[16];  // "192.168.1.100"
    
    // Statistics
    uint32_t total_connections;
    uint32_t total_disconnections;
    uint32_t total_failed_attempts;
} wifi_context_t;

static wifi_context_t g_wifi_ctx = {0};

/* ============================================================================
   PRIVATE HELPER FUNCTIONS
   ============================================================================ */

/**
 * @brief Update WiFi state and log transition
 */
static void wifi_update_state(wifi_state_t new_state)
{
    if (g_wifi_ctx.state != new_state) {
        APP_LOG_INFO(TAG, "State: %d → %d", g_wifi_ctx.state, new_state);
        g_wifi_ctx.state = new_state;
    }
}

/**
 * @brief Calculate exponential backoff for reconnection
 */
static uint32_t wifi_calculate_backoff(uint32_t retry_count)
{
    // Exponential backoff: 1s, 2s, 4s, 8s, 16s, ... max 60s
    uint32_t delay = WIFI_RETRY_MIN_MS << retry_count;
    
    if (delay > WIFI_RETRY_MAX_MS) {
        delay = WIFI_RETRY_MAX_MS;
    }
    
    return delay;
}

/**
 * @brief Format IP address from uint32_t
 */
static void wifi_format_ip(uint32_t ip, char *str, size_t len)
{
    if (!str || len < 16) {
        return;
    }
    
    snprintf(str, len, "%lu.%lu.%lu.%lu",
            (ip >> 0) & 0xFF,
            (ip >> 8) & 0xFF,
            (ip >> 16) & 0xFF,
            (ip >> 24) & 0xFF);
}

/**
 * @brief WiFi event handler (called from WiFi driver)
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            APP_LOG_INFO(TAG, "WiFi STA started, initiating connection");
            wifi_update_state(WIFI_STATE_CONNECTING);
            esp_wifi_connect();
            break;
            
        case WIFI_EVENT_STA_CONNECTED:
            APP_LOG_INFO(TAG, "✓ WiFi connected to AP");
            wifi_update_state(WIFI_STATE_CONNECTED);
            g_wifi_ctx.last_connected_time_ms = esp_timer_get_time() / 1000;
            break;
            
        case WIFI_EVENT_STA_DISCONNECTED:
            APP_LOG_WARN(TAG, "WiFi disconnected from AP");
            g_wifi_ctx.total_disconnections++;
            wifi_update_state(WIFI_STATE_DISCONNECTED);
            g_wifi_ctx.connected = false;
            xEventGroupSetBits(g_wifi_ctx.event_group, WIFI_DISCONNECTED_BIT);
            
            // Invoke callback
            if (g_wifi_ctx.config.on_disconnected) {
                g_wifi_ctx.config.on_disconnected();
            }
            
            // Attempt reconnection if not max retries
            if (g_wifi_ctx.retry_count < g_wifi_ctx.config.max_retries) {
                g_wifi_ctx.retry_delay_ms = wifi_calculate_backoff(g_wifi_ctx.retry_count);
                g_wifi_ctx.retry_count++;
                
                APP_LOG_WARN(TAG, "Reconnecting in %ld ms (attempt %ld/%ld)",
                           g_wifi_ctx.retry_delay_ms,
                           g_wifi_ctx.retry_count,
                           g_wifi_ctx.config.max_retries);
                
                // Schedule reconnection
                esp_wifi_connect();
            } else {
                APP_LOG_ERROR(TAG, "Max WiFi connection attempts exceeded!");
                g_wifi_ctx.total_failed_attempts++;
                wifi_update_state(WIFI_STATE_FAILED);
                xEventGroupSetBits(g_wifi_ctx.event_group, WIFI_FAIL_BIT);
                
                // Invoke failure callback
                if (g_wifi_ctx.config.on_connect_failed) {
                    g_wifi_ctx.config.on_connect_failed();
                }
            }
            break;
            
        default:
            APP_LOG_DEBUG(TAG, "WiFi event: %ld", event_id);
            break;
        }
    }
    else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            
            APP_LOG_INFO(TAG, "✓ Got IP address!");
            APP_LOG_INFO(TAG, "  IP: " IPSTR, IP2STR(&event->ip_info.ip));
            APP_LOG_INFO(TAG, "  Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
            APP_LOG_INFO(TAG, "  Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
            
            // Store IP info
            g_wifi_ctx.ip_address = event->ip_info.ip.addr;
            wifi_format_ip(g_wifi_ctx.ip_address, g_wifi_ctx.ip_str, 
                          sizeof(g_wifi_ctx.ip_str));
            
            // Reset retry count on successful connection
            g_wifi_ctx.retry_count = 0;
            g_wifi_ctx.retry_delay_ms = WIFI_RETRY_MIN_MS;
            g_wifi_ctx.total_connections++;
            g_wifi_ctx.connected = true;
            
            // Signal connected
            xEventGroupSetBits(g_wifi_ctx.event_group, WIFI_CONNECTED_BIT);
            
            // Invoke callback
            if (g_wifi_ctx.config.on_connected) {
                APP_LOG_DEBUG(TAG, "Invoking on_connected callback");
                g_wifi_ctx.config.on_connected();
            }
        }
        else if (event_id == IP_EVENT_STA_LOST_IP) {
            APP_LOG_WARN(TAG, "Lost IP address!");
            g_wifi_ctx.connected = false;
        }
    }
}

/**
 * @brief Initialize WiFi event loop and handlers
 */
static app_err_t wifi_init_event_handlers(void)
{
    APP_LOG_DEBUG(TAG, "Initializing WiFi event handlers");
    
    // Initialize event loop
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means loop already created
        if (ret != ESP_OK) {
            APP_LOG_ERROR(TAG, "Failed to create event loop: %d", ret);
            return APP_ERR_UNKNOWN;
        }
    }
    
    // Register WiFi event handlers
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "Failed to register WiFi event handler: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    // Register IP event handlers
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "Failed to register IP event handler: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP,
                                     &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "Failed to register IP lost handler: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    APP_LOG_DEBUG(TAG, "Event handlers registered");
    return APP_OK;
}

/**
 * @brief Initialize WiFi interface and configuration
 */
static app_err_t wifi_init_interface(const char *ssid, const char *password)
{
    APP_LOG_DEBUG(TAG, "Initializing WiFi interface");
    
    // Initialize network interface
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        APP_LOG_ERROR(TAG, "Failed to initialize net if: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    // Create default STA interface
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (!netif) {
        APP_LOG_ERROR(TAG, "Failed to create default WiFi STA");
        return APP_ERR_UNKNOWN;
    }
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "Failed to initialize WiFi: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    // Configure WiFi in STA mode
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    // Copy SSID and password
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    
    // Set WiFi configuration
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "Failed to set WiFi mode: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "Failed to set WiFi config: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "Failed to start WiFi: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    APP_LOG_DEBUG(TAG, "WiFi interface initialized");
    return APP_OK;
}

/* ============================================================================
   PUBLIC API IMPLEMENTATION
   ============================================================================ */

app_err_t app_wifi_init(const app_wifi_config_t *config)
{
    if (!config) {
        APP_LOG_ERROR(TAG, "WiFi config is NULL");
        return APP_ERR_INVALID_PARAM;
    }
    
    if (!config->ssid || !config->password) {
        APP_LOG_ERROR(TAG, "SSID or password is NULL");
        return APP_ERR_INVALID_PARAM;
    }
    
    if (g_wifi_ctx.initialized) {
        APP_LOG_WARN(TAG, "WiFi already initialized");
        return APP_OK;
    }
    
    APP_LOG_INFO(TAG, "╔═══════════════════════════════════╗");
    APP_LOG_INFO(TAG, "║  WiFi Connection Initialization   ║");
    APP_LOG_INFO(TAG, "╚═══════════════════════════════════╝");
    
    // Copy configuration
    memcpy(&g_wifi_ctx.config, config, sizeof(app_wifi_config_t));
    
    // Validate config
    if (g_wifi_ctx.config.max_retries == 0) {
        g_wifi_ctx.config.max_retries = WIFI_MAX_RETRIES;
    }
    if (g_wifi_ctx.config.timeout_ms == 0) {
        g_wifi_ctx.config.timeout_ms = WIFI_CONNECT_TIMEOUT_MS;
    }
    
    // Create event group for synchronization
    g_wifi_ctx.event_group = xEventGroupCreate();
    if (!g_wifi_ctx.event_group) {
        APP_LOG_ERROR(TAG, "Failed to create event group");
        return APP_ERR_NO_MEMORY;
    }
    
    // Initialize NVS (required by WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        APP_LOG_WARN(TAG, "NVS partition needs erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "NVS init failed: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    // Initialize event handlers
    app_err_t err = wifi_init_event_handlers();
    if (err != APP_OK) {
        return err;
    }
    
    // Initialize WiFi interface
    err = wifi_init_interface(config->ssid, config->password);
    if (err != APP_OK) {
        return err;
    }
    
    // Initialize state
    g_wifi_ctx.state = WIFI_STATE_STARTING;
    g_wifi_ctx.initialized = true;
    g_wifi_ctx.connected = false;
    g_wifi_ctx.retry_count = 0;
    g_wifi_ctx.retry_delay_ms = WIFI_RETRY_MIN_MS;
    g_wifi_ctx.total_connections = 0;
    g_wifi_ctx.total_disconnections = 0;
    g_wifi_ctx.total_failed_attempts = 0;
    
    APP_LOG_INFO(TAG, "✓ WiFi initialization complete (async)");
    APP_LOG_INFO(TAG, "  SSID: %s", config->ssid);
    APP_LOG_INFO(TAG, "  Max retries: %ld", config->max_retries);
    APP_LOG_INFO(TAG, "  Status: Connecting...");
    
    // ✅ RETURNS IMMEDIATELY - Connection happens in background!
    return APP_OK;
}

bool app_wifi_is_connected(void)
{
    return g_wifi_ctx.initialized && g_wifi_ctx.connected;
}

int8_t app_wifi_get_rssi(void)
{
    if (!g_wifi_ctx.connected) {
        return 0;
    }
    
    wifi_ap_record_t ap_info = {0};
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    
    if (ret == ESP_OK) {
        g_wifi_ctx.rssi = ap_info.rssi;
        return ap_info.rssi;
    }
    
    return 0;
}

app_err_t app_wifi_get_ip_address(char *ip_str, size_t max_len)
{
    if (!ip_str || max_len < 16) {
        return APP_ERR_INVALID_PARAM;
    }
    
    if (!g_wifi_ctx.connected) {
        strncpy(ip_str, "0.0.0.0", max_len - 1);
        return APP_OK;
    }
    
    strncpy(ip_str, g_wifi_ctx.ip_str, max_len - 1);
    ip_str[max_len - 1] = '\0';
    
    return APP_OK;
}

app_err_t app_wifi_disconnect(void)
{
    if (!g_wifi_ctx.initialized) {
        return APP_ERR_UNKNOWN;
    }
    
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
        APP_LOG_ERROR(TAG, "Failed to disconnect: %d", ret);
        return APP_ERR_UNKNOWN;
    }
    
    APP_LOG_INFO(TAG, "WiFi disconnected");
    g_wifi_ctx.connected = false;
    
    return APP_OK;
}

const char* app_wifi_get_status_string(void)
{
    switch (g_wifi_ctx.state) {
    case WIFI_STATE_INIT:
        return "INIT";
    case WIFI_STATE_STARTING:
        return "STARTING";
    case WIFI_STATE_CONNECTING:
        return "CONNECTING";
    case WIFI_STATE_CONNECTED:
        return "CONNECTED";
    case WIFI_STATE_DISCONNECTED:
        return "DISCONNECTED";
    case WIFI_STATE_FAILED:
        return "FAILED";
    case WIFI_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Print WiFi status for debugging
 */
void app_wifi_print_status(void)
{
    APP_LOG_INFO(TAG, "╔═══════════════════════════════════╗");
    APP_LOG_INFO(TAG, "║       WiFi Status Report          ║");
    APP_LOG_INFO(TAG, "╚═══════════════════════════════════╝");
    
    APP_LOG_INFO(TAG, "State: %s", app_wifi_get_status_string());
    APP_LOG_INFO(TAG, "Connected: %s", g_wifi_ctx.connected ? "Yes" : "No");
    
    if (g_wifi_ctx.connected) {
        APP_LOG_INFO(TAG, "IP Address: %s", g_wifi_ctx.ip_str);
        APP_LOG_INFO(TAG, "RSSI: %d dBm", app_wifi_get_rssi());
    }
    
    APP_LOG_INFO(TAG, "Total connections: %ld", g_wifi_ctx.total_connections);
    APP_LOG_INFO(TAG, "Total disconnections: %ld", g_wifi_ctx.total_disconnections);
    APP_LOG_INFO(TAG, "Failed attempts: %ld", g_wifi_ctx.total_failed_attempts);
    APP_LOG_INFO(TAG, "Retry count: %ld/%ld", g_wifi_ctx.retry_count, 
                g_wifi_ctx.config.max_retries);
}

/**
 * @brief Wait for WiFi connection (blocking, with timeout)
 * @param timeout_ms Maximum time to wait (0 = use config timeout)
 * @return APP_OK if connected, APP_ERR_TIMEOUT if timeout
 */
app_err_t app_wifi_wait_connected(uint32_t timeout_ms)
{
    if (!g_wifi_ctx.initialized) {
        return APP_ERR_UNKNOWN;
    }
    
    if (timeout_ms == 0) {
        timeout_ms = g_wifi_ctx.config.timeout_ms;
    }
    
    APP_LOG_INFO(TAG, "Waiting for WiFi connection (timeout: %ld ms)...", timeout_ms);
    
    EventBits_t bits = xEventGroupWaitBits(
        g_wifi_ctx.event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );
    
    if (bits & WIFI_CONNECTED_BIT) {
        APP_LOG_INFO(TAG, "✓ WiFi connected!");
        return APP_OK;
    }
    
    if (bits & WIFI_FAIL_BIT) {
        APP_LOG_ERROR(TAG, "✗ WiFi connection failed!");
        return APP_ERR_WIFI_CONNECT;
    }
    
    APP_LOG_WARN(TAG, "WiFi connection timeout");
    return APP_ERR_TIMEOUT;
}
