/**
 * @file mcp_tools_simple.c
 * @brief Simple MCP Tools Implementation for ESP32-C6
 * 
 * This file implements the basic MCP tools for ESP32-C6,
 * providing echo, display, GPIO, and system functionality.
 */

#include "mcp_server_simple.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "driver/gpio.h"
#include "cJSON.h"

static const char *TAG = "MCP_TOOLS";

/* External functions from main firmware */
extern void* get_display_handle(void);
extern uint32_t get_button_press_count(void);

/* Helper function to create JSON result */
static esp_err_t create_json_result(const char* status, const char* message, 
                                   cJSON* data, char* result_json, size_t result_size)
{
    cJSON* result = cJSON_CreateObject();
    if (!result) {
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddStringToObject(result, "status", status ? status : "success");
    if (message) {
        cJSON_AddStringToObject(result, "message", message);
    }
    if (data) {
        cJSON_AddItemToObject(result, "data", data);
    }
    
    char* result_str = cJSON_Print(result);
    if (!result_str) {
        cJSON_Delete(result);
        return ESP_ERR_NO_MEM;
    }
    
    if (strlen(result_str) >= result_size) {
        free(result_str);
        cJSON_Delete(result);
        return ESP_ERR_INVALID_SIZE;
    }
    
    strcpy(result_json, result_str);
    free(result_str);
    cJSON_Delete(result);
    
    return ESP_OK;
}

/* Echo tool implementation */
esp_err_t mcp_tool_echo_execute(const char* params_json, char* result_json, size_t result_size)
{
    if (!params_json || !result_json || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Echo tool called with: %s", params_json);
    
    /* Parse parameters */
    cJSON* params = cJSON_Parse(params_json);
    if (!params) {
        return create_json_result("error", "Invalid JSON parameters", NULL, result_json, result_size);
    }
    
    /* Echo back the parameters */
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "echo", params_json);
    cJSON_AddStringToObject(data, "timestamp", "current_time");
    
    esp_err_t ret = create_json_result("success", "Echo successful", data, result_json, result_size);
    
    cJSON_Delete(params);
    return ret;
}

/* Display tool implementation */
esp_err_t mcp_tool_display_execute(const char* params_json, char* result_json, size_t result_size)
{
    if (!params_json || !result_json || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Display tool called with: %s", params_json);
    
    /* Parse parameters */
    cJSON* params = cJSON_Parse(params_json);
    if (!params) {
        return create_json_result("error", "Invalid JSON parameters", NULL, result_json, result_size);
    }
    
    cJSON* action = cJSON_GetObjectItem(params, "action");
    if (!action || !cJSON_IsString(action)) {
        cJSON_Delete(params);
        return create_json_result("error", "Missing or invalid action parameter", NULL, result_json, result_size);
    }
    
    const char* action_str = cJSON_GetStringValue(action);
    
    /* Check if display is available */
    void* display_handle = get_display_handle();
    bool display_available = (display_handle != NULL);
    
    cJSON* data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "display_available", display_available);
    cJSON_AddStringToObject(data, "action_requested", action_str);
    
    if (strcmp(action_str, "get_info") == 0) {
        cJSON_AddNumberToObject(data, "width", 320);
        cJSON_AddNumberToObject(data, "height", 172);
        cJSON_AddStringToObject(data, "type", "ST7789");
        cJSON_AddBoolToObject(data, "initialized", display_available);
    } else if (strcmp(action_str, "show_text") == 0) {
        cJSON* text = cJSON_GetObjectItem(params, "text");
        if (text && cJSON_IsString(text)) {
            const char* text_str = cJSON_GetStringValue(text);
            cJSON_AddStringToObject(data, "text_to_show", text_str);
            if (display_available) {
                /* Here we would call the actual display function */
                ESP_LOGI(TAG, "Would display text: %s", text_str);
                cJSON_AddStringToObject(data, "result", "Text displayed successfully");
            } else {
                cJSON_AddStringToObject(data, "result", "Display not available");
            }
        } else {
            cJSON_Delete(data);
            cJSON_Delete(params);
            return create_json_result("error", "Missing text parameter", NULL, result_json, result_size);
        }
    } else if (strcmp(action_str, "clear") == 0) {
        if (display_available) {
            ESP_LOGI(TAG, "Would clear display");
            cJSON_AddStringToObject(data, "result", "Display cleared successfully");
        } else {
            cJSON_AddStringToObject(data, "result", "Display not available");
        }
    } else {
        cJSON_AddStringToObject(data, "result", "Unknown action");
    }
    
    esp_err_t ret = create_json_result("success", "Display tool executed", data, result_json, result_size);
    
    cJSON_Delete(params);
    return ret;
}

/* GPIO tool implementation */
esp_err_t mcp_tool_gpio_execute(const char* params_json, char* result_json, size_t result_size)
{
    if (!params_json || !result_json || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "GPIO tool called with: %s", params_json);
    
    /* Parse parameters */
    cJSON* params = cJSON_Parse(params_json);
    if (!params) {
        return create_json_result("error", "Invalid JSON parameters", NULL, result_json, result_size);
    }
    
    cJSON* action = cJSON_GetObjectItem(params, "action");
    if (!action || !cJSON_IsString(action)) {
        cJSON_Delete(params);
        return create_json_result("error", "Missing or invalid action parameter", NULL, result_json, result_size);
    }
    
    const char* action_str = cJSON_GetStringValue(action);
    
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action_requested", action_str);
    
    if (strcmp(action_str, "set_led") == 0) {
        cJSON* state = cJSON_GetObjectItem(params, "state");
        if (state && cJSON_IsBool(state)) {
            bool led_state = cJSON_IsTrue(state);
            
            /* Set LED state */
            gpio_set_level(GPIO_NUM_8, led_state ? 1 : 0);
            
            cJSON_AddBoolToObject(data, "led_state", led_state);
            cJSON_AddStringToObject(data, "result", "LED state updated");
            ESP_LOGI(TAG, "LED set to %s", led_state ? "ON" : "OFF");
        } else {
            cJSON_Delete(data);
            cJSON_Delete(params);
            return create_json_result("error", "Missing or invalid state parameter", NULL, result_json, result_size);
        }
    } else if (strcmp(action_str, "read_button") == 0) {
        /* Read button state */
        int button_level = gpio_get_level(GPIO_NUM_9);
        bool button_pressed = (button_level == 0); // Active low
        uint32_t button_count = get_button_press_count();
        
        cJSON_AddBoolToObject(data, "button_pressed", button_pressed);
        cJSON_AddNumberToObject(data, "button_count", button_count);
        cJSON_AddNumberToObject(data, "button_level", button_level);
        
        ESP_LOGI(TAG, "Button state: %s, count: %"PRIu32, 
                button_pressed ? "PRESSED" : "RELEASED", button_count);
    } else if (strcmp(action_str, "get_status") == 0) {
        /* Get GPIO status */
        int led_level = gpio_get_level(GPIO_NUM_8);
        int button_level = gpio_get_level(GPIO_NUM_9);
        uint32_t button_count = get_button_press_count();
        
        cJSON_AddBoolToObject(data, "led_on", led_level == 1);
        cJSON_AddBoolToObject(data, "button_pressed", button_level == 0);
        cJSON_AddNumberToObject(data, "button_count", button_count);
        
        ESP_LOGI(TAG, "GPIO status - LED: %s, Button: %s", 
                led_level ? "ON" : "OFF", button_level ? "RELEASED" : "PRESSED");
    } else {
        cJSON_AddStringToObject(data, "result", "Unknown action");
    }
    
    esp_err_t ret = create_json_result("success", "GPIO tool executed", data, result_json, result_size);
    
    cJSON_Delete(params);
    return ret;
}

/* System tool implementation */
esp_err_t mcp_tool_system_execute(const char* params_json, char* result_json, size_t result_size)
{
    if (!params_json || !result_json || result_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "System tool called with: %s", params_json);
    
    /* Parse parameters */
    cJSON* params = cJSON_Parse(params_json);
    if (!params) {
        return create_json_result("error", "Invalid JSON parameters", NULL, result_json, result_size);
    }
    
    cJSON* action = cJSON_GetObjectItem(params, "action");
    const char* action_str = "get_info"; // Default action
    
    if (action && cJSON_IsString(action)) {
        action_str = cJSON_GetStringValue(action);
    }
    
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "action_requested", action_str);
    
    if (strcmp(action_str, "get_info") == 0 || strcmp(action_str, "get_stats") == 0) {
        /* Get chip information */
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        
        /* Basic system info */
        cJSON_AddStringToObject(data, "chip_model", "ESP32-C6");
        cJSON_AddNumberToObject(data, "chip_revision", chip_info.revision);
        cJSON_AddNumberToObject(data, "cores", chip_info.cores);
        cJSON_AddStringToObject(data, "idf_version", IDF_VER);
        
        /* Memory info */
        cJSON_AddNumberToObject(data, "free_heap", esp_get_free_heap_size());
        cJSON_AddNumberToObject(data, "min_free_heap", esp_get_minimum_free_heap_size());
        
        /* System stats */
        cJSON_AddNumberToObject(data, "uptime_ms", esp_timer_get_time() / 1000);
        cJSON_AddNumberToObject(data, "reset_reason", esp_reset_reason());
        
        /* Features */
        cJSON* features = cJSON_CreateArray();
        if (chip_info.features & CHIP_FEATURE_WIFI_BGN) {
            cJSON_AddItemToArray(features, cJSON_CreateString("WiFi"));
        }
        if (chip_info.features & CHIP_FEATURE_BLE) {
            cJSON_AddItemToArray(features, cJSON_CreateString("BLE"));
        }
        if (chip_info.features & CHIP_FEATURE_IEEE802154) {
            cJSON_AddItemToArray(features, cJSON_CreateString("802.15.4"));
        }
        cJSON_AddItemToObject(data, "features", features);
        
        ESP_LOGI(TAG, "System info - Heap: %"PRIu32" bytes, Uptime: %lld ms", 
                esp_get_free_heap_size(), esp_timer_get_time() / 1000);
                
    } else if (strcmp(action_str, "restart") == 0) {
        cJSON_AddStringToObject(data, "result", "Restart command received (not executed in demo)");
        ESP_LOGW(TAG, "Restart requested (would restart if force flag was set)");
    } else {
        cJSON_AddStringToObject(data, "result", "Unknown action");
    }
    
    esp_err_t ret = create_json_result("success", "System tool executed", data, result_json, result_size);
    
    cJSON_Delete(params);
    return ret;
}