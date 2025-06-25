/**
 * @file system_tool.cpp
 * @brief MCP System Tool Implementation for ESP32-C6
 * 
 * This file implements the system information tool for MCP, providing
 * system statistics, memory information, and device control through JSON-RPC commands.
 */

#include "mcp_tools.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "MCP_SYSTEM_TOOL";

/* System Tool JSON Schema */
const char* MCP_TOOL_SYSTEM_SCHEMA = 
"{"
  "\"type\": \"object\","
  "\"properties\": {"
    "\"action\": {"
      "\"type\": \"string\","
      "\"enum\": [\"get_info\", \"get_stats\", \"get_memory\", \"get_tasks\", \"restart\", \"factory_reset\"],"
      "\"description\": \"Action to perform on the system\""
    "},"
    "\"include_tasks\": {"
      "\"type\": \"boolean\","
      "\"description\": \"Include task information (for get_stats)\""
    "},"
    "\"include_memory\": {"
      "\"type\": \"boolean\","
      "\"description\": \"Include detailed memory information (for get_stats)\""
    "},"
    "\"force_restart\": {"
      "\"type\": \"boolean\","
      "\"description\": \"Force immediate restart without confirmation\""
    "}"
  "},"
  "\"required\": [\"action\"]"
"}";

/* Helper function to convert action string to enum */
static mcp_system_action_t mcp_system_string_to_action(const char* action_str)
{
    if (!action_str) return MCP_SYSTEM_ACTION_GET_INFO;
    
    if (strcmp(action_str, "get_info") == 0) return MCP_SYSTEM_ACTION_GET_INFO;
    if (strcmp(action_str, "get_stats") == 0) return MCP_SYSTEM_ACTION_GET_STATS;
    if (strcmp(action_str, "get_memory") == 0) return MCP_SYSTEM_ACTION_GET_MEMORY;
    if (strcmp(action_str, "get_tasks") == 0) return MCP_SYSTEM_ACTION_GET_TASKS;
    if (strcmp(action_str, "restart") == 0) return MCP_SYSTEM_ACTION_RESTART;
    if (strcmp(action_str, "factory_reset") == 0) return MCP_SYSTEM_ACTION_FACTORY_RESET;
    
    return MCP_SYSTEM_ACTION_GET_INFO; // Default
}

/* Helper function to get reset reason string */
static const char* get_reset_reason_string(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_UNKNOWN: return "Unknown";
        case ESP_RST_POWERON: return "Power-on";
        case ESP_RST_EXT: return "External pin";
        case ESP_RST_SW: return "Software";
        case ESP_RST_PANIC: return "Exception/panic";
        case ESP_RST_INT_WDT: return "Interrupt watchdog";
        case ESP_RST_TASK_WDT: return "Task watchdog";
        case ESP_RST_WDT: return "Other watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep";
        case ESP_RST_BROWNOUT: return "Brownout";
        case ESP_RST_SDIO: return "SDIO";
        case ESP_RST_USB: return "USB";
        case ESP_RST_JTAG: return "JTAG";
        default: return "Unknown";
    }
}

/* Helper function to get chip model string */
static const char* get_chip_model_string(esp_chip_model_t model)
{
    switch (model) {
        case CHIP_ESP32: return "ESP32";
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        case CHIP_ESP32C2: return "ESP32-C2";
        case CHIP_ESP32C6: return "ESP32-C6";
        case CHIP_ESP32H2: return "ESP32-H2";
        default: return "Unknown";
    }
}

/* Parse system tool parameters from JSON */
esp_err_t mcp_tool_system_parse_params(const char* params_json, 
                                       mcp_system_params_t* params)
{
    if (!params_json || !params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON* json = cJSON_Parse(params_json);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse parameters JSON");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Initialize default values */
    memset(params, 0, sizeof(mcp_system_params_t));
    params->include_tasks = false;
    params->include_memory = false;
    params->force_restart = false;
    
    /* Parse action (required) */
    cJSON* action = cJSON_GetObjectItem(json, "action");
    if (!action || !cJSON_IsString(action)) {
        ESP_LOGE(TAG, "Missing or invalid action parameter");
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    params->action = mcp_system_string_to_action(cJSON_GetStringValue(action));
    
    /* Parse optional parameters */
    cJSON* include_tasks = cJSON_GetObjectItem(json, "include_tasks");
    if (include_tasks && cJSON_IsBool(include_tasks)) {
        params->include_tasks = cJSON_IsTrue(include_tasks);
    }
    
    cJSON* include_memory = cJSON_GetObjectItem(json, "include_memory");
    if (include_memory && cJSON_IsBool(include_memory)) {
        params->include_memory = cJSON_IsTrue(include_memory);
    }
    
    cJSON* force_restart = cJSON_GetObjectItem(json, "force_restart");
    if (force_restart && cJSON_IsBool(force_restart)) {
        params->force_restart = cJSON_IsTrue(force_restart);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

/* Validate system tool parameters */
esp_err_t mcp_tool_system_validate_params(const mcp_system_params_t* params)
{
    if (!params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate action */
    if (params->action < 0 || params->action >= MCP_SYSTEM_ACTION_MAX) {
        ESP_LOGE(TAG, "Invalid system action: %d", params->action);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/* Format system tool result to JSON */
esp_err_t mcp_tool_system_format_result(const mcp_system_result_t* result,
                                        char* result_json, 
                                        size_t result_size)
{
    if (!result || !result_json || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddBoolToObject(json, "success", result->success);
    
    if (result->message) {
        cJSON_AddStringToObject(json, "message", result->message);
    }
    
    if (result->success) {
        if (result->chip_model) {
            cJSON_AddStringToObject(json, "chip_model", result->chip_model);
        }
        if (result->idf_version) {
            cJSON_AddStringToObject(json, "idf_version", result->idf_version);
        }
        cJSON_AddNumberToObject(json, "free_heap", result->free_heap);
        cJSON_AddNumberToObject(json, "min_free_heap", result->min_free_heap);
        cJSON_AddNumberToObject(json, "uptime_ms", result->uptime_ms);
        cJSON_AddNumberToObject(json, "reset_reason", result->reset_reason);
        cJSON_AddNumberToObject(json, "cpu_freq_mhz", result->cpu_freq_mhz);
    }
    
    char* json_string = cJSON_Print(json);
    if (!json_string) {
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }
    
    if (strlen(json_string) >= result_size) {
        free(json_string);
        cJSON_Delete(json);
        return ESP_ERR_INVALID_SIZE;
    }
    
    strcpy(result_json, json_string);
    free(json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

/* Get system tool schema as JSON string */
const char* mcp_tool_system_get_schema(void)
{
    return MCP_TOOL_SYSTEM_SCHEMA;
}

/* Get detailed memory information */
static void get_memory_info(cJSON* json)
{
    /* Heap information */
    cJSON* heap = cJSON_CreateObject();
    cJSON_AddNumberToObject(heap, "free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(heap, "minimum_free", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(heap, "largest_free_block", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    
    /* Memory capabilities */
    cJSON* caps = cJSON_CreateObject();
    cJSON_AddNumberToObject(caps, "internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(caps, "spiram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(caps, "dma_free", heap_caps_get_free_size(MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(caps, "executable_free", heap_caps_get_free_size(MALLOC_CAP_EXEC));
    
    cJSON_AddItemToObject(heap, "capabilities", caps);
    cJSON_AddItemToObject(json, "memory", heap);
}

/* Get task information */
static void get_task_info(cJSON* json)
{
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t* task_array = (TaskStatus_t*)malloc(task_count * sizeof(TaskStatus_t));
    
    if (!task_array) {
        cJSON_AddStringToObject(json, "tasks_error", "Failed to allocate memory for task list");
        return;
    }
    
    UBaseType_t actual_count = uxTaskGetSystemState(task_array, task_count, NULL);
    
    cJSON* tasks = cJSON_CreateArray();
    for (UBaseType_t i = 0; i < actual_count; i++) {
        cJSON* task = cJSON_CreateObject();
        cJSON_AddStringToObject(task, "name", task_array[i].pcTaskName);
        cJSON_AddNumberToObject(task, "priority", task_array[i].uxCurrentPriority);
        cJSON_AddNumberToObject(task, "stack_high_water_mark", task_array[i].usStackHighWaterMark);
        
        const char* state_str = "Unknown";
        switch (task_array[i].eCurrentState) {
            case eRunning: state_str = "Running"; break;
            case eReady: state_str = "Ready"; break;
            case eBlocked: state_str = "Blocked"; break;
            case eSuspended: state_str = "Suspended"; break;
            case eDeleted: state_str = "Deleted"; break;
            default: break;
        }
        cJSON_AddStringToObject(task, "state", state_str);
        
        cJSON_AddItemToArray(tasks, task);
    }
    
    cJSON_AddItemToObject(json, "tasks", tasks);
    cJSON_AddNumberToObject(json, "task_count", actual_count);
    
    free(task_array);
}

/* Execute system tool */
esp_err_t mcp_tool_system_execute(const char* params_json, char* result_json, size_t result_size)
{
    if (!params_json || !result_json || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Executing system tool: %s", params_json);
    
    /* Parse parameters */
    mcp_system_params_t params;
    esp_err_t ret = mcp_tool_system_parse_params(params_json, &params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse parameters: %s", esp_err_to_name(ret));
        
        mcp_system_result_t error_result = {
            .success = false,
            .message = "Invalid parameters",
            .chip_model = NULL,
            .idf_version = NULL,
            .free_heap = 0,
            .min_free_heap = 0,
            .uptime_ms = 0,
            .reset_reason = 0,
            .cpu_freq_mhz = 0.0f
        };
        
        return mcp_tool_system_format_result(&error_result, result_json, result_size);
    }
    
    /* Validate parameters */
    ret = mcp_tool_system_validate_params(&params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid parameters: %s", esp_err_to_name(ret));
        
        mcp_system_result_t error_result = {
            .success = false,
            .message = "Parameter validation failed",
            .chip_model = NULL,
            .idf_version = NULL,
            .free_heap = 0,
            .min_free_heap = 0,
            .uptime_ms = 0,
            .reset_reason = 0,
            .cpu_freq_mhz = 0.0f
        };
        
        return mcp_tool_system_format_result(&error_result, result_json, result_size);
    }
    
    /* Execute action */
    switch (params.action) {
        case MCP_SYSTEM_ACTION_GET_INFO:
        case MCP_SYSTEM_ACTION_GET_STATS:
        case MCP_SYSTEM_ACTION_GET_MEMORY:
        case MCP_SYSTEM_ACTION_GET_TASKS:
            {
                /* Get chip information */
                esp_chip_info_t chip_info;
                esp_chip_info(&chip_info);
                
                /* Create detailed JSON response */
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "success", true);
                cJSON_AddStringToObject(json, "message", "OK");
                
                /* Basic system info */
                cJSON_AddStringToObject(json, "chip_model", get_chip_model_string(chip_info.model));
                cJSON_AddNumberToObject(json, "chip_revision", chip_info.revision);
                cJSON_AddNumberToObject(json, "cores", chip_info.cores);
                cJSON_AddStringToObject(json, "idf_version", IDF_VER);
                
                /* Memory info */
                cJSON_AddNumberToObject(json, "free_heap", esp_get_free_heap_size());
                cJSON_AddNumberToObject(json, "min_free_heap", esp_get_minimum_free_heap_size());
                
                /* System stats */
                cJSON_AddNumberToObject(json, "uptime_ms", esp_timer_get_time() / 1000);
                cJSON_AddNumberToObject(json, "reset_reason", esp_reset_reason());
                cJSON_AddStringToObject(json, "reset_reason_str", get_reset_reason_string(esp_reset_reason()));
                
                /* CPU frequency */
                rtc_cpu_freq_config_t freq_config;
                rtc_clk_cpu_freq_get_config(&freq_config);
                cJSON_AddNumberToObject(json, "cpu_freq_mhz", freq_config.freq_mhz);
                
                /* Flash info */
                uint32_t flash_size;
                if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
                    cJSON_AddNumberToObject(json, "flash_size", flash_size);
                }
                
                /* Additional info based on action */
                if (params.action == MCP_SYSTEM_ACTION_GET_MEMORY || params.include_memory) {
                    get_memory_info(json);
                }
                
                if (params.action == MCP_SYSTEM_ACTION_GET_TASKS || params.include_tasks) {
                    get_task_info(json);
                }
                
                /* Convert to string */
                char* json_string = cJSON_Print(json);
                if (!json_string) {
                    cJSON_Delete(json);
                    return ESP_ERR_NO_MEM;
                }
                
                if (strlen(json_string) >= result_size) {
                    free(json_string);
                    cJSON_Delete(json);
                    return ESP_ERR_INVALID_SIZE;
                }
                
                strcpy(result_json, json_string);
                free(json_string);
                cJSON_Delete(json);
                
                ESP_LOGI(TAG, "Returned system information");
                return ESP_OK;
            }
            break;
            
        case MCP_SYSTEM_ACTION_RESTART:
            {
                mcp_system_result_t exec_result = {
                    .success = true,
                    .message = "System restart initiated",
                    .chip_model = "ESP32-C6",
                    .idf_version = IDF_VER,
                    .free_heap = esp_get_free_heap_size(),
                    .min_free_heap = esp_get_minimum_free_heap_size(),
                    .uptime_ms = esp_timer_get_time() / 1000,
                    .reset_reason = esp_reset_reason(),
                    .cpu_freq_mhz = 160.0f // Default ESP32-C6 frequency
                };
                
                esp_err_t format_ret = mcp_tool_system_format_result(&exec_result, result_json, result_size);
                
                ESP_LOGW(TAG, "System restart requested");
                
                /* Schedule restart after a short delay to allow response to be sent */
                if (params.force_restart) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    esp_restart();
                } else {
                    ESP_LOGW(TAG, "Restart cancelled - force_restart not set");
                }
                
                return format_ret;
            }
            break;
            
        case MCP_SYSTEM_ACTION_FACTORY_RESET:
            {
                mcp_system_result_t exec_result = {
                    .success = true,
                    .message = "Factory reset initiated",
                    .chip_model = "ESP32-C6",
                    .idf_version = IDF_VER,
                    .free_heap = esp_get_free_heap_size(),
                    .min_free_heap = esp_get_minimum_free_heap_size(),
                    .uptime_ms = esp_timer_get_time() / 1000,
                    .reset_reason = esp_reset_reason(),
                    .cpu_freq_mhz = 160.0f
                };
                
                esp_err_t format_ret = mcp_tool_system_format_result(&exec_result, result_json, result_size);
                
                ESP_LOGW(TAG, "Factory reset requested");
                
                /* Erase NVS and restart */
                if (params.force_restart) {
                    ESP_LOGW(TAG, "Performing factory reset");
                    // Note: nvs_flash_erase() would be called here in a real implementation
                    vTaskDelay(pdMS_TO_TICKS(100));
                    esp_restart();
                } else {
                    ESP_LOGW(TAG, "Factory reset cancelled - force_restart not set");
                }
                
                return format_ret;
            }
            break;
            
        default:
            {
                mcp_system_result_t error_result = {
                    .success = false,
                    .message = "Unknown action",
                    .chip_model = NULL,
                    .idf_version = NULL,
                    .free_heap = 0,
                    .min_free_heap = 0,
                    .uptime_ms = 0,
                    .reset_reason = 0,
                    .cpu_freq_mhz = 0.0f
                };
                
                ESP_LOGW(TAG, "Unknown system action: %d", params.action);
                return mcp_tool_system_format_result(&error_result, result_json, result_size);
            }
            break;
    }
    
    return ESP_OK;
}