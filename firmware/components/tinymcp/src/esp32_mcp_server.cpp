/**
 * @file esp32_mcp_server.cpp
 * @brief ESP32-C6 MCP (Model Context Protocol) Server Implementation
 * 
 * This file implements the main MCP server functionality for ESP32-C6,
 * providing JSON-RPC 2.0 protocol support with ESP-IDF integration.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "cJSON.h"

#include "esp32_mcp_server.h"
#include "mcp_tools.h"

static const char *TAG = "MCP_SERVER";

/* MCP Server Internal Structure */
typedef struct esp32_mcp_server {
    /* Configuration */
    esp32_mcp_server_config_t config;
    
    /* State */
    bool initialized;
    bool running;
    int64_t start_time;
    
    /* Transport */
    esp32_mcp_transport_handle_t transport;
    
    /* Tasks */
    TaskHandle_t server_task;
    TaskHandle_t message_task;
    
    /* Synchronization */
    SemaphoreHandle_t mutex;
    QueueHandle_t message_queue;
    
    /* Tools */
    mcp_tool_t tools[MCP_MAX_TOOLS];
    uint32_t tool_count;
    
    /* Statistics */
    mcp_server_stats_t stats;
    
    /* Message handling */
    uint32_t next_message_id;
    
} esp32_mcp_server_t;

/* Forward declarations */
static void mcp_server_task(void* arg);
static void mcp_message_task(void* arg);
static esp_err_t mcp_handle_message(esp32_mcp_server_t* server, const char* message_json);
static esp_err_t mcp_send_response(esp32_mcp_server_t* server, uint32_t id, 
                                  const char* result_json, const char* error_msg);
static esp_err_t mcp_register_builtin_tools(esp32_mcp_server_t* server);

/* Default configuration */
esp_err_t esp32_mcp_server_get_default_config(esp32_mcp_server_config_t* config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(esp32_mcp_server_config_t));
    
    /* Basic configuration */
    config->server_name = ESP32_MCP_SERVER_NAME;
    config->server_version = ESP32_MCP_SERVER_VERSION;
    config->protocol_version = ESP32_MCP_PROTOCOL_VERSION;
    
    /* Transport configuration */
    config->transport_type = (mcp_transport_type_t)0; // MCP_TRANSPORT_UART
    config->transport_baudrate = 115200;
    config->transport_port = 80;
    config->transport_device = "/dev/ttyUSB0";
    
    /* Task configuration */
    config->server_task_stack_size = MCP_SERVER_TASK_STACK_SIZE;
    config->transport_task_stack_size = MCP_TRANSPORT_TASK_STACK_SIZE;
    config->server_task_priority = MCP_SERVER_TASK_PRIORITY;
    config->transport_task_priority = MCP_TRANSPORT_TASK_PRIORITY;
    
    /* Message configuration */
    config->max_message_size = MCP_MAX_MESSAGE_SIZE;
    config->max_concurrent_requests = MCP_MAX_REQUESTS;
    config->response_timeout_ms = MCP_RESPONSE_TIMEOUT_MS;
    config->transport_buffer_size = MCP_TRANSPORT_BUFFER_SIZE;
    
    /* Feature flags */
    config->enable_display_tool = true;
    config->enable_gpio_tool = true;
    config->enable_system_tool = true;
    config->enable_status_tool = true;
    
    return ESP_OK;
}

/* Initialize MCP server */
esp_err_t esp32_mcp_server_init(const esp32_mcp_server_config_t* config, 
                                esp32_mcp_server_handle_t* server_handle)
{
    if (!config || !server_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing MCP server: %s v%s", 
             config->server_name, config->server_version);
    
    /* Allocate server structure */
    esp32_mcp_server_t* server = (esp32_mcp_server_t*)heap_caps_calloc(1, 
                                   sizeof(esp32_mcp_server_t), MALLOC_CAP_DEFAULT);
    if (!server) {
        ESP_LOGE(TAG, "Failed to allocate server structure");
        return ESP_ERR_NO_MEM;
    }
    
    /* Copy configuration */
    memcpy(&server->config, config, sizeof(esp32_mcp_server_config_t));
    
    /* Create synchronization objects */
    server->mutex = xSemaphoreCreateMutex();
    if (!server->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(server);
        return ESP_ERR_NO_MEM;
    }
    
    server->message_queue = xQueueCreate(config->max_concurrent_requests, 
                                        sizeof(mcp_transport_message_t*));
    if (!server->message_queue) {
        ESP_LOGE(TAG, "Failed to create message queue");
        vSemaphoreDelete(server->mutex);
        free(server);
        return ESP_ERR_NO_MEM;
    }
    
    /* Skip transport initialization for now - will be added later */
    server->transport = NULL;
    
    /* Register built-in tools */
    ret = mcp_register_builtin_tools(server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register built-in tools: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    /* Initialize statistics */
    memset(&server->stats, 0, sizeof(mcp_server_stats_t));
    server->start_time = esp_timer_get_time();
    server->next_message_id = 1;
    
    server->initialized = true;
    *server_handle = server;
    
    ESP_LOGI(TAG, "MCP server initialized successfully");
    return ESP_OK;
    
cleanup:
    if (server->transport) {
        esp32_mcp_transport_deinit(server->transport);
    }
    if (server->message_queue) {
        vQueueDelete(server->message_queue);
    }
    if (server->mutex) {
        vSemaphoreDelete(server->mutex);
    }
    free(server);
    return ret;
}

/* Start MCP server */
esp_err_t esp32_mcp_server_start(esp32_mcp_server_handle_t server_handle)
{
    if (!server_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_server_t* server = (esp32_mcp_server_t*)server_handle;
    
    if (!server->initialized) {
        ESP_LOGE(TAG, "Server not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (server->running) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting MCP server");
    
    /* Skip transport start for now */
    esp_err_t ret = ESP_OK;
    
    /* Create server task */
    BaseType_t task_ret = xTaskCreate(mcp_server_task, 
                                     "mcp_server", 
                                     server->config.server_task_stack_size / sizeof(StackType_t),
                                     server, 
                                     server->config.server_task_priority, 
                                     &server->server_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create server task");
        esp32_mcp_transport_stop(server->transport);
        return ESP_ERR_NO_MEM;
    }
    
    /* Create message processing task */
    task_ret = xTaskCreate(mcp_message_task, 
                          "mcp_message", 
                          server->config.server_task_stack_size / sizeof(StackType_t),
                          server, 
                          server->config.server_task_priority - 1, 
                          &server->message_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create message task");
        vTaskDelete(server->server_task);
        esp32_mcp_transport_stop(server->transport);
        return ESP_ERR_NO_MEM;
    }
    
    server->running = true;
    ESP_LOGI(TAG, "MCP server started successfully");
    
    return ESP_OK;
}

/* Stop MCP server */
esp_err_t esp32_mcp_server_stop(esp32_mcp_server_handle_t server_handle)
{
    if (!server_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_server_t* server = (esp32_mcp_server_t*)server_handle;
    
    if (!server->running) {
        ESP_LOGW(TAG, "Server not running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping MCP server");
    
    server->running = false;
    
    /* Skip transport stop for now */
    
    /* Delete tasks */
    if (server->server_task) {
        vTaskDelete(server->server_task);
        server->server_task = NULL;
    }
    
    if (server->message_task) {
        vTaskDelete(server->message_task);
        server->message_task = NULL;
    }
    
    ESP_LOGI(TAG, "MCP server stopped");
    return ESP_OK;
}

/* Deinitialize MCP server */
esp_err_t esp32_mcp_server_deinit(esp32_mcp_server_handle_t server_handle)
{
    if (!server_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_server_t* server = (esp32_mcp_server_t*)server_handle;
    
    /* Stop server if running */
    if (server->running) {
        esp32_mcp_server_stop(server_handle);
    }
    
    ESP_LOGI(TAG, "Deinitializing MCP server");
    
    /* Skip transport cleanup for now */
    
    /* Cleanup synchronization objects */
    if (server->message_queue) {
        vQueueDelete(server->message_queue);
    }
    
    if (server->mutex) {
        vSemaphoreDelete(server->mutex);
    }
    
    /* Free server structure */
    free(server);
    
    ESP_LOGI(TAG, "MCP server deinitialized");
    return ESP_OK;
}

/* Register built-in tools */
static esp_err_t mcp_register_builtin_tools(esp32_mcp_server_t* server)
{
    server->tool_count = 0;
    
    /* Register display tool */
    if (server->config.enable_display_tool) {
        mcp_tool_t* tool = &server->tools[server->tool_count++];
        tool->name = MCP_TOOL_DISPLAY_NAME;
        tool->description = MCP_TOOL_DISPLAY_DESCRIPTION;
        tool->input_schema_json = mcp_tool_display_get_schema();
        tool->type = MCP_TOOL_TYPE_DISPLAY;
        tool->execute = mcp_tool_display_execute;
    }
    
    /* Register GPIO tool */
    if (server->config.enable_gpio_tool) {
        mcp_tool_t* tool = &server->tools[server->tool_count++];
        tool->name = MCP_TOOL_GPIO_NAME;
        tool->description = MCP_TOOL_GPIO_DESCRIPTION;
        tool->input_schema_json = mcp_tool_gpio_get_schema();
        tool->type = MCP_TOOL_TYPE_GPIO;
        tool->execute = mcp_tool_gpio_execute;
    }
    
    /* Register system tool */
    if (server->config.enable_system_tool) {
        mcp_tool_t* tool = &server->tools[server->tool_count++];
        tool->name = MCP_TOOL_SYSTEM_NAME;
        tool->description = MCP_TOOL_SYSTEM_DESCRIPTION;
        tool->input_schema_json = mcp_tool_system_get_schema();
        tool->type = MCP_TOOL_TYPE_SYSTEM;
        tool->execute = mcp_tool_system_execute;
    }
    
    /* Register status tool */
    if (server->config.enable_status_tool) {
        mcp_tool_t* tool = &server->tools[server->tool_count++];
        tool->name = MCP_TOOL_STATUS_NAME;
        tool->description = MCP_TOOL_STATUS_DESCRIPTION;
        tool->input_schema_json = mcp_tool_status_get_schema();
        tool->type = MCP_TOOL_TYPE_STATUS;
        tool->execute = mcp_tool_status_execute;
    }
    
    ESP_LOGI(TAG, "Registered %d built-in tools", server->tool_count);
    return ESP_OK;
}

/* Server main task */
static void mcp_server_task(void* arg)
{
    esp32_mcp_server_t* server = (esp32_mcp_server_t*)arg;
    
    ESP_LOGI(TAG, "MCP server task started");
    
    while (server->running) {
        /* Update statistics */
        if (xSemaphoreTake(server->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            server->stats.uptime_ms = (esp_timer_get_time() - server->start_time) / 1000;
            server->stats.free_heap = esp_get_free_heap_size();
            if (server->stats.free_heap < server->stats.min_free_heap || 
                server->stats.min_free_heap == 0) {
                server->stats.min_free_heap = server->stats.free_heap;
            }
            xSemaphoreGive(server->mutex);
        }
        
        /* Skip transport status check for now */
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "MCP server task stopped");
    vTaskDelete(NULL);
}

/* Message processing task */
static void mcp_message_task(void* arg)
{
    esp32_mcp_server_t* server = (esp32_mcp_server_t*)arg;
    char* message;
    
    ESP_LOGI(TAG, "MCP message task started");
    
    while (server->running) {
        /* Wait for message */
        if (xQueueReceive(server->message_queue, &message, pdMS_TO_TICKS(1000)) == pdTRUE) {
            /* Process message */
            if (message) {
                ESP_LOGD(TAG, "Processing message: %s", message);
                
                esp_err_t ret = mcp_handle_message(server, message);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to handle message: %s", esp_err_to_name(ret));
                    server->stats.errors_count++;
                }
                
                /* Free message */
                free(message);
            }
        }
    }
    
    ESP_LOGI(TAG, "MCP message task stopped");
    vTaskDelete(NULL);
}

/* Placeholder for future transport message callback */

/* Handle incoming message */
static esp_err_t mcp_handle_message(esp32_mcp_server_t* server, const char* message_json)
{
    cJSON* json = cJSON_Parse(message_json);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON message");
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON* method = cJSON_GetObjectItem(json, "method");
    cJSON* id = cJSON_GetObjectItem(json, "id");
    cJSON* params = cJSON_GetObjectItem(json, "params");
    
    if (!method || !cJSON_IsString(method)) {
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* method_str = cJSON_GetStringValue(method);
    uint32_t message_id = id ? (uint32_t)cJSON_GetNumberValue(id) : 0;
    
    ESP_LOGI(TAG, "Handling method: %s, id: %d", method_str, message_id);
    
    /* Handle different methods */
    if (strcmp(method_str, "tools/list") == 0) {
        /* List available tools */
        cJSON* tools_array = cJSON_CreateArray();
        for (uint32_t i = 0; i < server->tool_count; i++) {
            cJSON* tool_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(tool_obj, "name", server->tools[i].name);
            cJSON_AddStringToObject(tool_obj, "description", server->tools[i].description);
            
            cJSON* schema = cJSON_Parse(server->tools[i].input_schema_json);
            if (schema) {
                cJSON_AddItemToObject(tool_obj, "inputSchema", schema);
            }
            
            cJSON_AddItemToArray(tools_array, tool_obj);
        }
        
        cJSON* result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "tools", tools_array);
        
        char* result_str = cJSON_Print(result);
        mcp_send_response(server, message_id, result_str, NULL);
        
        free(result_str);
        cJSON_Delete(result);
        
    } else if (strcmp(method_str, "tools/call") == 0) {
        /* Call tool */
        cJSON* name = cJSON_GetObjectItem(params, "name");
        cJSON* arguments = cJSON_GetObjectItem(params, "arguments");
        
        if (!name || !cJSON_IsString(name)) {
            mcp_send_response(server, message_id, NULL, "Missing or invalid tool name");
            cJSON_Delete(json);
            return ESP_ERR_INVALID_ARG;
        }
        
        const char* tool_name = cJSON_GetStringValue(name);
        char* args_str = arguments ? cJSON_Print(arguments) : strdup("{}");
        
        /* Find and execute tool */
        bool tool_found = false;
        for (uint32_t i = 0; i < server->tool_count; i++) {
            if (strcmp(server->tools[i].name, tool_name) == 0) {
                char result_buffer[1024];
                esp_err_t ret = server->tools[i].execute(args_str, result_buffer, sizeof(result_buffer));
                
                if (ret == ESP_OK) {
                    mcp_send_response(server, message_id, result_buffer, NULL);
                    server->stats.tools_executed++;
                } else {
                    mcp_send_response(server, message_id, NULL, "Tool execution failed");
                    server->stats.errors_count++;
                }
                
                tool_found = true;
                break;
            }
        }
        
        if (!tool_found) {
            mcp_send_response(server, message_id, NULL, "Tool not found");
            server->stats.errors_count++;
        }
        
        free(args_str);
        
    } else {
        /* Unknown method */
        mcp_send_response(server, message_id, NULL, "Unknown method");
        server->stats.errors_count++;
    }
    
    server->stats.requests_processed++;
    cJSON_Delete(json);
    return ESP_OK;
}

/* Send response */
static esp_err_t mcp_send_response(esp32_mcp_server_t* server, uint32_t id, 
                                  const char* result_json, const char* error_msg)
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
    }
    
    char* response_str = cJSON_Print(response);
    if (response_str) {
        /* Skip actual sending for now - just log */
        ESP_LOGI(TAG, "Would send response: %s", response_str);
        esp_err_t ret = ESP_OK;
        if (ret == ESP_OK) {
            server->stats.messages_sent++;
        }
        free(response_str);
    }
    
    cJSON_Delete(response);
    return ESP_OK;
}

/* Check if server is running */
bool esp32_mcp_server_is_running(esp32_mcp_server_handle_t server_handle)
{
    if (!server_handle) {
        return false;
    }
    
    esp32_mcp_server_t* server = (esp32_mcp_server_t*)server_handle;
    return server->running;
}

/* Get server statistics */
esp_err_t esp32_mcp_server_get_stats(esp32_mcp_server_handle_t server_handle,
                                     mcp_server_stats_t* stats)
{
    if (!server_handle || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_server_t* server = (esp32_mcp_server_t*)server_handle;
    
    if (xSemaphoreTake(server->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &server->stats, sizeof(mcp_server_stats_t));
        xSemaphoreGive(server->mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}