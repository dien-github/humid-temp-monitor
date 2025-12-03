/**
 * @file app_wifi.h
 * @brief WiFi connection module - Non-blocking async initialization
 * @version 2.0
 */

#ifndef APP_WIFI_H
#define APP_WIFI_H

#include "app_common.h"

/* ============================================================================
   WIFI CONNECTION CALLBACKS
   ============================================================================ */

/**
 * @brief WiFi event callback function type
 */
typedef void (*wifi_callback_t)(void);

/**
 * @brief WiFi configuration and callbacks
 */
typedef struct {
    const char *ssid;           // WiFi SSID
    const char *password;       // WiFi password
    uint32_t max_retries;       // Max connection retries
    uint32_t timeout_ms;        // Connection timeout
    
    wifi_callback_t on_connected;      // Called on successful connection
    wifi_callback_t on_disconnected;   // Called on disconnection
    wifi_callback_t on_connect_failed; // Called on connection failure
} app_wifi_config_t;

/* ============================================================================
   PUBLIC API
   ============================================================================ */

/**
 * @brief Initialize WiFi module (non-blocking, async)
 * @param config WiFi configuration and callbacks
 * @return APP_OK on success
 * 
 * @note
 * This function returns immediately. WiFi connection happens in background.
 * Callbacks will be invoked when events occur.
 */
app_err_t app_wifi_init(const app_wifi_config_t *config);

/**
 * @brief Get WiFi connection status
 * @return true if connected, false otherwise
 */
bool app_wifi_is_connected(void);

/**
 * @brief Get WiFi RSSI (signal strength)
 * @return RSSI in dBm (0 if not connected)
 */
int8_t app_wifi_get_rssi(void);

/**
 * @brief Get WiFi IP address
 * @param ip_str Buffer to store IP address (e.g., "192.168.1.100")
 * @param max_len Maximum buffer length
 * @return APP_OK on success
 */
app_err_t app_wifi_get_ip_address(char *ip_str, size_t max_len);

/**
 * @brief Disconnect WiFi
 * @return APP_OK on success
 */
app_err_t app_wifi_disconnect(void);

/**
 * @brief Get WiFi status string for debugging
 * @return Status string (e.g., "CONNECTED", "CONNECTING", "DISCONNECTED")
 */
const char* app_wifi_get_status_string(void);

#endif /* APP_WIFI_H */