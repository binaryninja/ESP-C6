/**
 * @file wifi_manager.h
 * @brief ESP32-C6 Wi-Fi Manager Component
 * 
 * This component provides a comprehensive Wi-Fi management system for ESP32-C6
 * with hardcoded credentials loaded at build time, event handling, and status monitoring.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wi-Fi Manager Configuration
 */
typedef struct {
    uint32_t max_retry_attempts;        ///< Maximum connection retry attempts
    uint32_t retry_delay_ms;            ///< Delay between retry attempts
    bool auto_reconnect;                ///< Enable automatic reconnection
    wifi_ps_type_t power_save_mode;     ///< Power save mode
} wifi_manager_config_t;

/**
 * @brief Wi-Fi Connection Status
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,       ///< Not connected
    WIFI_STATUS_CONNECTING,             ///< Connection in progress
    WIFI_STATUS_CONNECTED,              ///< Connected with IP
    WIFI_STATUS_FAILED,                 ///< Connection failed
    WIFI_STATUS_RECONNECTING            ///< Reconnection in progress
} wifi_status_t;

/**
 * @brief Wi-Fi Statistics
 */
typedef struct {
    uint32_t connection_attempts;       ///< Total connection attempts
    uint32_t successful_connections;    ///< Successful connections
    uint32_t failed_connections;        ///< Failed connections
    uint32_t disconnections;            ///< Total disconnections
    uint32_t reconnections;             ///< Total reconnections
    uint32_t uptime_seconds;            ///< Connection uptime in seconds
    int8_t rssi;                        ///< Current RSSI (dBm)
    wifi_auth_mode_t auth_mode;         ///< Authentication mode
    uint8_t channel;                    ///< Current channel
} wifi_stats_t;

/**
 * @brief Wi-Fi Manager Event Callback
 * 
 * @param status Current Wi-Fi status
 * @param ip_addr IP address (valid when status is WIFI_STATUS_CONNECTED)
 */
typedef void (*wifi_manager_event_cb_t)(wifi_status_t status, uint32_t ip_addr);

/**
 * @brief Default Wi-Fi Manager Configuration
 */
#define WIFI_MANAGER_CONFIG_DEFAULT() { \
    .max_retry_attempts = 10, \
    .retry_delay_ms = 5000, \
    .auto_reconnect = true, \
    .power_save_mode = WIFI_PS_MIN_MODEM \
}

/**
 * @brief Initialize Wi-Fi Manager
 * 
 * This function initializes the Wi-Fi manager with hardcoded credentials
 * loaded from wifi-creds.txt at build time.
 * 
 * @param config Wi-Fi manager configuration
 * @param event_cb Event callback function (optional)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_init(const wifi_manager_config_t *config, wifi_manager_event_cb_t event_cb);

/**
 * @brief Start Wi-Fi Manager
 * 
 * Starts the Wi-Fi manager and begins connection process
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Stop Wi-Fi Manager
 * 
 * Stops the Wi-Fi manager and disconnects from network
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief Deinitialize Wi-Fi Manager
 * 
 * Cleans up Wi-Fi manager resources
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_deinit(void);

/**
 * @brief Get Wi-Fi Connection Status
 * 
 * @return Current Wi-Fi status
 */
wifi_status_t wifi_manager_get_status(void);

/**
 * @brief Get Wi-Fi Statistics
 * 
 * @param stats Pointer to statistics structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_get_stats(wifi_stats_t *stats);

/**
 * @brief Check if Wi-Fi is Connected
 * 
 * @return true if connected with IP address, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get Current IP Address
 * 
 * @return IP address as uint32_t (0 if not connected)
 */
uint32_t wifi_manager_get_ip_address(void);

/**
 * @brief Get Current IP Address as String
 * 
 * @param ip_str Buffer to store IP string (minimum 16 bytes)
 * @param max_len Maximum buffer length
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_get_ip_string(char *ip_str, size_t max_len);

/**
 * @brief Force Reconnection
 * 
 * Forces a reconnection attempt even if currently connected
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_reconnect(void);

/**
 * @brief Reset Wi-Fi Statistics
 * 
 * Resets all counters and statistics to zero
 */
void wifi_manager_reset_stats(void);

/**
 * @brief Set Power Save Mode
 * 
 * @param ps_mode Power save mode
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_set_power_save(wifi_ps_type_t ps_mode);

/**
 * @brief Get Wi-Fi Configuration Info
 * 
 * Returns information about the configured Wi-Fi network
 * 
 * @param ssid Buffer for SSID (minimum 33 bytes)
 * @param ssid_len Maximum SSID buffer length
 * @param channel Pointer to store channel number
 * @param auth_mode Pointer to store authentication mode
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_get_config_info(char *ssid, size_t ssid_len, uint8_t *channel, wifi_auth_mode_t *auth_mode);

/**
 * @brief Perform Wi-Fi Scan
 * 
 * Performs a scan for available access points
 * 
 * @param ap_records Buffer for AP records
 * @param max_records Maximum number of records to return
 * @param actual_records Pointer to store actual number of records found
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t max_records, uint16_t *actual_records);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H