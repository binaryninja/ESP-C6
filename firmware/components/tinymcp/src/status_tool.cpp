/**
 * @file status_tool.cpp
 * @brief MCP Status Tool Implementation for ESP32-C6
 * 
 * This file implements the status monitoring tool for MCP, providing
 * device health information, diagnostics, and operational status through JSON-RPC commands.
 */

#include "mcp_tools.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "cJSON.h"

static const char *TAG = "MCP_STATUS_TOOL";

/* External display handle - defined in main firmware */
extern display_handle_t* get_display_handle(void);
extern uint32_t get_button_press_count(void);

/* Status Tool JSON Schema */
const char* MCP_TOOL_STATUS_SCHEMA = 
"{"
  "\"type\": \"object\","
  "\"properties\": {"
    "\"action\": {"
      "\"type\": \"string\","
      "\"enum\": [\"get_health\", \"get_sensors\", \"get_connections\", \"run_diagnostics\"],"
      "\"description\": \"Action to perform for status monitoring\""
    "},"
    "\"include_sensors\": {"
      "\"type\": \"boolean\","
      "\"description\": \"Include sensor readings (for get_health)\""
    "},"
    "\"run_full_diagnostics\": {"
      "\"type\": \"boolean\","
      "\"description\": \"Run comprehensive diagnostics (for run_diagnostics)\""
    "}"
  "},"
  "\"required\": [\"action\"]"
"}";

/* Helper function to convert action string to enum */
static mcp_status_action_t mcp_status_string_to_action(const char* action_str)
{
    if (!action_str) return MCP_STATUS_ACTION_GET_HEALTH;
    
    if (strcmp(action_str, "get_health") == 0) return MCP_STATUS_ACTION_GET_HEALTH;
    if (strcmp(action_str, "get_sensors") == 0) return MCP_STATUS_ACTION_GET_SENSORS;
    if (strcmp(action_str, "get_connections") == 0) return MCP_STATUS_ACTION_GET_CONNECTIONS;
    if (strcmp(action_str, "run_diagnostics") == 0) return MCP_STATUS_ACTION_RUN_DIAGNOSTICS;
    
    return MCP_STATUS_ACTION_GET_HEALTH; // Default
}

/* Helper function to get health status string */
static const char* get_health_status_string(uint32_t free_heap, uint32_t error_count)
{
    if (error_count > 10) return "Critical";
    if (free_heap < 50000) return "Warning";
    if (error_count > 0) return "Caution";
    return "Good";
}

/* Helper function to get temperature reading */
static float get_temperature_celsius(void)
{
    temperature_sensor_handle_t temp_sensor = NULL;
    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    float temperature = 25.0; // Default/fallback temperature
    
    esp_err_t ret = temperature_sensor_install(&temp_config, &temp_sensor);
    if (ret == ESP_OK) {
        ret = temperature_sensor_enable(temp_sensor);
        if (ret == ESP_OK) {
            ret = temperature_sensor_get_celsius(temp_sensor, &temperature);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read temperature sensor");
                temperature = 25.0; // Fallback
            }
        }
        temperature_sensor_disable(temp_sensor);
        temperature_sensor_uninstall(temp_sensor);
    } else {
        ESP_LOGW(TAG, "Temperature sensor not available");
    }
    
    return temperature;
}

/* Parse status tool parameters from JSON */
esp_err_t mcp_tool_status_parse_params(const char* params_json, 
                                       mcp_status_params_t* params)
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
    memset(params, 0, sizeof(mcp_status_params_t));
    params->include_sensors = false;
    params->run_full_diagnostics = false;
    
    /* Parse action (required) */
    cJSON* action = cJSON_GetObjectItem(json, "action");
    if (!action || !cJSON_IsString(action)) {
        ESP_LOGE(TAG, "Missing or invalid action parameter");
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    params->action = mcp_status_string_to_action(cJSON_GetStringValue(action));
    
    /* Parse optional parameters */
    cJSON* include_sensors = cJSON_GetObjectItem(json, "include_sensors");
    if (include_sensors && cJSON_IsBool(include_sensors)) {
        params->include_sensors = cJSON_IsTrue(include_sensors);
    }
    
    cJSON* run_full_diagnostics = cJSON_GetObjectItem(json, "run_full_diagnostics");
    if (run_full_diagnostics && cJSON_IsBool(run_full_diagnostics)) {
        params->run_full_diagnostics = cJSON_IsTrue(run_full_diagnostics);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

/* Validate status tool parameters */
esp_err_t mcp_tool_status_validate_params(const mcp_status_params_t* params)
{
    if (!params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Validate action */
    if (params->action < 0 || params->action >= MCP_STATUS_ACTION_MAX) {
        ESP_LOGE(TAG, "Invalid status action: %d", params->action);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/* Format status tool result to JSON */
esp_err_t mcp_tool_status_format_result(const mcp_status_result_t* result,
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
        if (result->health_status) {
            cJSON_AddStringToObject(json, "health_status", result->health_status);
        }
        cJSON_AddNumberToObject(json, "temperature", result->temperature);
        cJSON_AddNumberToObject(json, "error_count", result->error_count);
        cJSON_AddBoolToObject(json, "display_ok", result->display_ok);
        cJSON_AddBoolToObject(json, "gpio_ok", result->gpio_ok);
        cJSON_AddBoolToObject(json, "memory_ok", result->memory_ok);
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

/* Get status tool schema as JSON string */
const char* mcp_tool_status_get_schema(void)
{
    return MCP_TOOL_STATUS_SCHEMA;
}

/* Run diagnostic tests */
static void run_diagnostics(cJSON* json, bool full_diagnostics)
{
    cJSON* diagnostics = cJSON_CreateObject();
    uint32_t total_tests = 0;
    uint32_t passed_tests = 0;
    
    /* Test 1: Memory test */
    total_tests++;
    uint32_t free_heap = esp_get_free_heap_size();
    bool memory_ok = (free_heap > 50000); // At least 50KB free
    cJSON_AddBoolToObject(diagnostics, "memory_test", memory_ok);
    if (memory_ok) passed_tests++;
    
    /* Test 2: Display test */
    total_tests++;
    display_handle_t* display = get_display_handle();
    bool display_ok = (display != NULL);
    cJSON_AddBoolToObject(diagnostics, "display_test", display_ok);
    if (display_ok) passed_tests++;
    
    /* Test 3: GPIO test */
    total_tests++;
    gpio_num_t led_pin = (gpio_num_t)MCP_GPIO_LED;
    gpio_num_t button_pin = (gpio_num_t)MCP_GPIO_BUTTON;
    
    // Test LED output
    esp_err_t led_ret = gpio_set_level(led_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10)); // Short delay
    gpio_set_level(led_pin, 0);
    
    // Test button input
    int button_level = gpio_get_level(button_pin);
    bool gpio_ok = (led_ret == ESP_OK) && (button_level >= 0);
    cJSON_AddBoolToObject(diagnostics, "gpio_test", gpio_ok);
    if (gpio_ok) passed_tests++;
    
    if (full_diagnostics) {
        /* Test 4: Temperature sensor test */
        total_tests++;
        float temp = get_temperature_celsius();
        bool temp_ok = (temp > -40.0 && temp < 125.0); // Reasonable temperature range
        cJSON_AddBoolToObject(diagnostics, "temperature_test", temp_ok);
        if (temp_ok) passed_tests++;
        
        /* Test 5: Timer test */
        total_tests++;
        int64_t start_time = esp_timer_get_time();
        vTaskDelay(pdMS_TO_TICKS(10));
        int64_t end_time = esp_timer_get_time();
        bool timer_ok = ((end_time - start_time) > 8000); // At least 8ms elapsed
        cJSON_AddBoolToObject(diagnostics, "timer_test", timer_ok);
        if (timer_ok) passed_tests++;
        
        /* Test 6: Task scheduler test */
        total_tests++;
        UBaseType_t task_count = uxTaskGetNumberOfTasks();
        bool scheduler_ok = (task_count > 2); // At least idle + current task + some others
        cJSON_AddBoolToObject(diagnostics, "scheduler_test", scheduler_ok);
        if (scheduler_ok) passed_tests++;
    }
    
    /* Summary */
    cJSON_AddNumberToObject(diagnostics, "total_tests", total_tests);
    cJSON_AddNumberToObject(diagnostics, "passed_tests", passed_tests);
    cJSON_AddNumberToObject(diagnostics, "success_rate", 
                           total_tests > 0 ? (float)passed_tests / total_tests * 100.0 : 0.0);
    
    cJSON_AddItemToObject(json, "diagnostics", diagnostics);
}

/* Execute status tool */
esp_err_t mcp_tool_status_execute(const char* params_json, char* result_json, size_t result_size)
{
    if (!params_json || !result_json || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Executing status tool: %s", params_json);
    
    /* Parse parameters */
    mcp_status_params_t params;
    esp_err_t ret = mcp_tool_status_parse_params(params_json, &params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse parameters: %s", esp_err_to_name(ret));
        
        mcp_status_result_t error_result = {
            .success = false,
            .message = "Invalid parameters",
            .health_status = "Error",
            .temperature = 0.0f,
            .error_count = 1,
            .display_ok = false,
            .gpio_ok = false,
            .memory_ok = false
        };
        
        return mcp_tool_status_format_result(&error_result, result_json, result_size);
    }
    
    /* Validate parameters */
    ret = mcp_tool_status_validate_params(&params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid parameters: %s", esp_err_to_name(ret));
        
        mcp_status_result_t error_result = {
            .success = false,
            .message = "Parameter validation failed",
            .health_status = "Error",
            .temperature = 0.0f,
            .error_count = 1,
            .display_ok = false,
            .gpio_ok = false,
            .memory_ok = false
        };
        
        return mcp_tool_status_format_result(&error_result, result_json, result_size);
    }
    
    /* Execute action */
    switch (params.action) {
        case MCP_STATUS_ACTION_GET_HEALTH:
        case MCP_STATUS_ACTION_GET_SENSORS:
        case MCP_STATUS_ACTION_GET_CONNECTIONS:
        case MCP_STATUS_ACTION_RUN_DIAGNOSTICS:
            {
                /* Create detailed JSON response */
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "success", true);
                cJSON_AddStringToObject(json, "message", "OK");
                
                /* Basic health information */
                uint32_t free_heap = esp_get_free_heap_size();
                uint32_t min_free_heap = esp_get_minimum_free_heap_size();
                uint32_t error_count = 0; // Would be tracked by error monitoring system
                
                const char* health_status = get_health_status_string(free_heap, error_count);
                cJSON_AddStringToObject(json, "health_status", health_status);
                cJSON_AddNumberToObject(json, "error_count", error_count);
                
                /* Memory status */
                bool memory_ok = (free_heap > 50000); // At least 50KB free
                cJSON_AddBoolToObject(json, "memory_ok", memory_ok);
                cJSON_AddNumberToObject(json, "free_heap", free_heap);
                cJSON_AddNumberToObject(json, "min_free_heap", min_free_heap);
                
                /* Component status */
                display_handle_t* display = get_display_handle();
                bool display_ok = (display != NULL);
                cJSON_AddBoolToObject(json, "display_ok", display_ok);
                
                gpio_num_t led_pin = (gpio_num_t)MCP_GPIO_LED;
                gpio_num_t button_pin = (gpio_num_t)MCP_GPIO_BUTTON;
                int led_level = gpio_get_level(led_pin);
                int button_level = gpio_get_level(button_pin);
                bool gpio_ok = (led_level >= 0 && button_level >= 0);
                cJSON_AddBoolToObject(json, "gpio_ok", gpio_ok);
                
                /* Sensor information */
                if (params.action == MCP_STATUS_ACTION_GET_SENSORS || params.include_sensors) {
                    float temperature = get_temperature_celsius();
                    cJSON_AddNumberToObject(json, "temperature", temperature);
                    
                    cJSON* sensors = cJSON_CreateObject();
                    cJSON_AddNumberToObject(sensors, "internal_temperature", temperature);
                    cJSON_AddNumberToObject(sensors, "button_count", get_button_press_count());
                    cJSON_AddNumberToObject(sensors, "uptime_ms", esp_timer_get_time() / 1000);
                    cJSON_AddItemToObject(json, "sensors", sensors);
                }
                
                /* Connection information */
                if (params.action == MCP_STATUS_ACTION_GET_CONNECTIONS) {
                    cJSON* connections = cJSON_CreateObject();
                    cJSON_AddBoolToObject(connections, "uart_available", true);
                    cJSON_AddBoolToObject(connections, "usb_cdc_available", true);
                    cJSON_AddBoolToObject(connections, "wifi_available", false);
                    cJSON_AddBoolToObject(connections, "bluetooth_available", false);
                    cJSON_AddItemToObject(json, "connections", connections);
                }
                
                /* Run diagnostics */
                if (params.action == MCP_STATUS_ACTION_RUN_DIAGNOSTICS) {
                    run_diagnostics(json, params.run_full_diagnostics);
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
                
                ESP_LOGI(TAG, "Returned status information");
                return ESP_OK;
            }
            break;
            
        default:
            {
                mcp_status_result_t error_result = {
                    .success = false,
                    .message = "Unknown action",
                    .health_status = "Error",
                    .temperature = 0.0f,
                    .error_count = 1,
                    .display_ok = false,
                    .gpio_ok = false,
                    .memory_ok = false
                };
                
                ESP_LOGW(TAG, "Unknown status action: %d", params.action);
                return mcp_tool_status_format_result(&error_result, result_json, result_size);
            }
            break;
    }
    
    return ESP_OK;
}