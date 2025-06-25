/**
 * @file esp32_mcp_server.h
 * @brief ESP32-C6 MCP (Model Context Protocol) Server Implementation
 * 
 * This header provides the main MCP server interface for ESP32-C6,
 * integrating TinyMCP functionality with ESP-IDF and FreeRTOS.
 * 
 * Features:
 * - JSON-RPC 2.0 protocol support
 * - WiFi TCP and BLE transport layers
 * - ESP32-specific MCP tools (Display, GPIO, System)
 * - FreeRTOS task integration
 * - ESP-IDF component compatibility
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

/* MCP Server Configuration */
#define ESP32_MCP_SERVER_NAME           "esp32-c6-mcp-server"
#define ESP32_MCP_SERVER_VERSION        "1.0.0"
#define ESP32_MCP_PROTOCOL_VERSION      "2024-11-05"

/* Task Configuration */
#ifndef MCP_SERVER_TASK_PRIORITY
#define MCP_SERVER_TASK_PRIORITY        5
#endif
#ifndef MCP_SERVER_TASK_STACK_SIZE
#define MCP_SERVER_TASK_STACK_SIZE      8192
#endif
#define MCP_TRANSPORT_TASK_PRIORITY     4
#define MCP_TRANSPORT_TASK_STACK_SIZE   4096

/* Message Configuration */
#ifndef MCP_MAX_MESSAGE_SIZE
#define MCP_MAX_MESSAGE_SIZE            2048
#endif
#ifndef MCP_MAX_TOOLS
#define MCP_MAX_TOOLS                   16
#endif
#define MCP_MAX_REQUESTS                8
#define MCP_TRANSPORT_BUFFER_SIZE       1024
#define MCP_RESPONSE_TIMEOUT_MS         5000

/* MCP Server Handle */
typedef struct esp32_mcp_server* esp32_mcp_server_handle_t;

/* Include transport types */
#include "esp32_transport.h"

/* ESP32 MCP Server Configuration */
typedef struct {
    /* Basic Configuration */
    const char* server_name;
    const char* server_version;
    const char* protocol_version;
    
    /* Transport Configuration */
    mcp_transport_type_t transport_type;
    int transport_port;             // For TCP transport
    const char* transport_device;   // Device path or address
    
    /* Task Configuration */
    uint32_t server_task_stack_size;
    UBaseType_t server_task_priority;
    UBaseType_t transport_task_priority;
    
    /* Message Configuration */
    uint32_t max_message_size;
    uint32_t response_buffer_size;
    uint32_t response_timeout_ms;
    
    /* Tool Feature Flags */
    bool enable_echo_tool;
    bool enable_display_tool;
    bool enable_gpio_tool;
    bool enable_system_tool;
    bool enable_status_tool;
    
} esp32_mcp_server_config_t;

/* ESP32 MCP Server Statistics */
typedef struct {
    uint64_t uptime_ms;
    uint32_t requests_processed;
    uint32_t responses_sent;
    uint32_t errors;
    uint32_t transport_errors;
    uint32_t connection_count;
    uint32_t bytes_received;
    uint32_t bytes_sent;
    bool connected;
} esp32_mcp_server_stats_t;

/**
 * @brief Get default MCP server configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @return ESP_OK on success
 */
esp_err_t esp32_mcp_server_get_default_config(esp32_mcp_server_config_t* config);

/**
 * @brief Initialize MCP server
 * 
 * @param config Server configuration
 * @param server_handle Pointer to store server handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_server_init(const esp32_mcp_server_config_t* config, 
                                esp32_mcp_server_handle_t* server_handle);

/**
 * @brief Start MCP server
 * 
 * Creates tasks and begins listening for MCP messages
 * 
 * @param server_handle Server handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_server_start(esp32_mcp_server_handle_t server_handle);

/**
 * @brief Stop MCP server
 * 
 * @param server_handle Server handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_server_stop(esp32_mcp_server_handle_t server_handle);

/**
 * @brief Deinitialize MCP server
 * 
 * @param server_handle Server handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_server_deinit(esp32_mcp_server_handle_t server_handle);

/**
 * @brief Get server statistics
 * 
 * @param server_handle Server handle
 * @param stats Pointer to store statistics
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_server_get_stats(esp32_mcp_server_handle_t server_handle,
                                     esp32_mcp_server_stats_t* stats);

/**
 * @brief Check if server is running
 * 
 * @param server_handle Server handle
 * @return true if running, false otherwise
 */
bool esp32_mcp_server_is_running(esp32_mcp_server_handle_t server_handle);

#ifdef __cplusplus
}
#endif