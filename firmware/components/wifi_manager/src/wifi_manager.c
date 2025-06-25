/**
 * @file wifi_manager.c
 * @brief ESP32-C6 Wi-Fi Manager Implementation
 * 
 * This component provides comprehensive Wi-Fi management for ESP32-C6
 * with hardcoded credentials loaded at build time from wifi-creds.txt
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"

static const char *TAG = "wifi_manager";

// Hardcoded Wi-Fi credentials (loaded from wifi-creds.txt at build time)
#define WIFI_SSID "FBI Surveillance Van"
#define WIFI_PASSWORD "jerjushanben2135"

// Event group bits
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT          BIT1
#define WIFI_SCANNING_BIT      BIT2

// Internal state structure
typedef struct {
    bool initialized;
    bool started;
    wifi_manager_config_t config;
    wifi_manager_event_cb_t event_callback;
    wifi_status_t current_status;
    wifi_stats_t stats;
    uint32_t current_ip;
    esp_netif_t *sta_netif;
    EventGroupHandle_t wifi_event_group;
    esp_timer_handle_t uptime_timer;
    esp_timer_handle_t retry_timer;
    uint32_t retry_count;
    int64_t connection_start_time;
} wifi_manager_state_t;

static wifi_manager_state_t s_wifi_state = {0};

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void uptime_timer_callback(void* arg);
static void retry_timer_callback(void* arg);
static void set_status(wifi_status_t new_status);
static esp_err_t start_connection_attempt(void);

/**
 * @brief Set Wi-Fi status and notify callback
 */
static void set_status(wifi_status_t new_status)
{
    if (s_wifi_state.current_status != new_status) {
        wifi_status_t old_status = s_wifi_state.current_status;
        s_wifi_state.current_status = new_status;
        
        ESP_LOGI(TAG, "Wi-Fi status changed: %d -> %d", (int)old_status, (int)new_status);
        
        // Update statistics
        switch (new_status) {
            case WIFI_STATUS_CONNECTING:
                s_wifi_state.stats.connection_attempts++;
                s_wifi_state.connection_start_time = esp_timer_get_time();
                break;
            case WIFI_STATUS_CONNECTED:
                s_wifi_state.stats.successful_connections++;
                if (s_wifi_state.uptime_timer) {
                    esp_timer_start_periodic(s_wifi_state.uptime_timer, 1000000); // 1 second
                }
                break;
            case WIFI_STATUS_FAILED:
                s_wifi_state.stats.failed_connections++;
                break;
            case WIFI_STATUS_DISCONNECTED:
                if (old_status == WIFI_STATUS_CONNECTED) {
                    s_wifi_state.stats.disconnections++;
                    if (s_wifi_state.uptime_timer) {
                        esp_timer_stop(s_wifi_state.uptime_timer);
                    }
                }
                break;
            case WIFI_STATUS_RECONNECTING:
                s_wifi_state.stats.reconnections++;
                break;
        }
        
        // Notify callback
        if (s_wifi_state.event_callback) {
            s_wifi_state.event_callback(new_status, s_wifi_state.current_ip);
        }
    }
}

/**
 * @brief Wi-Fi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "Wi-Fi station started");
            start_connection_attempt();
            break;
            
        case WIFI_EVENT_STA_CONNECTED:
            {
                wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*) event_data;
                ESP_LOGI(TAG, "Connected to AP SSID:%s channel:%d authmode:%d",
                         event->ssid, event->channel, event->authmode);
                
                s_wifi_state.stats.channel = event->channel;
                s_wifi_state.stats.auth_mode = event->authmode;
                s_wifi_state.retry_count = 0;
                
                // Stop retry timer if running
                if (s_wifi_state.retry_timer) {
                    esp_timer_stop(s_wifi_state.retry_timer);
                }
            }
            break;
            
        case WIFI_EVENT_STA_DISCONNECTED:
            {
                wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
                ESP_LOGW(TAG, "Disconnected from AP. Reason: %d", (int)event->reason);
                
                s_wifi_state.current_ip = 0;
                xEventGroupClearBits(s_wifi_state.wifi_event_group, WIFI_CONNECTED_BIT);
                
                if (s_wifi_state.current_status == WIFI_STATUS_CONNECTED) {
                    set_status(WIFI_STATUS_DISCONNECTED);
                }
                
                // Handle reconnection
                if (s_wifi_state.config.auto_reconnect && s_wifi_state.started) {
                    if (s_wifi_state.retry_count < s_wifi_state.config.max_retry_attempts) {
                        ESP_LOGI(TAG, "Scheduling reconnection attempt %"PRIu32"/%"PRIu32, 
                                s_wifi_state.retry_count + 1, s_wifi_state.config.max_retry_attempts);
                        set_status(WIFI_STATUS_RECONNECTING);
                        
                        // Start retry timer
                        if (s_wifi_state.retry_timer) {
                            esp_timer_start_once(s_wifi_state.retry_timer, s_wifi_state.config.retry_delay_ms * 1000);
                        }
                    } else {
                        ESP_LOGE(TAG, "Maximum retry attempts reached. Connection failed.");
                        set_status(WIFI_STATUS_FAILED);
                        xEventGroupSetBits(s_wifi_state.wifi_event_group, WIFI_FAIL_BIT);
                    }
                }
            }
            break;
            
        case WIFI_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "Wi-Fi scan completed");
            xEventGroupClearBits(s_wifi_state.wifi_event_group, WIFI_SCANNING_BIT);
            break;
            
        default:
            break;
    }
}

/**
 * @brief IP event handler
 */
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                s_wifi_state.current_ip = event->ip_info.ip.addr;
                
                char ip_str[16];
                esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
                ESP_LOGI(TAG, "Got IP address: %s", ip_str);
                
                xEventGroupSetBits(s_wifi_state.wifi_event_group, WIFI_CONNECTED_BIT);
                set_status(WIFI_STATUS_CONNECTED);
            }
            break;
            
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGW(TAG, "Lost IP address");
            s_wifi_state.current_ip = 0;
            xEventGroupClearBits(s_wifi_state.wifi_event_group, WIFI_CONNECTED_BIT);
            break;
            
        default:
            break;
    }
}

/**
 * @brief Uptime timer callback
 */
static void uptime_timer_callback(void* arg)
{
    if (s_wifi_state.current_status == WIFI_STATUS_CONNECTED) {
        s_wifi_state.stats.uptime_seconds++;
        
        // Update RSSI every 10 seconds
        if (s_wifi_state.stats.uptime_seconds % 10 == 0) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                s_wifi_state.stats.rssi = ap_info.rssi;
            }
        }
    }
}

/**
 * @brief Retry timer callback
 */
static void retry_timer_callback(void* arg)
{
    s_wifi_state.retry_count++;
    ESP_LOGI(TAG, "Retry attempt %"PRIu32"/%"PRIu32, s_wifi_state.retry_count, s_wifi_state.config.max_retry_attempts);
    start_connection_attempt();
}

/**
 * @brief Start connection attempt
 */
static esp_err_t start_connection_attempt(void)
{
    set_status(WIFI_STATUS_CONNECTING);
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start connection: %s", esp_err_to_name(ret));
        set_status(WIFI_STATUS_FAILED);
    }
    return ret;
}

/**
 * @brief Initialize Wi-Fi Manager
 */
esp_err_t wifi_manager_init(const wifi_manager_config_t *config, wifi_manager_event_cb_t event_cb)
{
    if (s_wifi_state.initialized) {
        ESP_LOGW(TAG, "Wi-Fi manager already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing Wi-Fi manager with SSID: %s", WIFI_SSID);
    
    // Initialize state
    memset(&s_wifi_state, 0, sizeof(s_wifi_state));
    s_wifi_state.config = config ? *config : (wifi_manager_config_t)WIFI_MANAGER_CONFIG_DEFAULT();
    s_wifi_state.event_callback = event_cb;
    s_wifi_state.current_status = WIFI_STATUS_DISCONNECTED;
    
    // Create event group
    s_wifi_state.wifi_event_group = xEventGroupCreate();
    if (!s_wifi_state.wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create default Wi-Fi STA interface
    s_wifi_state.sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_wifi_state.sta_netif) {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi STA interface");
        return ESP_FAIL;
    }
    
    // Initialize Wi-Fi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_LOST_IP,
                                                        &ip_event_handler,
                                                        NULL,
                                                        NULL));
    
    // Configure Wi-Fi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Set power save mode
    ESP_ERROR_CHECK(esp_wifi_set_ps(s_wifi_state.config.power_save_mode));
    
    // Create timers
    esp_timer_create_args_t uptime_timer_args = {
        .callback = &uptime_timer_callback,
        .arg = NULL,
        .name = "wifi_uptime"
    };
    ESP_ERROR_CHECK(esp_timer_create(&uptime_timer_args, &s_wifi_state.uptime_timer));
    
    esp_timer_create_args_t retry_timer_args = {
        .callback = &retry_timer_callback,
        .arg = NULL,
        .name = "wifi_retry"
    };
    ESP_ERROR_CHECK(esp_timer_create(&retry_timer_args, &s_wifi_state.retry_timer));
    
    s_wifi_state.initialized = true;
    ESP_LOGI(TAG, "Wi-Fi manager initialized successfully");
    
    return ESP_OK;
}

/**
 * @brief Start Wi-Fi Manager
 */
esp_err_t wifi_manager_start(void)
{
    if (!s_wifi_state.initialized) {
        ESP_LOGE(TAG, "Wi-Fi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_wifi_state.started) {
        ESP_LOGW(TAG, "Wi-Fi manager already started");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting Wi-Fi manager");
    
    esp_err_t ret = esp_wifi_start();
    if (ret == ESP_OK) {
        s_wifi_state.started = true;
        s_wifi_state.retry_count = 0;
    } else {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief Stop Wi-Fi Manager
 */
esp_err_t wifi_manager_stop(void)
{
    if (!s_wifi_state.initialized) {
        ESP_LOGE(TAG, "Wi-Fi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!s_wifi_state.started) {
        ESP_LOGW(TAG, "Wi-Fi manager not started");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping Wi-Fi manager");
    
    // Stop timers
    if (s_wifi_state.uptime_timer) {
        esp_timer_stop(s_wifi_state.uptime_timer);
    }
    if (s_wifi_state.retry_timer) {
        esp_timer_stop(s_wifi_state.retry_timer);
    }
    
    esp_err_t ret = esp_wifi_stop();
    if (ret == ESP_OK) {
        s_wifi_state.started = false;
        s_wifi_state.current_ip = 0;
        set_status(WIFI_STATUS_DISCONNECTED);
    } else {
        ESP_LOGE(TAG, "Failed to stop Wi-Fi: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

/**
 * @brief Deinitialize Wi-Fi Manager
 */
esp_err_t wifi_manager_deinit(void)
{
    if (!s_wifi_state.initialized) {
        ESP_LOGW(TAG, "Wi-Fi manager not initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing Wi-Fi manager");
    
    // Stop if running
    if (s_wifi_state.started) {
        wifi_manager_stop();
    }
    
    // Delete timers
    if (s_wifi_state.uptime_timer) {
        esp_timer_delete(s_wifi_state.uptime_timer);
        s_wifi_state.uptime_timer = NULL;
    }
    if (s_wifi_state.retry_timer) {
        esp_timer_delete(s_wifi_state.retry_timer);
        s_wifi_state.retry_timer = NULL;
    }
    
    // Unregister event handlers
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_LOST_IP, &ip_event_handler);
    
    // Deinitialize Wi-Fi
    esp_wifi_deinit();
    
    // Destroy event group
    if (s_wifi_state.wifi_event_group) {
        vEventGroupDelete(s_wifi_state.wifi_event_group);
        s_wifi_state.wifi_event_group = NULL;
    }
    
    // Clean up netif
    if (s_wifi_state.sta_netif) {
        esp_netif_destroy_default_wifi(s_wifi_state.sta_netif);
        s_wifi_state.sta_netif = NULL;
    }
    
    s_wifi_state.initialized = false;
    ESP_LOGI(TAG, "Wi-Fi manager deinitialized");
    
    return ESP_OK;
}

/**
 * @brief Get Wi-Fi Connection Status
 */
wifi_status_t wifi_manager_get_status(void)
{
    return s_wifi_state.current_status;
}

/**
 * @brief Get Wi-Fi Statistics
 */
esp_err_t wifi_manager_get_stats(wifi_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *stats = s_wifi_state.stats;
    return ESP_OK;
}

/**
 * @brief Check if Wi-Fi is Connected
 */
bool wifi_manager_is_connected(void)
{
    return (s_wifi_state.current_status == WIFI_STATUS_CONNECTED) && (s_wifi_state.current_ip != 0);
}

/**
 * @brief Get Current IP Address
 */
uint32_t wifi_manager_get_ip_address(void)
{
    return s_wifi_state.current_ip;
}

/**
 * @brief Get Current IP Address as String
 */
esp_err_t wifi_manager_get_ip_string(char *ip_str, size_t max_len)
{
    if (!ip_str || max_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_wifi_state.current_ip == 0) {
        strncpy(ip_str, "0.0.0.0", max_len);
    } else {
        esp_ip4_addr_t ip_addr = { .addr = s_wifi_state.current_ip };
        esp_ip4addr_ntoa(&ip_addr, ip_str, max_len);
    }
    
    return ESP_OK;
}

/**
 * @brief Force Reconnection
 */
esp_err_t wifi_manager_reconnect(void)
{
    if (!s_wifi_state.initialized || !s_wifi_state.started) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Forcing reconnection");
    s_wifi_state.retry_count = 0;
    
    // Disconnect first if connected
    if (s_wifi_state.current_status == WIFI_STATUS_CONNECTED) {
        esp_wifi_disconnect();
    } else {
        start_connection_attempt();
    }
    
    return ESP_OK;
}

/**
 * @brief Reset Wi-Fi Statistics
 */
void wifi_manager_reset_stats(void)
{
    memset(&s_wifi_state.stats, 0, sizeof(wifi_stats_t));
    ESP_LOGI(TAG, "Wi-Fi statistics reset");
}

/**
 * @brief Set Power Save Mode
 */
esp_err_t wifi_manager_set_power_save(wifi_ps_type_t ps_mode)
{
    s_wifi_state.config.power_save_mode = ps_mode;
    return esp_wifi_set_ps(ps_mode);
}

/**
 * @brief Get Wi-Fi Configuration Info
 */
esp_err_t wifi_manager_get_config_info(char *ssid, size_t ssid_len, uint8_t *channel, wifi_auth_mode_t *auth_mode)
{
    if (!ssid || ssid_len < 33) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(ssid, WIFI_SSID, ssid_len);
    
    if (channel) {
        *channel = s_wifi_state.stats.channel;
    }
    
    if (auth_mode) {
        *auth_mode = s_wifi_state.stats.auth_mode;
    }
    
    return ESP_OK;
}

/**
 * @brief Perform Wi-Fi Scan
 */
esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t max_records, uint16_t *actual_records)
{
    if (!ap_records || !actual_records) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_wifi_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting Wi-Fi scan");
    
    // Set scanning bit
    xEventGroupSetBits(s_wifi_state.wifi_event_group, WIFI_SCANNING_BIT);
    
    // Start scan
    esp_err_t ret = esp_wifi_scan_start(NULL, true);
    if (ret != ESP_OK) {
        xEventGroupClearBits(s_wifi_state.wifi_event_group, WIFI_SCANNING_BIT);
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Get scan results
    ret = esp_wifi_scan_get_ap_records(&max_records, ap_records);
    if (ret == ESP_OK) {
        *actual_records = max_records;
        ESP_LOGI(TAG, "Scan completed, found %d access points", (int)max_records);
    } else {
        *actual_records = 0;
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
    }
    
    xEventGroupClearBits(s_wifi_state.wifi_event_group, WIFI_SCANNING_BIT);
    return ret;
}