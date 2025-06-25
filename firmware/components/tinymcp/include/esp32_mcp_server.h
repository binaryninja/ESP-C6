/**
 * @file esp32_mcp_server.h
 * @brief ESP32-C6 MCP (Model Context Protocol) Server Implementation
 * 
 * This header provides the main MCP server interface for ESP32-C6,
 * integrating TinyMCP functionality with ESP-IDF and FreeRTOS.
 * 
 * Features:
 * - JSON-RPC 2.0 protocol support
 * - UART/USB CDC transport layer
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
#define MCP_SERVER_TASK_PRIORITY        5
#define MCP_SERVER_TASK_STACK_SIZE      8192
#define MCP_TRANSPORT_TASK_PRIORITY     4
#define MCP_TRANSPORT_TASK_STACK_SIZE   4096

/* Message Configuration */
#define MCP_MAX_MESSAGE_SIZE            2048
#define MCP_MAX_TOOLS                   16
#define MCP_MAX_REQUESTS                8
#define MCP_TRANSPORT_BUFFER_SIZE       1024
#define MCP_RESPONSE_TIMEOUT_MS         5000

/* MCP Server Handle */
typedef struct esp32_mcp_server* esp32_mcp_server_handle_t;

/* MCP Tool Types */
typedef enum {
    MCP_TOOL_TYPE_DISPLAY = 0,
    MCP_TOOL_TYPE_GPIO,
    MCP_TOOL_TYPE_SYSTEM,
    MCP_TOOL_TYPE_STATUS,
    MCP_TOOL_TYPE_MAX
} mcp_tool_type_t;

/* MCP Transport Types */
typedef enum {
    MCP_TRANSPORT_UART = 0,
    MCP_TRANSPORT_USB_CDC,
    MCP_TRANSPORT_WIFI_TCP,
    MCP_TRANSPORT_BLE,
    MCP_TRANSPORT_MAX
} mcp_transport_type_t;

/* MCP Message Types */
typedef enum {
    MCP_MSG_TYPE_REQUEST = 0,
    MCP_MSG_TYPE_RESPONSE,
    MCP_MSG_TYPE_NOTIFICATION,
    MCP_MSG_TYPE_ERROR
} mcp_message_type_t;

/* MCP Server Configuration */
typedef struct {
    /* Basic Configuration */
    const char* server_name;
    const char* server_version;
    const char* protocol_version;
    
    /* Transport Configuration */
    mcp_transport_type_t transport_type;
    uint32_t transport_baudrate;    // For UART transport
    int transport_port;             // For TCP transport
    const char* transport_device;   // Device path for UART
    
    /* Task Configuration */
    uint32_t server_task_stack_size;
    uint32_t transport_task_stack_size;
    UBaseType_t server_task_priority;
    UBaseType_t transport_task_priority;
    
    /* Message Configuration */
    uint32_t max_message_size;
    uint32_t max_concurrent_requests;
    uint32_t response_timeout_ms;
    uint32_t transport_buffer_size;
    
    /* Feature Flags */
    bool enable_display_tool;
    bool enable_gpio_tool;
    bool enable_system_tool;
    bool enable_status_tool;
    
} esp32_mcp_server_config_t;

/* MCP Tool Definition */
typedef struct {
    const char* name;
    const char* description;
    const char* input_schema_json;
    mcp_tool_type_t type;
    esp_err_t (*execute)(const char* params_json, char* result_json, size_t result_size);
} mcp_tool_t;

/* MCP Message Structure */
typedef struct {
    mcp_message_type_t type;
    uint32_t id;
    const char* method;
    const char* params_json;
    char* result_json;
    size_t result_size;
    esp_err_t error_code;
    int64_t timestamp;
} mcp_message_t;

/* MCP Server Statistics */
typedef struct {
    uint32_t messages_received;
    uint32_t messages_sent;
    uint32_t requests_processed;
    uint32_t errors_count;
    uint32_t tools_executed;
    uint64_t uptime_ms;
    uint32_t free_heap;
    uint32_t min_free_heap;
} mcp_server_stats_t;

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
 * @brief Register custom MCP tool
 * 
 * @param server_handle Server handle
 * @param tool Tool definition
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_server_register_tool(esp32_mcp_server_handle_t server_handle,
                                         const mcp_tool_t* tool);

/**
 * @brief Get server statistics
 * 
 * @param server_handle Server handle
 * @param stats Pointer to store statistics
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_server_get_stats(esp32_mcp_server_handle_t server_handle,
                                     mcp_server_stats_t* stats);

/**
 * @brief Send notification to client
 * 
 * @param server_handle Server handle
 * @param method Notification method name
 * @param params_json Parameters as JSON string
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_server_send_notification(esp32_mcp_server_handle_t server_handle,
                                             const char* method,
                                             const char* params_json);

/**
 * @brief Check if server is running
 * 
 * @param server_handle Server handle
 * @return true if running, false otherwise
 */
bool esp32_mcp_server_is_running(esp32_mcp_server_handle_t server_handle);

/**
 * @brief Get server configuration
 * 
 * @param server_handle Server handle
 * @param config Pointer to store configuration
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_server_get_config(esp32_mcp_server_handle_t server_handle,
                                      esp32_mcp_server_config_t* config);

/* MCP Tool Execution Functions - Implemented in separate files */

/**
 * @brief Execute display tool
 * 
 * @param params_json Tool parameters as JSON
 * @param result_json Buffer for result JSON
 * @param result_size Size of result buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_display_execute(const char* params_json, char* result_json, size_t result_size);

/**
 * @brief Execute GPIO tool
 * 
 * @param params_json Tool parameters as JSON
 * @param result_json Buffer for result JSON
 * @param result_size Size of result buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_gpio_execute(const char* params_json, char* result_json, size_t result_size);

/**
 * @brief Execute system tool
 * 
 * @param params_json Tool parameters as JSON
 * @param result_json Buffer for result JSON
 * @param result_size Size of result buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_system_execute(const char* params_json, char* result_json, size_t result_size);

/**
 * @brief Execute status tool
 * 
 * @param params_json Tool parameters as JSON
 * @param result_json Buffer for result JSON
 * @param result_size Size of result buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_status_execute(const char* params_json, char* result_json, size_t result_size);

#ifdef __cplusplus
}
#endif