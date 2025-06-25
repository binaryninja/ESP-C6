/**
 * @file mcp_server_simple.h
 * @brief Simple MCP (Model Context Protocol) Server for ESP32-C6
 * 
 * This header provides a simplified MCP server implementation in C
 * for ESP32-C6, focusing on basic JSON-RPC functionality without
 * complex transport layers.
 * 
 * Features:
 * - Basic JSON-RPC 2.0 protocol support
 * - Simple communication interface
 * - ESP32-specific tools (echo, display, GPIO, system)
 * - FreeRTOS task integration
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

/* MCP Server Configuration */
#define MCP_SERVER_NAME             "esp32-c6-mcp"
#define MCP_SERVER_VERSION          "1.0.0"
#define MCP_PROTOCOL_VERSION        "2024-11-05"

/* Task Configuration */
#define MCP_SERVER_TASK_PRIORITY    5
#ifndef MCP_SERVER_TASK_STACK_SIZE
#define MCP_SERVER_TASK_STACK_SIZE  4096
#endif

/* Message Configuration */
#ifndef MCP_MAX_MESSAGE_SIZE
#define MCP_MAX_MESSAGE_SIZE        1024
#endif
#ifndef MCP_MAX_TOOLS
#define MCP_MAX_TOOLS               8
#endif
#define MCP_RESPONSE_TIMEOUT_MS     5000

/* MCP Server Handle */
typedef struct mcp_server_simple* mcp_server_handle_t;

/* MCP Tool Types */
typedef enum {
    MCP_TOOL_ECHO = 0,
    MCP_TOOL_DISPLAY,
    MCP_TOOL_GPIO,
    MCP_TOOL_SYSTEM,
    MCP_TOOL_MAX
} mcp_tool_type_t;

/* MCP Message Types */
typedef enum {
    MCP_MSG_REQUEST = 0,
    MCP_MSG_RESPONSE,
    MCP_MSG_NOTIFICATION,
    MCP_MSG_ERROR
} mcp_message_type_t;

/* MCP Server Configuration */
typedef struct {
    const char* server_name;
    const char* server_version;
    const char* protocol_version;
    uint32_t task_stack_size;
    UBaseType_t task_priority;
    uint32_t max_message_size;
    bool enable_echo_tool;
    bool enable_display_tool;
    bool enable_gpio_tool;
    bool enable_system_tool;
} mcp_server_config_t;

/* MCP Tool Definition */
typedef struct {
    const char* name;
    const char* description;
    mcp_tool_type_t type;
    esp_err_t (*execute)(const char* params_json, char* result_json, size_t result_size);
} mcp_tool_def_t;

/* MCP Message Structure */
typedef struct {
    mcp_message_type_t type;
    uint32_t id;
    const char* method;
    const char* params_json;
    char* result_json;
    size_t result_size;
    esp_err_t error_code;
} mcp_message_t;

/* MCP Server Statistics */
typedef struct {
    uint32_t messages_received;
    uint32_t messages_sent;
    uint32_t requests_processed;
    uint32_t errors_count;
    uint32_t tools_executed;
    uint64_t uptime_ms;
} mcp_server_stats_t;

/**
 * @brief Get default MCP server configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @return ESP_OK on success
 */
esp_err_t mcp_server_get_default_config(mcp_server_config_t* config);

/**
 * @brief Initialize MCP server
 * 
 * @param config Server configuration
 * @param server_handle Pointer to store server handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_server_init(const mcp_server_config_t* config, 
                          mcp_server_handle_t* server_handle);

/**
 * @brief Start MCP server
 * 
 * Creates task and begins processing MCP messages
 * 
 * @param server_handle Server handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_server_start(mcp_server_handle_t server_handle);

/**
 * @brief Stop MCP server
 * 
 * @param server_handle Server handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_server_stop(mcp_server_handle_t server_handle);

/**
 * @brief Deinitialize MCP server
 * 
 * @param server_handle Server handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_server_deinit(mcp_server_handle_t server_handle);

/**
 * @brief Get server statistics
 * 
 * @param server_handle Server handle
 * @param stats Pointer to store statistics
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_server_get_stats(mcp_server_handle_t server_handle,
                               mcp_server_stats_t* stats);

/**
 * @brief Check if server is running
 * 
 * @param server_handle Server handle
 * @return true if running, false otherwise
 */
bool mcp_server_is_running(mcp_server_handle_t server_handle);

/**
 * @brief Process a single line of input
 * 
 * This function can be called from the main firmware to process
 * JSON-RPC messages received from various sources
 * 
 * @param server_handle Server handle
 * @param input_line Input line to process
 * @param output_buffer Buffer for response
 * @param output_size Size of output buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_server_process_line(mcp_server_handle_t server_handle,
                                  const char* input_line,
                                  char* output_buffer,
                                  size_t output_size);

/* Built-in Tool Functions */

/**
 * @brief Echo tool - responds with the input parameters
 * 
 * @param params_json Tool parameters as JSON
 * @param result_json Buffer for result JSON
 * @param result_size Size of result buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_echo_execute(const char* params_json, char* result_json, size_t result_size);

/**
 * @brief Display tool - controls ST7789 display
 * 
 * @param params_json Tool parameters as JSON
 * @param result_json Buffer for result JSON
 * @param result_size Size of result buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_display_execute(const char* params_json, char* result_json, size_t result_size);

/**
 * @brief GPIO tool - controls LED and reads button
 * 
 * @param params_json Tool parameters as JSON
 * @param result_json Buffer for result JSON
 * @param result_size Size of result buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_gpio_execute(const char* params_json, char* result_json, size_t result_size);

/**
 * @brief System tool - provides system information
 * 
 * @param params_json Tool parameters as JSON
 * @param result_json Buffer for result JSON
 * @param result_size Size of result buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tool_system_execute(const char* params_json, char* result_json, size_t result_size);

#ifdef __cplusplus
}
#endif