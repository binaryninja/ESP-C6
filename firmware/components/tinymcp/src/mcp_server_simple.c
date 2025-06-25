/**
 * @file mcp_server_simple.c
 * @brief Simple MCP (Model Context Protocol) Server Implementation for ESP32-C6
 * 
 * This file implements a simplified MCP server in C for ESP32-C6,
 * providing basic JSON-RPC functionality for remote control.
 */

#include "mcp_server_simple.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"

static const char *TAG = "MCP_SERVER";

/* MCP Server Internal Structure */
struct mcp_server_simple {
    /* Configuration */
    mcp_server_config_t config;
    
    /* State */
    bool initialized;
    bool running;
    int64_t start_time;
    
    /* Tasks */
    TaskHandle_t server_task;
    
    /* Synchronization */
    SemaphoreHandle_t mutex;
    
    /* Tools */
    mcp_tool_def_t tools[MCP_MAX_TOOLS];
    uint32_t tool_count;
    
    /* Statistics */
    mcp_server_stats_t stats;
    
    /* Message handling */
    uint32_t next_message_id;
};

/* Forward declarations */
static void mcp_server_task_function(void* arg);
static esp_err_t mcp_handle_request(struct mcp_server_simple* server, 
                                   const char* request_json, 
                                   char* response_json, 
                                   size_t response_size);
static esp_err_t mcp_register_builtin_tools(struct mcp_server_simple* server);
static esp_err_t mcp_send_response(uint32_t id, const char* result_json, 
                                  const char* error_msg, char* output, size_t output_size);

/* Get default MCP server configuration */
esp_err_t mcp_server_get_default_config(mcp_server_config_t* config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(mcp_server_config_t));
    
    config->server_name = MCP_SERVER_NAME;
    config->server_version = MCP_SERVER_VERSION;
    config->protocol_version = MCP_PROTOCOL_VERSION;
    config->task_stack_size = MCP_SERVER_TASK_STACK_SIZE;
    config->task_priority = MCP_SERVER_TASK_PRIORITY;
    config->max_message_size = MCP_MAX_MESSAGE_SIZE;
    config->enable_echo_tool = true;
    config->enable_display_tool = true;
    config->enable_gpio_tool = true;
    config->enable_system_tool = true;
    
    return ESP_OK;
}

/* Initialize MCP server */
esp_err_t mcp_server_init(const mcp_server_config_t* config, 
                          mcp_server_handle_t* server_handle)
{
    if (!config || !server_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing simple MCP server: %s v%s", 
             config->server_name, config->server_version);
    
    /* Allocate server structure */
    struct mcp_server_simple* server = (struct mcp_server_simple*)heap_caps_calloc(1, 
                                        sizeof(struct mcp_server_simple), MALLOC_CAP_DEFAULT);
    if (!server) {
        ESP_LOGE(TAG, "Failed to allocate server structure");
        return ESP_ERR_NO_MEM;
    }
    
    /* Copy configuration */
    memcpy(&server->config, config, sizeof(mcp_server_config_t));
    
    /* Create synchronization objects */
    server->mutex = xSemaphoreCreateMutex();
    if (!server->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(server);
        return ESP_ERR_NO_MEM;
    }
    
    /* Register built-in tools */
    esp_err_t ret = mcp_register_builtin_tools(server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register built-in tools: %s", esp_err_to_name(ret));
        vSemaphoreDelete(server->mutex);
        free(server);
        return ret;
    }
    
    /* Initialize statistics */
    memset(&server->stats, 0, sizeof(mcp_server_stats_t));
    server->start_time = esp_timer_get_time();
    server->next_message_id = 1;
    
    server->initialized = true;
    *server_handle = server;
    
    ESP_LOGI(TAG, "Simple MCP server initialized successfully");
    return ESP_OK;
}

/* Start MCP server */
esp_err_t mcp_server_start(mcp_server_handle_t server_handle)
{
    if (!server_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct mcp_server_simple* server = (struct mcp_server_simple*)server_handle;
    
    if (!server->initialized) {
        ESP_LOGE(TAG, "Server not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (server->running) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting simple MCP server");
    
    /* Create server task */
    BaseType_t task_ret = xTaskCreate(mcp_server_task_function, 
                                     "mcp_server", 
                                     server->config.task_stack_size / sizeof(StackType_t),
                                     server, 
                                     server->config.task_priority, 
                                     &server->server_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create server task");
        return ESP_ERR_NO_MEM;
    }
    
    server->running = true;
    ESP_LOGI(TAG, "Simple MCP server started successfully");
    ESP_LOGI(TAG, "Available tools: echo, display_control, gpio_control, system_info");
    
    return ESP_OK;
}

/* Stop MCP server */
esp_err_t mcp_server_stop(mcp_server_handle_t server_handle)
{
    if (!server_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct mcp_server_simple* server = (struct mcp_server_simple*)server_handle;
    
    if (!server->running) {
        ESP_LOGW(TAG, "Server not running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping simple MCP server");
    
    server->running = false;
    
    /* Delete task */
    if (server->server_task) {
        vTaskDelete(server->server_task);
        server->server_task = NULL;
    }
    
    ESP_LOGI(TAG, "Simple MCP server stopped");
    return ESP_OK;
}

/* Deinitialize MCP server */
esp_err_t mcp_server_deinit(mcp_server_handle_t server_handle)
{
    if (!server_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct mcp_server_simple* server = (struct mcp_server_simple*)server_handle;
    
    /* Stop server if running */
    if (server->running) {
        mcp_server_stop(server_handle);
    }
    
    ESP_LOGI(TAG, "Deinitializing simple MCP server");
    
    /* Cleanup synchronization objects */
    if (server->mutex) {
        vSemaphoreDelete(server->mutex);
    }
    
    /* Free server structure */
    free(server);
    
    ESP_LOGI(TAG, "Simple MCP server deinitialized");
    return ESP_OK;
}

/* Get server statistics */
esp_err_t mcp_server_get_stats(mcp_server_handle_t server_handle,
                               mcp_server_stats_t* stats)
{
    if (!server_handle || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct mcp_server_simple* server = (struct mcp_server_simple*)server_handle;
    
    if (xSemaphoreTake(server->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &server->stats, sizeof(mcp_server_stats_t));
        stats->uptime_ms = (esp_timer_get_time() - server->start_time) / 1000;
        xSemaphoreGive(server->mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

/* Check if server is running */
bool mcp_server_is_running(mcp_server_handle_t server_handle)
{
    if (!server_handle) {
        return false;
    }
    
    struct mcp_server_simple* server = (struct mcp_server_simple*)server_handle;
    return server->running;
}

/* Process a single line of input */
esp_err_t mcp_server_process_line(mcp_server_handle_t server_handle,
                                  const char* input_line,
                                  char* output_buffer,
                                  size_t output_size)
{
    if (!server_handle || !input_line || !output_buffer || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct mcp_server_simple* server = (struct mcp_server_simple*)server_handle;
    
    if (!server->running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "Processing line: %s", input_line);
    
    /* Update statistics */
    if (xSemaphoreTake(server->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        server->stats.messages_received++;
        xSemaphoreGive(server->mutex);
    }
    
    /* Handle the request */
    esp_err_t ret = mcp_handle_request(server, input_line, output_buffer, output_size);
    
    if (ret == ESP_OK) {
        if (xSemaphoreTake(server->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            server->stats.messages_sent++;
            server->stats.requests_processed++;
            xSemaphoreGive(server->mutex);
        }
    } else {
        if (xSemaphoreTake(server->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            server->stats.errors_count++;
            xSemaphoreGive(server->mutex);
        }
    }
    
    return ret;
}

/* Register built-in tools */
static esp_err_t mcp_register_builtin_tools(struct mcp_server_simple* server)
{
    server->tool_count = 0;
    
    /* Register echo tool */
    if (server->config.enable_echo_tool) {
        mcp_tool_def_t* tool = &server->tools[server->tool_count++];
        tool->name = "echo";
        tool->description = "Echo back the input parameters";
        tool->type = MCP_TOOL_ECHO;
        tool->execute = mcp_tool_echo_execute;
    }
    
    /* Register display tool */
    if (server->config.enable_display_tool) {
        mcp_tool_def_t* tool = &server->tools[server->tool_count++];
        tool->name = "display_control";
        tool->description = "Control ST7789 display";
        tool->type = MCP_TOOL_DISPLAY;
        tool->execute = mcp_tool_display_execute;
    }
    
    /* Register GPIO tool */
    if (server->config.enable_gpio_tool) {
        mcp_tool_def_t* tool = &server->tools[server->tool_count++];
        tool->name = "gpio_control";
        tool->description = "Control GPIO pins";
        tool->type = MCP_TOOL_GPIO;
        tool->execute = mcp_tool_gpio_execute;
    }
    
    /* Register system tool */
    if (server->config.enable_system_tool) {
        mcp_tool_def_t* tool = &server->tools[server->tool_count++];
        tool->name = "system_info";
        tool->description = "Get system information";
        tool->type = MCP_TOOL_SYSTEM;
        tool->execute = mcp_tool_system_execute;
    }
    
    ESP_LOGI(TAG, "Registered %"PRIu32" built-in tools", server->tool_count);
    return ESP_OK;
}

/* Server main task */
static void mcp_server_task_function(void* arg)
{
    struct mcp_server_simple* server = (struct mcp_server_simple*)arg;
    
    ESP_LOGI(TAG, "Simple MCP server task started");
    
    while (server->running) {
        /* Simple task that just updates statistics */
        if (xSemaphoreTake(server->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            server->stats.uptime_ms = (esp_timer_get_time() - server->start_time) / 1000;
            xSemaphoreGive(server->mutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "Simple MCP server task stopped");
    vTaskDelete(NULL);
}

/* Handle incoming request */
static esp_err_t mcp_handle_request(struct mcp_server_simple* server, 
                                   const char* request_json, 
                                   char* response_json, 
                                   size_t response_size)
{
    cJSON* json = cJSON_Parse(request_json);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON request");
        return mcp_send_response(0, NULL, "Parse error", response_json, response_size);
    }
    
    cJSON* method = cJSON_GetObjectItem(json, "method");
    cJSON* id = cJSON_GetObjectItem(json, "id");
    cJSON* params = cJSON_GetObjectItem(json, "params");
    
    if (!method || !cJSON_IsString(method)) {
        cJSON_Delete(json);
        return mcp_send_response(0, NULL, "Missing method", response_json, response_size);
    }
    
    const char* method_str = cJSON_GetStringValue(method);
    uint32_t request_id = id ? (uint32_t)cJSON_GetNumberValue(id) : 0;
    
    ESP_LOGI(TAG, "Handling method: %s, id: %"PRIu32, method_str, request_id);
    
    /* Handle different methods */
    if (strcmp(method_str, "tools/list") == 0) {
        /* List available tools */
        cJSON* tools_array = cJSON_CreateArray();
        for (uint32_t i = 0; i < server->tool_count; i++) {
            cJSON* tool_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(tool_obj, "name", server->tools[i].name);
            cJSON_AddStringToObject(tool_obj, "description", server->tools[i].description);
            cJSON_AddItemToArray(tools_array, tool_obj);
        }
        
        cJSON* result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "tools", tools_array);
        
        char* result_str = cJSON_Print(result);
        esp_err_t ret = mcp_send_response(request_id, result_str, NULL, response_json, response_size);
        
        free(result_str);
        cJSON_Delete(result);
        cJSON_Delete(json);
        return ret;
        
    } else if (strcmp(method_str, "tools/call") == 0) {
        /* Call tool */
        cJSON* name = cJSON_GetObjectItem(params, "name");
        cJSON* arguments = cJSON_GetObjectItem(params, "arguments");
        
        if (!name || !cJSON_IsString(name)) {
            cJSON_Delete(json);
            return mcp_send_response(request_id, NULL, "Missing tool name", response_json, response_size);
        }
        
        const char* tool_name = cJSON_GetStringValue(name);
        char* args_str = arguments ? cJSON_Print(arguments) : strdup("{}");
        
        /* Find and execute tool */
        bool tool_found = false;
        for (uint32_t i = 0; i < server->tool_count; i++) {
            if (strcmp(server->tools[i].name, tool_name) == 0) {
                char result_buffer[512];
                esp_err_t ret = server->tools[i].execute(args_str, result_buffer, sizeof(result_buffer));
                
                if (ret == ESP_OK) {
                    esp_err_t send_ret = mcp_send_response(request_id, result_buffer, NULL, response_json, response_size);
                    if (xSemaphoreTake(server->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        server->stats.tools_executed++;
                        xSemaphoreGive(server->mutex);
                    }
                    free(args_str);
                    cJSON_Delete(json);
                    return send_ret;
                } else {
                    free(args_str);
                    cJSON_Delete(json);
                    return mcp_send_response(request_id, NULL, "Tool execution failed", response_json, response_size);
                }
            }
        }
        
        free(args_str);
        cJSON_Delete(json);
        return mcp_send_response(request_id, NULL, "Tool not found", response_json, response_size);
        
    } else {
        /* Unknown method */
        cJSON_Delete(json);
        return mcp_send_response(request_id, NULL, "Unknown method", response_json, response_size);
    }
}

/* Send response */
static esp_err_t mcp_send_response(uint32_t id, const char* result_json, 
                                  const char* error_msg, char* output, size_t output_size)
{
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(response, "id", id);
    
    if (error_msg) {
        cJSON* error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", -32000);
        cJSON_AddStringToObject(error, "message", error_msg);
        cJSON_AddItemToObject(response, "error", error);
    } else if (result_json) {
        cJSON* result = cJSON_Parse(result_json);
        if (result) {
            cJSON_AddItemToObject(response, "result", result);
        } else {
            cJSON_AddStringToObject(response, "result", result_json);
        }
    } else {
        cJSON_AddNullToObject(response, "result");
    }
    
    char* response_str = cJSON_Print(response);
    if (response_str) {
        if (strlen(response_str) < output_size) {
            strcpy(output, response_str);
            free(response_str);
            cJSON_Delete(response);
            return ESP_OK;
        }
        free(response_str);
    }
    
    cJSON_Delete(response);
    return ESP_ERR_INVALID_SIZE;
}